/*
 * Copyright (c) 2018-2019 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
 // https://searchfox.org/mozilla-central/source/dom/media/platforms/wmf
/*
MF_TRANSFORM_ASYNC_UNLOCK: MUST set for async (set by the whole MF pipeline)
MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE: (Get)always true for async

vista: sync, like mediacodec
win7+: async (event). METransformNeedInput...: desktop only

uwp: https://www.eternalcoding.com/?p=413

https://docs.microsoft.com/zh-cn/uwp/win32-and-com/win32-apis
https://docs.microsoft.com/zh-cn/uwp/win32-and-com/alternatives-to-windows-apis-uwp
https://docs.microsoft.com/zh-cn/previous-versions/windows/mt592899%28v%3dwin.10%29
https://docs.microsoft.com/en-us/uwp/api/windows.media

https://docs.microsoft.com/zh-cn/windows/desktop/medfound/uncompressed-audio-media-types
https://docs.microsoft.com/zh-cn/windows/desktop/medfound/uncompressed-video-media-types

BUG:
- (intel HD 520) hevc main10 d3d11 decoding blocks(via store extension), works again after seek. (ffmpeg d3d11/dxva works fine). gpu render core is used instead of gpu decoder.

Compare with FFmpeg D3D11/DXVA:
- (intel HD 520) MFT supports gpu decoding for hevc but ffmpeg d3d11 does not, instead it use render core(hybrid mode?)
- (intel HD 520) ffmpeg d3d11 hevc wrong output size and color. because of hybrid decoding?
- (intel HD 520) MFT may failed to set dxva device manager for hevc main10, but ffmpeg dxva works?
*/
//#ifdef _MSC_VER
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0602 // for d3d11 etc. _WIN32_WINNT_WIN8 is not defined yet
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0602
# endif
#include "mdk/VideoDecoder.h"
#include "mdk/MediaInfo.h"
#include "mdk/Packet.h"
#include "mdk/VideoFrame.h"
#include "AnnexBFilter.h"
#include "base/ByteArray.h"
#include "base/ms/MFTCodec.h"
#include "video/d3d/D3D9Utils.h"
#include "video/d3d/D3D11Utils.h"
#include <codecapi.h>
#include <Mferror.h>
#include <iostream>
//#ifdef _MSC_VER
# pragma pop_macro("_WIN32_WINNT")

// properties: pool=1(0, 1), d3d=0(0, 9, 11), copy=0(0, 1, 2), adapter=0
MDK_NS_BEGIN
using namespace std;
class MFTVideoDecoder final : public VideoDecoder, protected MFTCodec
{
public:
    MFTVideoDecoder() {
        useSamplePool(std::stoi(property("pool", "1")));
        copy_ = std::stoi(property("copy", "0"));
    }
    const char* name() const override {return "MFT";}
    bool open() override;
    bool close() override;
    bool flush() override {
        bool ret =  flushCodec();
        onFlush();
        return ret;
    }
    bool decode(const Packet& pkt) override { return decodePacket(pkt); }
private:
    void onPropertyChanged(const std::string& key, const std::string& value) override {
        if (key == "copy")
            copy_ = std::stoi(value);
        else if (key == "pool")
            useSamplePool(std::stoi(value));
    }

    bool onMFTCreated(ComPtr<IMFTransform> mft) override;
    bool testConstraints(ComPtr<IMFTransform> mft);
    uint8_t* filter(uint8_t* data, size_t* size) override;

    virtual bool setInputTypeAttributes(IMFAttributes* attr) override;
    virtual bool setOutputTypeAttributes(IMFAttributes* attr) override;
    virtual int getInputTypeScore(IMFAttributes* attr) override;
    virtual int getOutputTypeScore(IMFAttributes* attr) override;
    bool onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type) override; // width/height, pixel format, yuv mat, color primary/transfer func/chroma sitting/range, par
    bool onOutput(ComPtr<IMFSample> sample) override;

    // properties
    int copy_ = 0;
    int use_d3d_ = 0;

    const CLSID* codec_id_ = nullptr;
    int nal_size_ = 0;
    int csd_size_ = 0;
    uint8_t* csd_ = nullptr;
    ByteArray csd_pkt_;
    VideoFrame frame_param_;
    UINT32 stride_x_ = 0;
    UINT32 stride_y_ = 0;
