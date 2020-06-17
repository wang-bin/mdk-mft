/*
 * Copyright (c) 2018~2019 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "MFGlue.h"
#include "mdk/Buffer.h"
#include "mdk/MediaInfo.h"
#include "mdk/Packet.h"
#include <atomic>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string_view>
#include "cppcompat/cstdio.hpp"
#include "base/log.h"

MDK_NS_BEGIN
namespace MF {
/*
IMFMediaBuffer, IMFSample, IMFMediaType:
This interface is available on the following platforms if the Windows Media Format 11 SDK redistributable components are installed:
Windows XP SP2+, Windows XP Media Center Edition 2005 with KB900325 (Windows XP Media Center Edition 2005) and KB925766 (October 2006 Update Rollup for Windows XP Media Center Edition) installed.

TODO: dynamic load new dlls, or delay load?
*/
class MFBuffer final : public Buffer {
    int size_;
    ptrdiff_t offset_;
    // FIXME: ptr after unlock is invalid. so return an object can be implicitly converted to unit8_t* and unlock in it's dtor and use as auto?
    // or add lock/unlock in Buffer?
    mutable uint8_t* locked_ = nullptr;
    mutable ComPtr<IMFMediaBuffer> mfbuf_;
public:
    MFBuffer(ComPtr<IMFMediaBuffer> mfbuf, ptrdiff_t offset = 0, int size = -1)
        : size_(size)
        , offset_(offset)
        , mfbuf_(mfbuf)
    {}
    ~MFBuffer() {
        if (locked_)
            MS_WARN(mfbuf_->Unlock());
    }

    const uint8_t* constData() const override {
        if (!locked_)
            MS_ENSURE(mfbuf_->Lock(&locked_, nullptr, nullptr), nullptr); //D3DERR_INVALIDCALL: if d3d surface is not lockable
        return locked_ + offset_;
    }
    size_t size() const override {
        if (size_ > 0)
            return size_;
        DWORD len = 0;
        MS_ENSURE(mfbuf_->GetMaxLength(&len), 0);
        return len  - offset_;
    }
};

BufferRef to(ComPtr<IMFMediaBuffer> b, ptrdiff_t offset, int size)
{
    if (!b)
        return nullptr;
    return std::make_shared<MFBuffer>(b, offset, size);
}

class MFBuffer2D final : public Buffer2D {
    int size_;
    mutable LONG stride_ = 0;
    ptrdiff_t offset_;
    // FIXME: ptr after unlock is invalid. so return an object can be implicitly converted to unit8_t* and unlock in it's dtor and use as auto?
    // or add lock/unlock in Buffer?
    mutable uint8_t* locked_ = nullptr;
    mutable ComPtr<IMF2DBuffer> mfbuf_;
public:
    MFBuffer2D(ComPtr<IMF2DBuffer> mfbuf, ptrdiff_t offset = 0, int size = -1)
        : size_(size)
        , offset_(offset)
        , mfbuf_(mfbuf)
    {}
    ~MFBuffer2D() {
        if (locked_)
            MS_WARN(mfbuf_->Unlock2D());
    }

    const uint8_t* constData() const override {
        if (!ensureLock())
            return nullptr;
        return locked_ + offset_;
    }
    size_t size() const override {
        if (size_ > 0)
            return size_;
        ComPtr<IMFMediaBuffer> b;
        MS_ENSURE(mfbuf_.As(&b), 0);
        DWORD len = 0;
        MS_ENSURE(b->GetMaxLength(&len), 0);
        return len  - offset_;
    }
    size_t stride() const override {
        if (!ensureLock())
            return 0;
        return stride_; // FIXME: stride<0: bottom up
    }
private:
    bool ensureLock() const {
        if (locked_)
            return true;
        ComPtr<IMF2DBuffer2> b; // FIXME: win8+. faster?
        if (SUCCEEDED(mfbuf_.As(&b))) // TODO: ppbBufferStart instead of ppbScanline0 to work with plPitch<0
            MS_ENSURE(b->Lock2DSize(MF2DBuffer_LockFlags_Read, &locked_, &stride_, nullptr, nullptr), false); //D3DERR_INVALIDCALL: if d3d surface is not lockable
        else
            MS_ENSURE(mfbuf_->Lock2D(&locked_, &stride_), false); //D3DERR_INVALIDCALL: if d3d surface is not lockable
        return true;
    }
};

Buffer2DRef to(ComPtr<IMF2DBuffer> b, ptrdiff_t offset, int size)
{
    if (!b)
        return nullptr;
    return std::make_shared<MFBuffer2D>(b, offset, size);
}


