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
# if _WIN32_WINNT < 0x0602 // MF_MT_AAC_PAYLOAD_TYPE
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0602
# endif
#include "base/ms/MFGlue.h"
#include "base/ByteArrayBuffer.h"
#include "mdk/MediaInfo.h"
#include "mdk/AudioFrame.h"
#include <mfidl.h>
#include <Mferror.h>
//#ifdef _MSC_VER
# pragma pop_macro("_WIN32_WINNT")

MDK_NS_BEGIN
namespace MF {

bool to(AudioFormat::ChannelMap& cm, UINT32 cl)
{
    cm = uint64_t(cl);
    return true;
}

bool to(AudioFormat& af, const IMFAttributes* ca)
{
    auto a = const_cast<IMFAttributes*>(ca);
    UINT32 v = 0;
    if (FAILED(a->GetUINT32(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, &v)) // may be not set (if no padding data)
        && FAILED(a->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &v)))
        return false;
    GUID subtype;
    MS_ENSURE(a->GetGUID(MF_MT_SUBTYPE, &subtype), false);
    bool is_flt = false;
    bool is_uint = false;
    if (subtype == MFAudioFormat_PCM) {
        is_uint = v == 8;
    } else if (subtype == MFAudioFormat_Float) {
        is_flt = true;
    } else { // spdif pass through? not supported yet
        return false;
    }
    af.setSampleFormat(AudioFormat::make(v/8, is_flt, is_uint, false));

    if (SUCCEEDED(a->GetUINT32(MF_MT_AUDIO_CHANNEL_MASK, &v))) {
        AudioFormat::ChannelMap cm;
        if (!to(cm, v))
            return false;
        af.setChannelMap(cm);
    } else {
        MS_ENSURE(a->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &v), false);
        af.setChannels(v);
    }

    MS_ENSURE(a->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &v), false);
    af.setSampleRate(v);
    return true;
}


bool to(AudioFrame& frame, ComPtr<IMFSample> sample, bool copy)
{
    LONGLONG t = 0;
    if (SUCCEEDED(sample->GetSampleTime(&t)))
        frame.setTimestamp(from_mf_time(t));
    DWORD nb_bufs = 0;
    MS_ENSURE(sample->GetBufferCount(&nb_bufs), false);
    const bool contiguous = frame.format().planeCount() > (int)nb_bufs;

    for (DWORD i = 0; i < nb_bufs; ++i) {
        ComPtr<IMFMediaBuffer> buf;
        MS_ENSURE(sample->GetBufferByIndex(i, &buf), false);
        if (copy) {
            BYTE* data = nullptr;
            DWORD len = 0;
            MS_ENSURE(buf->Lock(&data, nullptr, &len), false);
            if (contiguous) {
                const uint8_t* da[] = {data, nullptr, nullptr};
                //frame.setBuffers(da);
            } else {
                frame.addBuffer(std::make_shared<ByteArrayBuffer>(len, data));
            }
            buf->Unlock();
        } else {
            frame.addBuffer(to(buf));
        }
    }
    DWORD bytes = 0;
    MS_ENSURE(sample->GetTotalLength(&bytes), false);
    frame.setSamplesPerChannel(frame.format().framesForBytes(bytes));
    return true;
}

bool from(const AudioFormat& af, IMFAttributes* a)
{
    return false;
}


bool from(const AudioCodecParameters& par, IMFAttributes* a)
{
    MS_ENSURE(a->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), false);
    MS_ENSURE(a->SetGUID(MF_MT_SUBTYPE, *codec_for(par.codec, MediaType::Audio)), false);

    if (par.bit_rate > 0)
        MS_ENSURE(a->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)par.bit_rate), false);

    bool use_extra = !par.extra.empty(); // and if no bsf
    if (par.codec == "aac") { // https://docs.microsoft.com/en-us/windows/desktop/medfound/mf-mt-user-data-attribute
        // https://docs.microsoft.com/en-us/windows/desktop/medfound/aac-decoder#example-media-types
        // The first 12 bytes of MF_MT_USER_DATA: HEAACWAVEINFO.wPayloadType,wAudioProfileLevelIndication,wStructType
        ByteArray extra(int(12 + par.extra.size()), 0); // +2 for last 2 bytes?
        memcpy(extra.data() + 12, par.extra.data(), par.extra.size());
        const UINT32 payload_type = par.extra.empty() ? 1 : 0;
        extra[0] = payload_type; // for HEAACWAVEINFO.wPayloadType
        // https://docs.microsoft.com/en-us/windows/desktop/medfound/mf-mt-aac-payload-type
        MS_ENSURE(a->SetBlob(MF_MT_USER_DATA, extra.data(), extra.size()), false);
        MS_ENSURE(a->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, payload_type), false);
    } else if (use_extra) {
        // check existing MF_MT_USER_DATA?
        MS_ENSURE(a->SetBlob(MF_MT_USER_DATA, par.extra.data(), (UINT32)par.extra.size()), false);
    }

    a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, par.sample_rate);
    a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, par.channels);
    // WAVEFORMATEX stuff; might be required by some codecs.
    if (par.block_align > 0)
        a->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, par.block_align);
    if (par.bit_rate > 0)
        a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(par.bit_rate / 8));
    if (par.bits_per_coded_sample)
        a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, par.bits_per_coded_sample);

    a->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1); // ??
    return true;
}
} // namespace MF
MDK_NS_END