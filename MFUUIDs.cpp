/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
// https://docs.microsoft.com/zh-cn/windows/uwp/audio-video-camera/supported-codecs
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0A00 // _WIN32_WINNT_WIN10 is not defined yet
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0A00
# endif
#include "MFGlue.h"
#include <unordered_map>
#include <codecapi.h>
#include <Mferror.h>
#include <Mftransform.h> // MFT_FRIENDLY_NAME_Attribute
// #include <rpc.h> //UuidToString, UuidHash
# pragma pop_macro("_WIN32_WINNT")

namespace std
{
    template<>
    struct hash<UUID>
    {
        std::size_t operator()(const UUID& value) const noexcept {
            //unsigned short UuidHash(UUID *Uuid, RPC_STATUS *Status);
            return std::hash<std::string>{}(MDK_NS::MF::to_string(value)); // to_stringview?
        }
    };
}

MDK_NS_BEGIN
namespace MF {
// StringFromCLSID: wstring, upper case, with {} around. RpcStringFreeA is not declared for uwp?
std::string to_string(const GUID& id)
{
    char v[64]{};
    snprintf(&v[0], sizeof(v), "{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}"
              , (unsigned)id.Data1, id.Data2, id.Data3
              , id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    return v;
}

#define GUID_KV(var) {var, # var}

static const std::unordered_map<UUID,const char*> kIdNameMap{
    GUID_KV(MFT_FRIENDLY_NAME_Attribute),
    GUID_KV(MF_TRANSFORM_CATEGORY_Attribute),
    GUID_KV(MF_TRANSFORM_FLAGS_Attribute),
    GUID_KV(MF_TRANSFORM_ASYNC),
    GUID_KV(MF_TRANSFORM_ASYNC_UNLOCK),
    GUID_KV(MFT_INPUT_TYPES_Attributes),
    GUID_KV(MFT_OUTPUT_TYPES_Attributes),
    GUID_KV(MFT_FIELDOFUSE_UNLOCK_Attribute),
    GUID_KV(MFT_CODEC_MERIT_Attribute),
    GUID_KV(MFT_CATEGORY_AUDIO_DECODER),
    GUID_KV(MFT_CATEGORY_AUDIO_EFFECT),
    GUID_KV(MFT_CATEGORY_AUDIO_ENCODER),
    GUID_KV(MFT_CATEGORY_VIDEO_DECODER),
    GUID_KV(MFT_CATEGORY_VIDEO_EFFECT),
    GUID_KV(MFT_CATEGORY_VIDEO_ENCODER),
    GUID_KV(MFT_CATEGORY_VIDEO_PROCESSOR),
    GUID_KV(MFT_TRANSFORM_CLSID_Attribute),
    GUID_KV(MFT_ENUM_HARDWARE_URL_Attribute),
    GUID_KV(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute),
    GUID_KV(MFT_CONNECTED_STREAM_ATTRIBUTE),
    GUID_KV(MFT_CONNECTED_TO_HW_STREAM),
    GUID_KV(MF_SA_D3D_AWARE),
    GUID_KV(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT),
    GUID_KV(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE),
    GUID_KV(MF_SA_D3D11_BINDFLAGS),
    GUID_KV(MF_SA_D3D11_USAGE),
    GUID_KV(MF_SA_D3D11_AWARE),
    GUID_KV(MF_SA_D3D11_SHARED),
    GUID_KV(MF_SA_D3D11_SHARED_WITHOUT_MUTEX),
    GUID_KV(MF_MT_SUBTYPE),
    GUID_KV(MF_MT_MAJOR_TYPE),
    GUID_KV(MF_MT_AUDIO_SAMPLES_PER_SECOND),
    GUID_KV(MF_MT_AUDIO_NUM_CHANNELS),
    GUID_KV(MF_MT_AUDIO_CHANNEL_MASK),
    GUID_KV(MF_MT_FRAME_SIZE),
    GUID_KV(MF_MT_INTERLACE_MODE),
    GUID_KV(MF_MT_USER_DATA),
    GUID_KV(MF_MT_PIXEL_ASPECT_RATIO),
    // MFT_TRANSFORM_CLSID_Attribute, mfidl.h, win10
    GUID_KV(CLSID_MSH264DecoderMFT),
    GUID_KV(CLSID_MSH264EncoderMFT),
    GUID_KV(CLSID_MSDDPlusDecMFT),
    GUID_KV(CLSID_MP3DecMediaObject),
    GUID_KV(CLSID_MSAACDecMFT),
    GUID_KV(CLSID_MSH265DecoderMFT),
    GUID_KV(CLSID_WMVDecoderMFT),
    GUID_KV(CLSID_WMADecMediaObject),
    GUID_KV(CLSID_MSMPEGAudDecMFT),
    GUID_KV(CLSID_MSMPEGDecoderMFT),
    GUID_KV(CLSID_AudioResamplerMediaObject),
    GUID_KV(CLSID_MSVPxDecoder),
    GUID_KV(CLSID_MSOpusDecoder),
    GUID_KV(CLSID_VideoProcessorMFT),
    GUID_KV(MFMediaType_Audio),
    GUID_KV(MFMediaType_Video),
    GUID_KV(MFAudioFormat_PCM),
    GUID_KV(MFAudioFormat_Float),
    GUID_KV(MFVideoFormat_H264),
    GUID_KV(MFVideoFormat_H264_ES),
    GUID_KV(MFVideoFormat_H265),
    GUID_KV(MFVideoFormat_HEVC),
    GUID_KV(MFVideoFormat_HEVC_ES),
    GUID_KV(MFVideoFormat_MPEG2),
    GUID_KV(MFVideoFormat_MP43),
    GUID_KV(MFVideoFormat_MP4V),
    GUID_KV(MFVideoFormat_VP80),
    GUID_KV(MFVideoFormat_VP90),
    GUID_KV(MFVideoFormat_WMV1),
    GUID_KV(MFVideoFormat_WMV2),
    GUID_KV(MFVideoFormat_WMV3),
    GUID_KV(MFVideoFormat_WVC1),
    GUID_KV(MFAudioFormat_Dolby_AC3),
    GUID_KV(MFAudioFormat_Dolby_DDPlus),
    GUID_KV(MFAudioFormat_AAC),
    GUID_KV(MFAudioFormat_MP3),
    GUID_KV(MFAudioFormat_MSP1),
    //GUID_KV(MFAudioFormat_MSAUDIO1),
    GUID_KV(MFAudioFormat_FLAC),
    GUID_KV(MFAudioFormat_Opus),
    GUID_KV(MFAudioFormat_Vorbis),
    GUID_KV(MFAudioFormat_WMAudioV8),
    GUID_KV(MFAudioFormat_WMAudioV9),
    GUID_KV(MFAudioFormat_WMAudio_Lossless),
    GUID_KV(MF_MT_ALL_SAMPLES_INDEPENDENT),
    GUID_KV(MF_MT_COMPRESSED),
    GUID_KV(MF_MT_FIXED_SIZE_SAMPLES),
    GUID_KV(MF_MT_SAMPLE_SIZE),
    GUID_KV(MF_MT_WRAPPED_TYPE),
    GUID_KV(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION),
    GUID_KV(MF_MT_AAC_PAYLOAD_TYPE),
    GUID_KV(MF_MT_AUDIO_AVG_BYTES_PER_SECOND),
    GUID_KV(MF_MT_AUDIO_BITS_PER_SAMPLE),
    GUID_KV(MF_MT_AUDIO_BLOCK_ALIGNMENT),
    GUID_KV(MF_MT_AUDIO_CHANNEL_MASK),
    GUID_KV(MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND),
    GUID_KV(MF_MT_AUDIO_FOLDDOWN_MATRIX),
    GUID_KV(MF_MT_AUDIO_NUM_CHANNELS),
    GUID_KV(MF_MT_AUDIO_PREFER_WAVEFORMATEX),
    GUID_KV(MF_MT_AUDIO_SAMPLES_PER_BLOCK),
    GUID_KV(MF_MT_AUDIO_SAMPLES_PER_SECOND),
    GUID_KV(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE),
    GUID_KV(MF_MT_AUDIO_WMADRC_AVGREF),
    GUID_KV(MF_MT_AUDIO_WMADRC_AVGTARGET),
    GUID_KV(MF_MT_AUDIO_WMADRC_PEAKREF),
    GUID_KV(MF_MT_AUDIO_WMADRC_PEAKTARGET),
    GUID_KV(MF_MT_AVG_BIT_ERROR_RATE),
    GUID_KV(MF_MT_AVG_BITRATE),
    //GUID_KV(MF_MT_CUSTOM_VIDEO_PRIMARIES), // desktop
    GUID_KV(MF_MT_DEFAULT_STRIDE),
    GUID_KV(MF_MT_DRM_FLAGS),
    GUID_KV(MF_MT_FRAME_RATE),
    GUID_KV(MF_MT_FRAME_RATE_RANGE_MAX),
    GUID_KV(MF_MT_FRAME_RATE_RANGE_MIN),
    GUID_KV(MF_MT_FRAME_SIZE),
    GUID_KV(MF_MT_GEOMETRIC_APERTURE),
    GUID_KV(MF_MT_INTERLACE_MODE),
    GUID_KV(MF_MT_MAX_KEYFRAME_SPACING),
    GUID_KV(MF_MT_MINIMUM_DISPLAY_APERTURE),
    GUID_KV(MF_MT_MPEG_SEQUENCE_HEADER),
    GUID_KV(MF_MT_MPEG_START_TIME_CODE),
    GUID_KV(MF_MT_MPEG2_FLAGS),
    GUID_KV(MF_MT_MPEG2_LEVEL),
    GUID_KV(MF_MT_MPEG2_PROFILE),
    GUID_KV(MF_MT_PAD_CONTROL_FLAGS),
    GUID_KV(MF_MT_PALETTE),
    GUID_KV(MF_MT_PAN_SCAN_APERTURE),
    GUID_KV(MF_MT_PAN_SCAN_ENABLED),
    GUID_KV(MF_MT_PIXEL_ASPECT_RATIO),
    GUID_KV(MF_MT_SOURCE_CONTENT_HINT),
    GUID_KV(MF_MT_TRANSFER_FUNCTION),
    GUID_KV(MF_MT_VIDEO_CHROMA_SITING),
    GUID_KV(MF_MT_VIDEO_LIGHTING),
    GUID_KV(MF_MT_VIDEO_NOMINAL_RANGE),
    GUID_KV(MF_MT_VIDEO_PRIMARIES),
    GUID_KV(MF_MT_VIDEO_ROTATION),
    GUID_KV(MF_MT_YUV_MATRIX),
    GUID_KV(MFVideoFormat_Base),
    GUID_KV(MFVideoFormat_YUY2),
    GUID_KV(MFVideoFormat_YVYU),
    GUID_KV(MFVideoFormat_UYVY),
    GUID_KV(MFVideoFormat_NV12),
    GUID_KV(MFVideoFormat_YV12),
    GUID_KV(MFVideoFormat_I420),
    GUID_KV(MFVideoFormat_IYUV),
    GUID_KV(MFVideoFormat_P210),
    GUID_KV(MFVideoFormat_P216),
    GUID_KV(MFVideoFormat_P010),
    GUID_KV(MFVideoFormat_P016),
    GUID_KV(MFVideoFormat_RGB32),
    GUID_KV(MFVideoFormat_ARGB32),
    GUID_KV(MFVideoFormat_RGB24),
    GUID_KV(CODECAPI_AVDecVideoThumbnailGenerationMode),
    GUID_KV(CODECAPI_AVDecVideoDropPicWithMissingRef),
    GUID_KV(CODECAPI_AVDecVideoSoftwareDeinterlaceMode),
    GUID_KV(CODECAPI_AVDecVideoFastDecodeMode),
    GUID_KV(CODECAPI_AVLowLatencyMode),
    GUID_KV(CODECAPI_AVDecVideoH264ErrorConcealment),
    GUID_KV(CODECAPI_AVDecVideoMPEG2ErrorConcealment),
    GUID_KV(CODECAPI_AVDecVideoCodecType),
    GUID_KV(CODECAPI_AVDecVideoDXVAMode),
    GUID_KV(CODECAPI_AVDecVideoDXVABusEncryption),
    GUID_KV(CODECAPI_AVDecVideoSWPowerLevel),
    GUID_KV(CODECAPI_AVDecVideoMaxCodedWidth),
    GUID_KV(CODECAPI_AVDecVideoMaxCodedHeight),
    GUID_KV(CODECAPI_AVDecNumWorkerThreads),
    GUID_KV(CODECAPI_AVDecSoftwareDynamicFormatChange),
    GUID_KV(CODECAPI_AVDecDisableVideoPostProcessing),
    GUID_KV(CODECAPI_AVDecVideoAcceleration_H264),
    GUID_KV(CODECAPI_AVDecVideoAcceleration_MPEG2),
    GUID_KV(CODECAPI_AVDecVideoAcceleration_VC1),
    GUID_KV(MFT_DECODER_EXPOSE_OUTPUT_TYPES_IN_NATIVE_ORDER),
    GUID_KV(MFSampleExtension_CleanPoint),
};

std::string to_name(const GUID& id)
{
    const auto i = kIdNameMap.find(id);
    if (i == kIdNameMap.cend())
        return to_string(id);
    return i->second;
}
} // namespace MF
MDK_NS_END