#if (MS_API_DESKTOP+0)
    D3D9::Manager mgr9_;
#endif
    D3D11::Manager mgr11_;
    NativeVideoBufferPoolRef pool_;
};

bool MFTVideoDecoder::open()
{
    const auto& par = parameters();
    codec_id_ = MF::codec_for(par.codec, MediaType::Video);
    if (!codec_id_) {
        std::clog << "codec is not supported: " << par.codec << std::endl;
        return false;
    }
    // https://docs.microsoft.com/en-us/windows/desktop/medfound/h-264-video-decoder#format-constraints
    // TODO: other codecs
    if (*codec_id_ == MFVideoFormat_H264) {
        if (par.profile > 100) {
            std::clog << "H264 profile is not supported by MFT. Max is High(100)" << std::endl;
            return false;
        }
        if (par.level > 51) {
            std::clog << "H264 level is not supported by MFT. Max is 5.1" << std::endl;
            return false;
        }
        // chroma subsample?
    }
    nal_size_ = 0;
    // http://www.howtobuildsoftware.com/index.php/how-do/9vN/c-windows-ms-media-foundation-mf-doesnt-play-video-from-my-source
    if (!par.extra.empty()) {
        auto extra = par.extra;
        typedef uint8_t* (*to_annexb_t)(const uint8_t* extradata, int extrasize, int* out_size, int* nalu_field_size);
        to_annexb_t to_annexb_func = nullptr;
        if (strstr(par.codec.data(), "h264"))
            to_annexb_func = avcc_to_annexb_extradata;
        else if (strstr(par.codec.data(), "hevc") || strstr(par.codec.data(), "h265"))
            to_annexb_func = hvcc_to_annexb_extradata;
        if (to_annexb_func) {
            if (!is_annexb(extra.data(), (int)extra.size())) {
                std::clog << "try to convert extra data to annexb" << std::endl;
                csd_ = to_annexb_func(extra.data(), (int)extra.size(), &csd_size_, &nal_size_);
            }
        }
    }
    if (!openCodec(MediaType::Video, *codec_id_))
        return false;
    std::clog << "MFT decoder is ready" << std::endl;
    onOpen();
    return true;
}

bool MFTVideoDecoder::close()
{
    if (csd_)
        free(csd_);
    csd_ = nullptr;
    csd_size_ = 0;
    csd_pkt_.clear();
    bool ret = closeCodec(); 
    onClose();
    return ret;
}

bool MFTVideoDecoder::onMFTCreated(ComPtr<IMFTransform> mft)
{
    if (!testConstraints(mft))
        return false;
    use_d3d_ = std::stoi(property("d3d", "0"));
    uint32_t adapter = std::stoi(property("adapter", "0"));
    ComPtr<IMFAttributes> a;
    MS_ENSURE(mft->GetAttributes(&a), false);
    // d3d: https://docs.microsoft.com/en-us/windows/desktop/medfound/direct3d-aware-mfts
    UINT32 d3d_aware = 0;
#if (MS_API_DESKTOP+0)
    if (use_d3d_ == 9 && SUCCEEDED(a->GetUINT32(MF_SA_D3D_AWARE, &d3d_aware)) && d3d_aware
        && mgr9_.init(adapter)) {
        auto mgr = mgr9_.create();
        if (mgr) {
            HRESULT hr = S_OK;
            MS_WARN((hr = mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)mgr.Get())));
            if (SUCCEEDED(hr))
                pool_ = NativeVideoBufferPool::create("D3D9");
        }
    }
