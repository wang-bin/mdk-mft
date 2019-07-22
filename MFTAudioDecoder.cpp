
/*
 * Copyright (c) 2018-2019 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
//#ifdef _MSC_VER
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0602 // MFT_ENUM_HARDWARE_VENDOR_ID_Attribute
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0602
# endif
#include "mdk/AudioDecoder.h"
#include "mdk/MediaInfo.h"
#include "mdk/Packet.h"
#include "mdk/AudioFrame.h"
#include "base/ms/MFTCodec.h"
#include <codecapi.h>
#include <Mferror.h>
#include <iostream>
//#ifdef _MSC_VER
# pragma pop_macro("_WIN32_WINNT")
// properties: pool=1(0, 1), copy=0(0, 1, 2), in_type=index(or -1), out_type=index(or -1)

MDK_NS_BEGIN
using namespace std;
class MFTAudioDecoder final : public AudioDecoder, protected MFTCodec
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
    void onPropertyChanged(const std::string& key, const std::string& value) override {
        if (key == "copy")
            copy_ = std::stoi(value);
        else if (key == "pool")
            useSamplePool(std::stoi(value));
    }

    bool onMFTCreated(ComPtr<IMFTransform> mft) override;
    virtual bool setInputTypeAttributes(IMFAttributes* attr) override;
    virtual bool setOutputTypeAttributes(IMFAttributes* attr) override;
    virtual int getInputTypeScore(IMFAttributes* attr) override;
    virtual int getOutputTypeScore(IMFAttributes* attr) override;
    bool onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type) override; // width/height, pixel format, yuv mat, color primary/transfer func/chroma sitting/range, par
    bool onOutput(ComPtr<IMFSample> sample) override;

    const CLSID* codec_id_ = nullptr;
    AudioFormat outfmt_;
    bool copy_ = false;
};

bool MFTAudioDecoder::open()
{
    useSamplePool(std::stoi(property("pool", "1")));
    copy_ = std::stoi(property("copy", "0"));
    setInputTypeIndex(std::stoi(property("in_type", "-1")));
    setOutputTypeIndex(std::stoi(property("out_type", "-1")));

    const auto& par = parameters();
    codec_id_ = MF::codec_for(par.codec, MediaType::Audio);
    if (!codec_id_) {
        std::clog << "codec is not supported: " << par.codec << std::endl;
        return false;
    }
    if (!openCodec(MediaType::Audio, *codec_id_))
        return false;
    std::clog << this << "MFT decoder is ready" << std::endl;
    onOpen();
    return true;
}

bool MFTAudioDecoder::close()
{
    bool ret = closeCodec(); 
    onClose();
    return ret;
}

bool MFTAudioDecoder::onMFTCreated(ComPtr<IMFTransform> mft)
{
    ComPtr<IMFAttributes> a;
    MS_ENSURE(mft->GetAttributes(&a), true); // not implemented

    wchar_t vendor[128]{}; // set vendor id?
    if (SUCCEEDED(a->GetString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, vendor, sizeof(vendor), nullptr))) // win8+, so warn only
        printf("hw vendor id: %ls\n", vendor);
    if (SUCCEEDED(a->GetString(MFT_ENUM_HARDWARE_URL_Attribute, vendor, sizeof(vendor), nullptr))) // win8+, so warn only
        printf("hw url: %ls\n", vendor);
    return true;
}

bool MFTAudioDecoder::setInputTypeAttributes(IMFAttributes* a)
{
    return MF::from(parameters(), a);
}

bool MFTAudioDecoder::setOutputTypeAttributes(IMFAttributes* attr)
{
    return true;
}

int MFTAudioDecoder::getInputTypeScore(IMFAttributes* attr)
{
    GUID id;
    MS_ENSURE(attr->GetGUID(MF_MT_SUBTYPE, &id), -1);
    if (id != *codec_id_) // TODO: always same id because mft is activated from same codec id? aac can be aac or adts
        return -1;
    return 1;
}

int MFTAudioDecoder::getOutputTypeScore(IMFAttributes* attr)
{
    GUID subtype;
    MS_ENSURE(attr->GetGUID(MF_MT_SUBTYPE, &subtype), -1);
    AudioFormat fmt;
    if (!MF::to(fmt, attr)) // TODO: closest channels, depth as option/property? e.g. dolby
        return -1;
    return 0;
}

bool MFTAudioDecoder::onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type)
{
    ComPtr<IMFAttributes> a;
    MS_ENSURE(type.As(&a), false);
    AudioFormat outfmt;
    if (!MF::to(outfmt, a.Get()))
        return false;
    std::clog << "output format: " << outfmt << std::endl;
    outfmt_ = outfmt;
    return true;
}

bool MFTAudioDecoder::onOutput(ComPtr<IMFSample> sample)
{
    AudioFrame frame(outfmt_);
    if (!MF::to(frame, sample, copy_))
        return false;
    frameDecoded(frame);
    return true;
}

void register_audio_decoders_mf() {
    AudioDecoder::registerOnce("MFT", []{return new MFTAudioDecoder();});
}
namespace { // DCE
static const struct register_at_load_time_if_no_dce {
    inline register_at_load_time_if_no_dce() { register_audio_decoders_mf();}
} s;
}
MDK_NS_END