/*
 * Copyright (c) 2018-2022 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "mdk/Packet.h"
#include "MFGlue.h"
#include <iostream>

MDK_NS_BEGIN
class MDK_NOVTBL MFTCodec
{
protected:
    MFTCodec();
    virtual ~MFTCodec();
    // enable (unbounded) sample pool. Use a pool to reduce sample and buffer allocation frequency because a buffer(video) can be in large size.
    void useSamplePool(bool value = true) { use_pool_ = value; } // setPoolSamples(int size = -1)
    void activateAt(int value) { activate_index_ = value; }
    bool openCodec(MediaType type, const CLSID& codec_id, const Property* prop);
    bool closeCodec();
    bool flushCodec();
    bool decodePacket(const Packet& pkt);
    void setInputTypeIndex(int index = -1) {
        in_type_idx_ = index;
    }
    void setOutputTypeIndex(int index = -1) {
        out_type_idx_ = index;
    }
private:
    bool createMFT(MediaType type, const CLSID& codec_id);
    virtual bool onMFTCreated(ComPtr<IMFTransform> /*mft*/) {return true;}
    bool destroyMFT();
    bool setMediaTypes();
    // bitstream/packet filter
    // return nullptr if filter in place, otherwise allocated data is returned and size is modified. no need to free the data
    virtual BufferRef filter(BufferRef in) {return in;}
    virtual bool setInputTypeAttributes(IMFAttributes*) {return true;}
    virtual bool setOutputTypeAttributes(IMFAttributes*) {return true;}
    virtual int getInputTypeScore(IMFAttributes*) {return -1;}
    virtual int getOutputTypeScore(IMFAttributes*) {return -1;}
    virtual bool onOutputTypeChanged(DWORD streamId, ComPtr<IMFMediaType> type) = 0;
    virtual bool onOutput(ComPtr<IMFSample> sample) = 0;
    ComPtr<IMFSample> getOutSample(); // get an output sample from pool, or create directly.
    // processInput(data, size);
    bool processOutput();
    virtual ComPtr<IMFMediaType> selectInputType(DWORD stream_id, bool* later);
    virtual ComPtr<IMFMediaType> selectOutputType(DWORD stream_id, bool* later);
protected:
    ComPtr<IMFTransform> mft_;
private:
    class SamplePool;
    SamplePool* getPool() {return reinterpret_cast<SamplePool*>(pool_cb_.Get());}

    bool uninit_com_ = false;
    bool use_pool_ = true;
    bool discontinuity_ = false;
    bool warn_not_tracked_ = true;
    int activate_index_ = -1;
    DWORD id_in_ = 0;
    DWORD id_out_ = 0;
    MFT_INPUT_STREAM_INFO info_in_;
    MFT_OUTPUT_STREAM_INFO info_out_;
    int in_type_idx_ = -1;
    int out_type_idx_ = -1;

    using SamplePoolRef = std::shared_ptr<SamplePool>;
    ComPtr<IMFAsyncCallback> pool_cb_; // double-pool for stream(parameter) change to clear samples outside the pool?
};
MDK_NS_END