#endif
    if (use_d3d_ == 11)
        MS_WARN(a->GetUINT32(MF_SA_D3D11_AWARE, &d3d_aware));
    if (use_d3d_ == 11 && d3d_aware && mgr11_.init(adapter)) {
        auto mgr = mgr11_.create();
        if (mgr) {
            HRESULT hr = S_OK;
            MS_WARN((hr = mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)mgr.Get())));
            if (SUCCEEDED(hr))
                pool_ = NativeVideoBufferPool::create("D3D11");
        } else {
            std::clog << "failed to create IMFDXGIDeviceManager. MFT d3d11 will be disabled." << std::endl;
        }
    }

    wchar_t vendor[128]{}; // set vendor id?
    if (SUCCEEDED(a->GetString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, vendor, sizeof(vendor), nullptr))) // win8+, so warn only
        printf("hw vendor id: %ls\n", vendor);
    if (SUCCEEDED(a->GetString(MFT_ENUM_HARDWARE_URL_Attribute, vendor, sizeof(vendor), nullptr))) // win8+, so warn only
        printf("hw url: %ls\n", vendor);
    // MFT_ENUM_HARDWARE_URL_Attribute 
    // https://docs.microsoft.com/zh-cn/windows/desktop/DirectShow/codec-api-properties
    // or ICodecAPI.SetValue(, &VARIANT{.vt=VT_UI4, .uintVal=1})
    MS_WARN(a->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, 1));
    // faster but lower quality. h264 for win8+
    MS_WARN(a->SetUINT32(CODECAPI_AVLowLatencyMode, 1)); // .vt = VT_BOOL, .boolVal = VARIANT_TRUE fails
    return true;
}

bool MFTVideoDecoder::testConstraints(ComPtr<IMFTransform> mft)
{
    // ICodecAPI is optional. same attributes
    ComPtr<IMFAttributes> a;
    MS_ENSURE(mft->GetAttributes(&a), false);
    UINT32 max_w = 0, max_h = 0;
    if (SUCCEEDED(a->GetUINT32(CODECAPI_AVDecVideoMaxCodedWidth, &max_w)) // TODO: we can set max value?
        && SUCCEEDED(a->GetUINT32(CODECAPI_AVDecVideoMaxCodedHeight, &max_h))) {
        std::clog << "max supported size: " << max_w << "x" << max_h << std::endl;
        // FIXME: par.width/height is not coded value
        if ((max_w > 0 && (int)max_w < parameters().width) || (max_h > 0 && (int)max_h <= parameters().height)) {
            std::clog << "unsupported frame size" << std::endl;
            return false;
        }
    }
    return true;
}

uint8_t* MFTVideoDecoder::filter(uint8_t* data, size_t* size)
{
    if (nal_size_ <= 0)
        return nullptr;
    if (nal_size_ > 0) { // TODO: PacketFilter
        //data = pkt.buffer->data(); // modify constData() if pkt is rvalue
        to_annexb_packet(data, (int)*size, nal_size_);
        
    }
    if (!csd_pkt_.empty())
        return nullptr;
    csd_pkt_.resize(csd_size_ + (int)*size);
    memcpy(csd_pkt_.data(), csd_, csd_size_);
    memcpy(csd_pkt_.data() + csd_size_, data, *size);
    *size = csd_pkt_.size();
    return csd_pkt_.data();
}

bool MFTVideoDecoder::setInputTypeAttributes(IMFAttributes* a)
{
    if (csd_ && false) { // TODO: no effect?
        // TODO: vlc set MF_MT_USER_DATA if not exist (MF_E_ATTRIBUTENOTFOUND)?
        UINT32 blob_size = 0;
        if (a->GetBlobSize(MF_MT_USER_DATA, &blob_size) == MF_E_ATTRIBUTENOTFOUND)
            MS_ENSURE(a->SetBlob(MF_MT_USER_DATA, csd_, csd_size_), false);
    }
    return MF::from(parameters(), a);
}

bool MFTVideoDecoder::setOutputTypeAttributes(IMFAttributes* attr)
{
    return true;
}

int MFTVideoDecoder::getInputTypeScore(IMFAttributes* attr)
{
    GUID id;
    MS_ENSURE(attr->GetGUID(MF_MT_SUBTYPE, &id), -1);
    if (id != *codec_id_) // TODO: always same id because mft is activated from same codec id?
        return -1;
    //if (codec_tag > 0 && FOURCCMap(id).GetFOURCC() == codec_tag) // TODO: if not constructed from a valid fcc, return value is random
    //    return 2;
    // no fourcc.h?
    const auto tag = parameters().codec_tag;
    if (tag > 0 && to_fourcc(id) == tag)
        return 2;
    return 1;
}

