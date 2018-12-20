/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
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
*/
#include "mdk/VideoDecoder.h"
#include "mdk/MediaInfo.h"
#include "mdk/Packet.h"
#include "mdk/VideoFrame.h"
#include "AnnexBFilter.h"
#include "base/ByteArray.h"
#include "base/ms/MFTCodec.h"
#include <codecapi.h>
#include <Mferror.h>
#include <iostream>

MDK_NS_BEGIN
using namespace std;
class MFTVideoDecoder final : public VideoDecoder, protected MFTCodec
{
public:
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
    bool onMFTCreated(ComPtr<IMFTransform> mft) override;
    bool testConstraints(ComPtr<IMFTransform> mft);
    uint8_t* filter(uint8_t* data, size_t* size) override;

    virtual bool setInputTypeAttributes(IMFAttributes* attr) override;
    virtual bool setOutputTypeAttributes(IMFAttributes* attr) override;
    virtual int getInputTypeScore(IMFAttributes* attr) override;
    virtual int getOutputTypeScore(IMFAttributes* attr) override;
    bool onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type) override; // width/height, pixel format, yuv mat, color primary/transfer func/chroma sitting/range, par
    bool onOutput(ComPtr<IMFSample> sample) override;

    const CLSID* codec_id_ = nullptr;
    int copy_ = 0;
    int nal_size_ = 0;
    bool prepend_csd_ = true;
    int csd_size_ = 0;
    uint8_t* csd_ = nullptr;
    ByteArray csd_pkt_;
    VideoFrame frame_param_;
    UINT32 stride_x_ = 0;
    UINT32 stride_y_ = 0;
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
    // http://www.howtobuildsoftware.com/index.php/how-do/9vN/c-windows-ms-media-foundation-mf-doesnt-play-video-from-my-source
    if (!par.extra.empty()) {
        if (strstr(par.codec.data(), "h264")) { // & if avcC?
            csd_ = avcc_to_annexb_extradata(par.extra.data(), par.extra.size(), &csd_size_, &nal_size_);
        } else if (strstr(par.codec.data(), "hevc") || strstr(par.codec.data(), "h265")) {
            csd_ = hvcc_to_annexb_extradata(par.extra.data(), par.extra.size(), &csd_size_, &nal_size_);
        } // TODO: mpeg4?
    }
    if (!openCodec(MediaType::Video, *codec_id_))
        return false;
    std::string prop = property("pool");
    if (!prop.empty())
        useSamplePool(std::atoi(prop.data()));
    prop = property("copy");
    if (!prop.empty())
        copy_ = std::stoi(prop);
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
    ComPtr<IMFAttributes> a;
    MS_ENSURE(mft->GetAttributes(&a), false);
    // d3d: https://docs.microsoft.com/en-us/windows/desktop/medfound/direct3d-aware-mfts

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
        to_annexb_packet(data, *size, nal_size_);
        
    }
    if (!prepend_csd_)
        return nullptr;
    prepend_csd_ = false;
    csd_pkt_.resize(csd_size_ + *size);
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
    Unpack2UINT32AsUINT64(dim, &stride_x_, &stride_y_);
    UINT32 w = stride_x_, h = stride_y_;
    std::clog << "output size: " << w << "x" << h << std::endl;
    MFVideoArea area{}; // desktop only?
    if (SUCCEEDED(a->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), nullptr))) {
        std::clog << "video area: (" << area.OffsetX.value << ", " << area.OffsetY.value << "), " << area.Area.cx << "x" << area.Area.cy << std::endl;
        w = area.Area.cx;
        h = area.Area.cy;
    }
    frame_param_ = VideoFrame(w, h, outfmt);
    // TODO: MF_MT_PIXEL_ASPECT_RATIO, MF_MT_YUV_MATRIX, MF_MT_VIDEO_PRIMARIES, MF_MT_TRANSFER_FUNCTION, MF_MT_VIDEO_CHROMA_SITING, MF_MT_VIDEO_NOMINAL_RANGE
    // MFVideoPrimaries_BT709, MFVideoPrimaries_BT2020
    // MF_MT_TRANSFER_FUNCTION:  MFVideoTransFunc_2020(_const)
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