/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "mdk/global.h"
#include <mfapi.h>
#include <mfidl.h>
#include "MSUtils.h"
#include "MFCompat.h"
MDK_NS_BEGIN

class Buffer;
class Buffer2D;
class IOControl;
class MediaIO;
class Packet;
class AudioFormat;
class AudioFrame;
class VideoFormat;
class VideoFrame;
struct VideoCodecParameters;
struct AudioCodecParameters;
enum class PixelFormat;

namespace MF {
// create wrapper buffer from IMF in limited offset and size, like MFCreateMediaBufferWrapper
// offset is of type ptrdiff_t instead of int to avoid ambiguous overload
// size <=0: use the whole size - offset
std::shared_ptr<Buffer> to(ComPtr<IMFMediaBuffer> b, ptrdiff_t offset = 0, int size = -1);
std::shared_ptr<Buffer2D> to(ComPtr<IMF2DBuffer> b, ptrdiff_t offset = 0, int size = -1);

// assum from/to source and target are not the same internal type(mem, mf, av buffer)
// TODO: no mem allocation if alignment matches
ComPtr<IMFMediaBuffer> from(std::shared_ptr<Buffer> buf, int align = 0);
ComPtr<IMF2DBuffer> from(std::shared_ptr<Buffer2D> buf);

static inline LONGLONG to_mf_time(double s)
{
    return LONGLONG(s*100000000.0); // 100ns
}

static inline double from_mf_time(LONGLONG ns_100)
{
    return ns_100/100000000.0;
}

ComPtr<IMFSample> from(const Packet& pkt, int align = 0);
void to(Packet& pkt, ComPtr<IMFSample> mfpkt);

const CLSID* codec_for(const std::string& name, MediaType type);

//AudioCodecParameters
bool from(const AudioCodecParameters& par, IMFAttributes* a);

bool to(AudioFormat& fmt, const IMFAttributes* a);
bool from(const AudioFormat& fmt, IMFAttributes* a);

bool to(AudioFrame& frame, ComPtr<IMFSample> sample, bool copy = false);
bool from(const AudioFrame& frame, ComPtr<IMFSample> sample);

bool from(const VideoCodecParameters& par, IMFAttributes* a);

bool to(VideoFormat& fmt, const IMFAttributes* a);
bool from(const VideoFormat& fmt, IMFAttributes* a);

// frame format and size are not touched(no corresponding attributes in sample)
// \param copy
// 0: no copy if possible, i.e. hold d3d surface for d3d buffer, or add ref to sample buffers and lock to access for software decoder sample buffers(no d3d).
// 1: lock sample buffer for d3d, and also copy data to frame planes for software decoder sample buffers
// 2: lock sample buffer copy data to frame planes for d3d
bool to(VideoFrame& frame, ComPtr<IMFSample> sample, int strideX = 0, int strideY = 0, int copy = 0);
bool from(const VideoFrame& frame, ComPtr<IMFSample> sample);
/*
bool to(AudioFrame& frame, ComPtr<IMFMediaType> mfmt);
AVFrame* from(const AudioFrame& frame);
bool to(VideoFormat& vf, AVPixelFormat fmt);
AVPixelFormat from(const VideoFormat& vf);
bool to(VideoFrame& frame, const AVFrame* avframe);
AVFrame* from(const VideoFrame& frame);
*/
std::string to_string(const GUID& id); // uuid string, e.g. 8D2FD10B-5841-4a6b-8905-588FEC1ADED9
std::string to_name(const GUID& id);
void dump(IMFAttributes* a);
} // namespace MF
MDK_NS_END