#if (_MSC_VER + 0) // RuntimeClass is missing in mingw
class MFMediaBufferView : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFMediaBuffer>  // IUnknown is implemented by RuntimeClass
{
    BufferRef buf_;
public:
    MFMediaBufferView(BufferRef b) : buf_(b) {}
    HRESULT STDMETHODCALLTYPE Lock(BYTE **ppbBuffer, _Out_opt_  DWORD *pcbMaxLength, _Out_opt_  DWORD *pcbCurrentLength) override {
        *ppbBuffer = buf_->data();
        if (pcbMaxLength)
            *pcbMaxLength = (DWORD)buf_->size();
        if (pcbCurrentLength)
            *pcbCurrentLength = (DWORD)buf_->size();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Unlock() override {return S_OK;}

    HRESULT STDMETHODCALLTYPE GetCurrentLength(_Out_  DWORD *pcbCurrentLength) override {
        *pcbCurrentLength = (DWORD)buf_->size();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCurrentLength(DWORD cbCurrentLength) override {return S_OK;}

    HRESULT STDMETHODCALLTYPE GetMaxLength(_Out_  DWORD *pcbMaxLength) override {
        *pcbMaxLength = (DWORD)buf_->size();
        return S_OK;
    }
};
#endif // (_MSC_VER + 0)

ComPtr<IMFMediaBuffer> from(BufferRef buf, int align)
{
    ComPtr<IMFMediaBuffer> b;
    const auto a = std::max<int>(align, 16);
#if (_MSC_VER + 0) // RuntimeClass is missing in mingw
    if (((intptr_t)buf->constData() & (a-1)) == 0)
        return Make<MFMediaBufferView>(buf);
#endif
    MS_ENSURE(MFCreateAlignedMemoryBuffer((DWORD)buf->size(), a - 1, &b), nullptr);
    BYTE* ptr = nullptr;
    MS_ENSURE(b->Lock(&ptr, nullptr, nullptr), nullptr);
    memcpy(ptr, buf->constData(), buf->size());
    MS_ENSURE(b->SetCurrentLength((DWORD)buf->size()), nullptr); // necessary?
    b->Unlock();
    return b;
}

ComPtr<IMF2DBuffer> from(Buffer2DRef buf)
{
    ComPtr<IMF2DBuffer> b;
    // MFCreate2DMediaBuffer: win8
    return b;
}


// TODO: IMFAttributes
//IMFSample::SetSampleTime method: 100-nanosecond
ComPtr<IMFSample> from(const Packet& pkt, int align)
{
    if (!pkt)
        return nullptr;
    ComPtr<IMFSample> p;
    MS_ENSURE(MFCreateSample(&p), nullptr);
    MS_ENSURE(p->AddBuffer(from(pkt.buffer, align).Get()), nullptr); // take the ownership. E_INVALIDARG: AddBuffer(nullptr)
    p->SetSampleTime(to_mf_time(pkt.pts));
    if (pkt.duration > 0)
        p->SetSampleDuration(to_mf_time(pkt.duration)); // MF_E_NO_SAMPLE_DURATION in GetSampleDuration() if not called
    if (pkt.hasKeyFrame) {
        ComPtr<IMFAttributes> attr;
        if (SUCCEEDED(p.As(&attr)))
            attr->SetUINT32(MFSampleExtension_CleanPoint, 1);
    }
    return p;
}

void to(Packet& pkt, ComPtr<IMFSample> mfpkt)
{
    ComPtr<IMFMediaBuffer> b; // sample is compressed, no need to use ConvertToContiguousBuffer()
    MS_ENSURE(mfpkt->GetBufferByIndex(0, &b));
    pkt.buffer = to(b);
    LONGLONG t = 0;
    if (SUCCEEDED(mfpkt->GetSampleTime(&t)))
        pkt.pts = from_mf_time(t);
    if (SUCCEEDED(mfpkt->GetSampleDuration(&t)))
        pkt.duration = from_mf_time(t);
}

const CLSID* codec_for(const std::string& name, MediaType type)
{
    using codec_id_map = std::unordered_map<std::string_view, const CLSID*>;
    // https://gix.github.io/media-types
    // TODO: MediaInfo codec fourcc(index_sequence)? enum forcc<char*>::value, static_assert([4]==0)
    static const codec_id_map acodec_id{
        {"ac3", &MFAudioFormat_Dolby_AC3},
        {"eac3", &MFAudioFormat_Dolby_DDPlus},
        {"aac", &MFAudioFormat_AAC},
        {"mp3", &MFAudioFormat_MP3},
        {"mp2", &MFAudioFormat_MPEG},
        {"mp1", &MFAudioFormat_MPEG},
        {"wmavoice", &MFAudioFormat_MSP1},
        //{"wmav1", &MFAudioFormat_MSAUDIO1},
        {"wmav2", &MFAudioFormat_WMAudioV8},
        {"wmapro", &MFAudioFormat_WMAudioV9},
        {"wmalossless", &MFAudioFormat_WMAudio_Lossless},
        //{"flac", &MFAudioFormat_FLAC}, //win10+
        //{"opus", &MFAudioFormat_Opus}, //win10+
        // flac, alac, opus, amrnb/wb/wp, dts, msp1, qcelp
    };
    static const codec_id_map vcodec_id{
        {"h264", &MFVideoFormat_H264},
        {"hevc", &MFVideoFormat_HEVC}, // MFVideoFormat_H265 'H265', MFVideoFormat_HEVC_ES , 'HEVS'
        {"vp8", &MFVideoFormat_VP80}, // 'MPG1'
        {"vp9", &MFVideoFormat_VP90}, // 'MPG1'
        {"mjpeg", &MFVideoFormat_MJPG},
        {"mpeg2", &MFVideoFormat_MPEG2},
        {"mpeg4", &MFVideoFormat_MP4V},
        //{"msmpeg4v1", &MFVideoFormat_MP42}, // symbol not defined?
        //{"msmpeg4v2", &MFVideoFormat_MP42},
        {"msmpeg4v3", &MFVideoFormat_MP43},
        {"wmv1", &MFVideoFormat_WMV1},
        {"wmv2", &MFVideoFormat_WMV2},
        {"wmv3", &MFVideoFormat_WMV3},
        {"vc1", &MFVideoFormat_WVC1},
        {"av1", &MFVideoFormat_AV1},
    };
    static const codec_id_map codec_ids[] = {vcodec_id, acodec_id};
    const auto& ids = codec_ids[std::underlying_type<MediaType>::type(type)];
    const auto it = ids.find(name);
    if (it == ids.cend())
        return nullptr;
    return it->second;
}
//  IMFMediaBuffer
// IMF2DBuffer: MFCreateDXSurfaceBuffer(IID_IDirect3DSurface9)  https://docs.microsoft.com/en-us/windows/desktop/medfound/directx-surface-buffer
// IMFDXGIBuffer+IMF2DBuffer(2)+IMFMediaBuffer : MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D)

void dump(IMFAttributes* a)
{
    UINT32 count = 0;
    MS_ENSURE(a->GetCount(&count));
    std::stringstream ss;
    ss << count << " attributes: ";
    for (UINT32 i = 0; i < count; ++i) {
        GUID key;
        char detail[80] = {0};
        MS_ENSURE(a->GetItemByIndex(i, &key, nullptr));
        if (key == MF_MT_AUDIO_CHANNEL_MASK) {
            UINT32 v;
            MS_ENSURE(a->GetUINT32(key, &v));
            std::snprintf(detail, sizeof(detail), " (0x%x)", (unsigned)v);
        } else if (key == MF_MT_FRAME_SIZE) {
            UINT32 w, h;
            MS_ENSURE(MFGetAttributeSize(a, MF_MT_FRAME_SIZE, &w, &h));
            std::snprintf(detail, sizeof(detail), " (%dx%d)", (int)w, (int)h);
        } else if (key == MF_MT_PIXEL_ASPECT_RATIO || key == MF_MT_FRAME_RATE) {
            UINT32 num, den;
            MS_ENSURE(MFGetAttributeRatio(a, key, &num, &den));
            std::snprintf(detail, sizeof(detail), " (%d:%d)", (int)num, (int)den);
        }
        ss << to_name(key) << "=";
        MF_ATTRIBUTE_TYPE type;
        MS_ENSURE(a->GetItemType(key, &type));
        switch (type) {
        case MF_ATTRIBUTE_UINT32: {
            UINT32 v;
            MS_ENSURE(a->GetUINT32(key, &v));
            ss << v;
            break;
        case MF_ATTRIBUTE_UINT64: {
            UINT64 v;
            MS_ENSURE(a->GetUINT64(key, &v));
            ss << v;
            break;
        }
        case MF_ATTRIBUTE_DOUBLE: {
            DOUBLE v;
            MS_ENSURE(a->GetDouble(key, &v));
            ss << v;
            break;
        }
        case MF_ATTRIBUTE_STRING: {
            wchar_t wv[512]; // being lazy here
            MS_ENSURE(a->GetString(key, wv, sizeof(wv), nullptr));
            char cwv[512]{};
            std::snprintf(cwv, sizeof(cwv), "%ls", wv);
            ss << cwv;
            break;
        }
        case MF_ATTRIBUTE_GUID: {
            GUID v;
            MS_ENSURE(a->GetGUID(key, &v));
            ss << to_name(v);
            break;
        }
        case MF_ATTRIBUTE_BLOB: {
            UINT32 sz;
            MS_ENSURE(a->GetBlobSize(key, &sz));
            ss << "(" << sz << ")";
            std::vector<UINT8> buffer(sz, 0);
            MS_ENSURE(a->GetBlob(key, &buffer[0], (UINT32)buffer.size(), &sz));
            ss << std::hex;
            for (const auto x : buffer)
                ss << " " << (int)x;
            ss << std::dec;
            break;
        }
        case MF_ATTRIBUTE_IUNKNOWN:
        default:
            ss << "<UNKNOWN type>";
            break;
        }
        }
        ss << detail << ", ";
    }
    std::clog << ss.str() << std::endl;
}
} // namespace MF
MDK_NS_END