int MFTVideoDecoder::getOutputTypeScore(IMFAttributes* attr)
{
    GUID subtype;
    MS_ENSURE(attr->GetGUID(MF_MT_SUBTYPE, &subtype), -1);
    VideoFormat vf;
    if (!MF::to(vf, attr))
        return -1;
    // if not the same as input format(depth), MF_E_TRANSFORM_STREAM_CHANGE will occur and have to select again(in a dead loop). e.g. input vp9 is 8bit, but p010 is selected, mft can refuse to use it.
    return vf.bitsPerChannel() == VideoFormat(parameters().format).bitsPerChannel();
}

bool MFTVideoDecoder::onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type)
{
    ComPtr<IMFAttributes> a;
    MS_ENSURE(type.As(&a), false);
    VideoFormat outfmt;
    if (!MF::to(outfmt, a.Get()))
        return false;
    std::clog << "output format: " << outfmt << std::endl;
    // MFGetAttributeSize: vista+
    UINT64 dim = 0;
    MS_ENSURE(a->GetUINT64(MF_MT_FRAME_SIZE, &dim), false);
    UINT32 w = 0, h = 0;
    Unpack2UINT32AsUINT64(dim, &w, &h);
    stride_x_ = outfmt.bytesPerLine(w, 0);
    stride_y_ = h;
    std::clog << "output size: " << w << "x" << h << ", stride: " << stride_x_ << "x" << stride_y_ << std::endl;
    MFVideoArea area{}; // desktop only?
    if (SUCCEEDED(a->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), nullptr))) {
        std::clog << "video area: (" << area.OffsetX.value << ", " << area.OffsetY.value << "), " << area.Area.cx << "x" << area.Area.cy << std::endl;
        w = area.Area.cx;
        h = area.Area.cy;
    }
    ColorSpace cs;
    MF::to(cs, a.Get());
    frame_param_ = VideoFrame(w, h, outfmt);
    frame_param_.setColorSpace(cs, true);
    // TODO: MF_MT_PIXEL_ASPECT_RATIO, MF_MT_YUV_MATRIX, MF_MT_VIDEO_PRIMARIES, MF_MT_TRANSFER_FUNCTION, MF_MT_VIDEO_CHROMA_SITING, MF_MT_VIDEO_NOMINAL_RANGE
    // MFVideoPrimaries_BT709, MFVideoPrimaries_BT2020
    // MF_MT_TRANSFER_FUNCTION:  MFVideoTransFunc_2020(_const)
    if (use_d3d_ == 11 && pool_) {
        //MS_ENSURE(mft_->GetOutputStreamAttributes(streamId, &a), false);
        // win8 attributes
        //MS_ENSURE(a->SetUINT32(MF_SA_D3D11_SHARED , 1), false); // shared via keyed-mutex. FIXME: ProcessInput() error
        //MS_ENSURE(a->SetUINT32(MF_SA_D3D11_SHARED_WITHOUT_MUTEX , 1), false); // shared via legacy mechanism
        //MS_ENSURE(a->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_SHADER_RESOURCE), false); // optional?
        // MF_SA_D3D11_USAGE
        //MS_ENSURE(a->SetUINT32(MF_SA_BUFFERS_PER_SAMPLE , 1), false); // 3d video
    }
    return true;
}

bool MFTVideoDecoder::onOutput(ComPtr<IMFSample> sample)
{
    class PoolBuffer : public NativeVideoBuffer
    {
        NativeVideoBufferPoolRef pool_;
    public:
        PoolBuffer(NativeVideoBufferPoolRef pool) : pool_(pool) {}
        void* map(Type type, MapParameter*) override { return pool_.get();}
    };
    VideoFrame frame(frame_param_.width(), frame_param_.height(), frame_param_.format());
    if (pool_)
        frame.setNativeBuffer(std::make_shared<PoolBuffer>(pool_));
    if (!MF::to(frame, sample, (int)stride_x_, (int)stride_y_, copy_))
        return false;
    ColorSpace cs;
    frame_param_.colorSpace(&cs, false);
    frame.setColorSpace(cs, true);
    frameDecoded(frame);
    return true;
}

void register_video_decoders_mf() {
    VideoDecoder::registerOnce("MFT", []{return new MFTVideoDecoder();});
}
namespace { // DCE
static const struct register_at_load_time_if_no_dce {
    inline register_at_load_time_if_no_dce() { register_video_decoders_mf();}
} s;
}
MDK_NS_END