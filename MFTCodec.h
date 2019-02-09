/*
 * Copyright (c) 2018-2019 WangBin <wbsecg1 at gmail.com>
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
class MFTCodec
{
protected:
    MFTCodec();
    virtual ~MFTCodec();
    // enable (unbounded) sample pool. Use a pool to reduce sample and buffer allocation frequency because a buffer(video) can be in large size.
    void useSamplePool(bool value = true) { use_pool_ = value; } // setPoolSamples(int size = -1)
    void activateAt(int value);
    bool openCodec(MediaType type, const CLSID& codec_id);
    bool closeCodec();
    bool flushCodec();
    bool decodePacket(const Packet& pkt);
private:
    bool createMFT(MediaType type, const CLSID& codec_id);
    virtual bool onMFTCreated(ComPtr<IMFTransform> /*mft*/) {return true;}
    bool destroyMFT();
    bool setMediaTypes();
    // bitstream/packet filter
    // return nullptr if filter in place, otherwise allocated data is returned and size is modified. no need to free the data
    virtual uint8_t* filter(uint8_t* /*data*/, size_t* /*size*/) {return nullptr;}
    virtual bool setInputTypeAttributes(IMFAttributes* attr) {return true;}
    virtual bool setOutputTypeAttributes(IMFAttributes* attr) {return true;}
    virtual int getInputTypeScore(IMFAttributes* attr) {return -1;}
    virtual int getOutputTypeScore(IMFAttributes* attr) {return -1;}
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
    int activate_index_ = -1;
    DWORD id_in_ = 0;
    DWORD id_out_ = 0;
    MFT_INPUT_STREAM_INFO info_in_;
    MFT_OUTPUT_STREAM_INFO info_out_;

    using SamplePoolRef = std::shared_ptr<SamplePool>;
    ComPtr<IMFAsyncCallback> pool_cb_; // double-pool for stream(parameter) change to clear samples outside the pool?
};
MDK_NS_END