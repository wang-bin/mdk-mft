/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0601 // for IMFTrackedSample. doc says it's available for vista, but win sdk requires win7
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0601
# endif
#include "base/ms/MFGlue.h"
#include "base/ByteArrayBuffer.h"
#include "mdk/MediaInfo.h"
#include "mdk/VideoFrame.h"
#if (MS_API_DESKTOP+0)
#include <d3d9.h>
#endif
#include <d3d11.h>
#include <mfidl.h>
#include <Mferror.h>
# pragma pop_macro("_WIN32_WINNT")

MDK_NS_BEGIN
namespace MF {

struct mf_pix_fmt_entry {
    const GUID& guid;
    PixelFormat pixfmt;
};

// https://docs.microsoft.com/en-us/windows/desktop/medfound/video-subtype-guids
static const struct mf_pix_fmt_entry mf_pixfmts[] = {
    {MFVideoFormat_IYUV, PixelFormat::YUV420P},
    {MFVideoFormat_I420, PixelFormat::YUV420P},
    {MFVideoFormat_NV12, PixelFormat::NV12},
    {MFVideoFormat_P010, PixelFormat::P010LE},
    {MFVideoFormat_P016, PixelFormat::P016LE},
    {MFVideoFormat_YUY2, PixelFormat::YUYV422},
    {MFVideoFormat_UYVY, PixelFormat::UYVY422},
};

bool to_pixfmt(PixelFormat* pixfmt, const GUID& guid)
{
    for (const auto i : mf_pixfmts) {
        if (i.guid == guid) {
            *pixfmt = i.pixfmt;
            return true;
        }
    }
    return false;
}

bool from_pixfmt(GUID* guid, PixelFormat pixfmt)
{
    for (const auto i : mf_pixfmts) {
        if (i.pixfmt == pixfmt) {
            *guid = i.guid;
            return true;
        }
    }
    return false;
}

bool to(VideoFormat& fmt, const IMFAttributes* ca)
{
    GUID subtype;
    auto a = const_cast<IMFAttributes*>(ca);
    MS_ENSURE(a->GetGUID(MF_MT_SUBTYPE, &subtype), false);
    PixelFormat pixfmt;
    if (!to_pixfmt(&pixfmt, subtype))
        return false;
    fmt = pixfmt;
    return true;
}

bool from(const VideoFormat& fmt, IMFAttributes* a)
{
    return false;
}
#undef DEFINE_GUID
#define DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) EXTERN_C const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
// FIXME: defined in evr.lib?
DEFINE_GUID(MR_BUFFER_SERVICE, 0xa562248c, 0x9ac6, 0x4ffc, 0x9f, 0xba, 0x3a, 0xf8, 0xf8, 0xad, 0x1a, 0x4d );

class MFBuffer2DState {
    ComPtr<IMFMediaBuffer> buf_;
    ComPtr<IMF2DBuffer> buf2d_;
public:
    MFBuffer2DState(ComPtr<IMFMediaBuffer> b) : buf_(b) {}
    MFBuffer2DState(ComPtr<IMF2DBuffer> b) : buf2d_(b) {}
    MFBuffer2DState(ComPtr<IMF2DBuffer2> b) : buf2d_(b) {}
    ~MFBuffer2DState() {
        if (buf2d_)
            MS_WARN(buf2d_->Unlock2D());
        if (buf_)
            MS_WARN(buf_->Unlock());
    }
};

class MFPlaneBuffer2D final: public Buffer2D
{
    const Byte* const data_; // not writable, so const Byte*
    size_t size_;
    size_t stride_;
    std::shared_ptr<MFBuffer2DState> state_;
public:
    MFPlaneBuffer2D(const uint8_t* data, size_t size, int stride, std::shared_ptr<MFBuffer2DState> state)
        : data_(data), size_(size), stride_(stride), state_(state)
    {}
    const Byte* constData() const override {return data_;}
    size_t size() const override { return size_;}
    size_t stride() const override { return stride_;}
};

bool setBuffersTo(VideoFrame& frame, ComPtr<IMFMediaBuffer> buf, int stride_x = 0, int stride_y = 0, bool copy = false)
{
    BYTE* data = nullptr;
    DWORD len = 0;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> buf2d;
    ComPtr<IMF2DBuffer2> buf2d2;
    std::shared_ptr<MFBuffer2DState> bs;

    if (SUCCEEDED(buf.As(&buf2d2))) { // usually d3d buffer
        bs = std::make_shared<MFBuffer2DState>(buf2d2);
        BYTE* start = nullptr; // required
        MS_ENSURE(buf2d2->Lock2DSize(MF2DBuffer_LockFlags_Read, &data, &pitch, &start, &len), false);
    } else if (SUCCEEDED(buf.As(&buf2d))) { // usually d3d buffer
        bs = std::make_shared<MFBuffer2DState>(buf2d);
        MS_ENSURE(buf2d->Lock2D(&data, &pitch), false);
    } else { // slower than 2d if is 2d
        bs = std::make_shared<MFBuffer2DState>(buf);
        MS_ENSURE(buf->Lock(&data, nullptr, &len), false);
    }
    if (pitch <= 0)
        pitch = stride_x;
    const uint8_t* da[] = {data, nullptr, nullptr}; // assume no padding data if da[i>0] == null
    int strides[] = {(int)pitch, (int)pitch, (int)pitch};
    const auto& fmt = frame.format();
    for (int plane = 0; plane < fmt.planeCount(); ++plane) {
        if (pitch <= 0)
            pitch = frame.effectiveBytesPerLine(plane);
        const size_t bytes = pitch*fmt.height(stride_y, plane);
        if (copy)
            da[plane] = data;
        else
            frame.addBuffer(std::make_shared<MFPlaneBuffer2D>(data, bytes, (int)pitch, bs)); // same stride for each plane?
        data += bytes;
    }
    if (copy)
        frame.setBuffers(da, strides);
    return true;
}

bool to(VideoFrame& frame, ComPtr<IMFSample> sample, int stride_x, int stride_y, int copy)
{
    LONGLONG t = 0;
    if (SUCCEEDED(sample->GetSampleTime(&t)))
        frame.setTimestamp(from_mf_time(t));
    DWORD nb_bufs = 0;
    MS_ENSURE(sample->GetBufferCount(&nb_bufs), false);
    const auto& fmt = frame.format();
    const bool contiguous = fmt.planeCount() > (int)nb_bufs;
    for (DWORD i = 0; i < nb_bufs; ++i) {
        ComPtr<IMFMediaBuffer> buf;
        MS_ENSURE(sample->GetBufferByIndex(i, &buf), false);
        ComPtr<IMFDXGIBuffer> dxgibuf; // MFCreateDXGISurfaceBuffer
        struct {
            ID3D11Texture2D* tex;
            int index;
        } texinfo;
        void* opaque = nullptr;
        if (SUCCEEDED(buf.As(&dxgibuf))) {
            MS_WARN(dxgibuf->GetResource(IID_PPV_ARGS(&texinfo.tex)));
            UINT subidx = 0;
            MS_WARN(dxgibuf->GetSubresourceIndex(&subidx));
            texinfo.index = subidx;
            opaque = &texinfo;
        }
#if (MS_API_DESKTOP+0)
        ComPtr<IDirect3DSurface9> d3d9surf; // MFCreateDXSurfaceBuffer
        //ComPtr<IMFGetService> getsv; MS_WARN(buf.As(&getsv)); IMFGetService::GetService
        if (!opaque && SUCCEEDED(MFGetService(buf.Get(), MR_BUFFER_SERVICE, IID_PPV_ARGS(&d3d9surf))))
            opaque = d3d9surf.Get();
#endif
        if (opaque) {
            if (copy > 0) {
                frame.setNativeBuffer(nullptr);
                setBuffersTo(frame, buf, stride_x, stride_y, copy > 1);
                return true;
            }
            // d3d: The sample will contain exactly one media buffer
            auto nbuf = frame.nativeBuffer();
            if (nbuf) {
                auto pool = (NativeVideoBufferPool*)nbuf->map(NativeVideoBuffer::Original, nullptr);
                if (pool) {
                    frame.setNativeBuffer(pool->getBuffer(opaque));
                    return true;
                }
            }
        }

        DWORD len = 0;
        MS_ENSURE(buf->GetCurrentLength(&len), false);
        if (contiguous) {
            setBuffersTo(frame, buf, stride_x, stride_y, copy > 0);
        } else {
            ComPtr<IMF2DBuffer> buf2d;
            if (SUCCEEDED(buf.As(&buf2d))) {
            }
            if (buf2d) {
                frame.addBuffer(to(buf2d), i);
            } else {
                // TODO: no copy. Buffer2DRef to(ComPtr<IMFBuffer> b, int stride, int height, ptrdiff_t offset = 0, int size = -1);
                BYTE* data = nullptr;
                MS_ENSURE(buf->Lock(&data, nullptr, &len), false);
                frame.addBuffer(std::make_shared<ByteArrayBuffer2D>(len/fmt.height(stride_y, i), fmt.height(stride_y, i), data));
                buf->Unlock();
            }
        }
    }
    return true;
}

bool from(const VideoCodecParameters& par, IMFAttributes* a)
{
    MS_ENSURE(a->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), false);
    MS_ENSURE(a->SetGUID(MF_MT_SUBTYPE, *codec_for(par.codec, MediaType::Video)), false);
    MS_ENSURE(a->SetUINT64(MF_MT_FRAME_SIZE, Pack2UINT32AsUINT64(par.width, par.height)), false);
    //MS_ENSURE(MFSetAttributeSize(a.Get(), MF_MT_FRAME_SIZE, par.width, par.height), nullptr); // vista+
    MS_ENSURE(a->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive), false);
    //MS_ENSURE(MFSetAttributeRatio(a, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), false); // TODO:

    if (par.bit_rate > 0)
        MS_ENSURE(a->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)par.bit_rate), false);

    // TODO: mp4 extra data check?
    bool use_extra = !par.extra.empty(); // and if no bsf
    if (use_extra) {
        if (par.codec.find("mpeg4") != std::string::npos) {
            if (par.extra.size() < 3 || par.extra[0] || par.extra[1] || par.extra[2] != 1)
                use_extra = false;
        } else if (par.codec == "h264" || par.codec == "hevc" || par.codec == "h265") {
            if (par.extra[0] == 1) // avcC/hvcC
                use_extra = false;
        }
        UINT32 blob_size = 0; // TODO: vlc set MF_MT_USER_DATA if not exist (MF_E_ATTRIBUTENOTFOUND)
        if (a->GetBlobSize(MF_MT_USER_DATA, &blob_size) == MF_E_ATTRIBUTENOTFOUND)
            use_extra = false;
    }
    if (use_extra)
        MS_ENSURE(a->SetBlob(MF_MT_USER_DATA, par.extra.data(), par.extra.size()), false);
    return true;
}

} // namespace MF
MDK_NS_END