/*
 * Copyright (c) 2018-2019 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
// https://docs.microsoft.com/zh-cn/windows/uwp/audio-video-camera/supported-codecs
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0601 // _WIN32_WINNT_WIN7 is not defined yet
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0601
# endif
#include "MFTCodec.h"
#include "base/scope_atexit.h"
#include "base/ByteArrayBuffer.h"
#include "base/fmt.h"
#include "base/mpsc_fifo.h"
#include "cppcompat/type_traits.hpp"
#include <algorithm>
#include <codecapi.h>
#include <Mferror.h>
#include <Mftransform.h> // MFT_FRIENDLY_NAME_Attribute
# pragma pop_macro("_WIN32_WINNT")

MDK_NS_BEGIN
#if (_MSC_VER + 0) // RuntimeClass is missing in mingw
// used by SetAllocator, pool ref must be added in Tracked sample, so make it as IUnknown
class MFTCodec::SamplePool : public mpsc_fifo<ComPtr<IMFTrackedSample>>, public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFAsyncCallback>  // IUnknown is implemented by RuntimeClass
{
public:
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD *pdwFlags, DWORD *pdwQueue) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult *pAsyncResult) override {
        IMFTrackedSample* s = nullptr;
        HRESULT hr = S_OK;
        MS_ENSURE((hr = pAsyncResult->GetState((IUnknown**)&s)), hr);
        push(std::move(s));
        return hr;
    }
};
#endif
MFTCodec::MFTCodec()
{
#if (_MSC_VER + 0)
    pool_cb_ = Make<SamplePool>();
#endif
}

MFTCodec::~MFTCodec() = default;

bool MFTCodec::openCodec(MediaType mt, const CLSID& codec_id)
{
    if (!createMFT(mt, codec_id))
        return false;
    // TODO: unlock async
    // TODO: thread property. codec_api_->SetValue(CODECAPI_AVDecNumWorkerThreads,)

    DWORD nb_in = 0, nb_out = 0;
    MS_ENSURE(mft_->GetStreamCount(&nb_in, &nb_out), false);
    std::clog << "stream cout: in=" << nb_in << ", out=" << nb_out << std::endl;
    auto hr = mft_->GetStreamIDs(1, &id_in_, 1, &id_out_);
    if (hr == E_NOTIMPL) {// stream number is fixed and 0~n-1
        id_in_ = id_out_ = 0;
    } else if (FAILED(hr)) {
        std::clog << "failed to get stream ids" << std::endl;
        return false;
    }
    if (!setMediaTypes())
        return false;
    MS_ENSURE(mft_->GetInputStreamInfo(id_in_, &info_in_), false);
    std::clog << "input stream info: dwFlags=" << info_in_.dwFlags << ", cbSize=" << info_in_.cbSize << ", cbAlignment=" << info_in_.cbAlignment << ", hnsMaxLatency=" << info_in_.hnsMaxLatency << ", cbMaxLookahead=" << info_in_.cbMaxLookahead << std::endl;
    MS_ENSURE(mft_->GetOutputStreamInfo(id_out_, &info_out_), false);
    std::clog << "output stream info: dwFlags=" << info_out_.dwFlags << ", cbSize=" << info_out_.cbSize << ", cbAlignment=" << info_out_.cbAlignment << std::endl;
    ComPtr<IMFMediaType> type;
    MS_ENSURE(mft_->GetOutputCurrentType(id_out_, &type), false);
    if (!onOutputTypeChanged(id_out_, type))
        return false;
    // TODO: apply extra data here?
    MS_ENSURE(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, ULONG_PTR()), false); // optional(After setting all media types, before ProcessInput). allocate resources(in the 1st ProcessInput if not sent).
    MS_ENSURE(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, ULONG_PTR()), false); // required by async. start to process inputs
    return true;
}

bool MFTCodec::closeCodec()
{
    destroyMFT();
    return true;
}

bool MFTCodec::createMFT(MediaType mt, const CLSID& codec_id)
{
    uninit_com_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED) != RPC_E_CHANGED_MODE; // TODO: class
    std::clog << "uninit com required for MFT: " << uninit_com_ << std::endl;
    MS_ENSURE(MFStartup(MF_VERSION), false);
    static const CLSID kMajorType[] = {
        MFMediaType_Video,
        MFMediaType_Audio,
    };
    MFT_REGISTER_TYPE_INFO reg{kMajorType[std::underlying_type_t<MediaType>(mt)], codec_id};
    UINT32 flags = 0;//
    //            MFT_ENUM_FLAG_HARDWARE | // MUST be async. intel mjpeg decoder
    //             MFT_ENUM_FLAG_SYNCMFT  |
    //             MFT_ENUM_FLAG_LOCALMFT |
    //             MFT_ENUM_FLAG_SORTANDFILTER; // TODO: vlc flags. default 0: MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER
    // MFT_ENUM_FLAG_HARDWARE implies MFT_ENUM_FLAG_ASYNCMFT. usually with MFT_ENUM_FLAG_TRANSCODE_ONLY, and GetStreamIDs error
    IMFActivate **activates = nullptr;
    UINT32 nb_activates = 0;
    CLSID *pCLSIDs = nullptr; // for MFTEnum() <win7
    auto activates_deleter = scope_atexit([&]{
        if (activates) {
            for (UINT32 i = 0; i < nb_activates; ++i) {
                MS_WARN(activates[i]->ShutdownObject()); // required by some. no effect if not
                activates[i]->Release();
            }
        }
        CoTaskMemFree(activates);
        CoTaskMemFree(pCLSIDs);
    });
    const GUID kCategory[] {
        MFT_CATEGORY_VIDEO_DECODER,
        MFT_CATEGORY_AUDIO_DECODER,
        MFT_CATEGORY_VIDEO_ENCODER,
        MFT_CATEGORY_AUDIO_ENCODER,
        MFT_CATEGORY_VIDEO_EFFECT,
        MFT_CATEGORY_AUDIO_EFFECT,
    };
    const auto category = kCategory[std::underlying_type_t<MediaType>(mt)];
    // optional KSCATEGORY_DATADECOMPRESSOR for hw
#if !(MS_WINRT+0)
// TODO: MFTGetInfo() to get in/out info
    typedef HRESULT (STDAPICALLTYPE *MFTEnumEx_fn)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO*, const MFT_REGISTER_TYPE_INFO*, IMFActivate***, UINT32*);
    MFTEnumEx_fn MFTEnumEx = nullptr;
    HMODULE mfplat_dll = GetModuleHandleW(L"mfplat.dll");
    if (mfplat_dll)
        MFTEnumEx = (MFTEnumEx_fn)GetProcAddress(mfplat_dll, "MFTEnumEx");
    if (!MFTEnumEx) {// vista
        MS_ENSURE(MFTEnum(category, flags, &reg, nullptr, nullptr, &pCLSIDs, &nb_activates), false);
        std::clog << nb_activates << " MFT class ids found." << std::endl;
    } else
#endif
    {
        MS_ENSURE(MFTEnumEx(category, flags, &reg, nullptr, &activates, &nb_activates), false);
        std::clog << nb_activates << " MFT class activates found" << std::endl;

    }
    if (nb_activates == 0)
        return false;
    for (int i = 0; i < (int)nb_activates; ++i) {
        if (i != activate_index_ && activate_index_ >= 0)
            continue;
        if (i > activate_index_ && activate_index_ >= 0)
            break;
        if (activates) {
            ComPtr<IMFAttributes> aa;
            ComPtr<IMFActivate> act(activates[i]);
            MS_ENSURE(act.As(&aa), false);
            std::clog << "IMFActivate[" << i << "] attributes:" << std::endl;
            MF::dump(aa.Get());
            wchar_t name[512]{};
            if (SUCCEEDED(activates[i]->GetString(MFT_FRIENDLY_NAME_Attribute, name, sizeof(name), nullptr))) // win7 attribute
                std::clog << fmt::to_string("Activating IMFActivate: %ls", name) << std::endl;
            MS_WARN(activates[i]->ActivateObject(IID_IMFTransform, &mft_)); // __uuidof(IMFTransform), IID_PPV_ARGS(&mft)
        } else {
#if !(MS_WINRT+0)
            MS_WARN(CoCreateInstance(pCLSIDs[i], nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft_)));
#endif
        }
        if (!mft_.Get())
        {
            ComPtr<IMFAttributes> attr;
            if (SUCCEEDED(mft_->GetAttributes(&attr))) {
                UINT32 bAsync = 0; // MFGetAttributeUINT32
                if (SUCCEEDED(attr->GetUINT32(MF_TRANSFORM_ASYNC, &bAsync)) && bAsync) { // only iff MFT_ENUM_FLAG_HARDWARE is explicitly set
                    std::clog << "Async mft is not supported yet" << std::endl;
                    continue;
                }
            }
        }

        if (onMFTCreated(mft_))
            break;
        mft_.Reset();
        break;
    }
    // https://docs.microsoft.com/zh-cn/windows/desktop/medfound/supporting-direct3d-11-video-decoding-in-media-foundation#allocating-uncompressed-buffers
    if (mft_) {
        ComPtr<IMFAttributes> attr;
        if (SUCCEEDED(mft_->GetAttributes(&attr))) {
            // TODO: what about using eventgenerator anyway
            // async requires: IMFMediaEventGenerator, IMFShutdown
            //MS_WARN(attr->SetUINT32(MF_TRANSFORM_ASYNC, TRUE));
            //MS_WARN(attr->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
            // TODO: MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE must be true for async
            std::clog << "Selected MFT attributes:" << std::endl;
            MF::dump(attr.Get());
        }
    }
    return !!mft_.Get();
}

bool MFTCodec::destroyMFT()
{
#if (_MSC_VER + 0) // missing in mingw
    ComPtr<IMFShutdown> shutdown;
    if (mft_ && SUCCEEDED(mft_.As(&shutdown))) // async MFT only
        MS_WARN(shutdown->Shutdown());
#endif // (_MSC_VER + 0)
// TODO: affect other mft/com components? shared?
    mft_.Reset(); // reset before shutdown. otherwise crash
    MS_WARN(MFShutdown());
    if (uninit_com_)
        CoUninitialize();
    return true;
}

using GetAvailableType = std::function<HRESULT(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType **ppType)>;
using GetScore = std::function<int(IMFAttributes*)>; // return 0(the same value) to get the 1st one, for MFT_DECODER_EXPOSE_OUTPUT_TYPES_IN_NATIVE_ORDER
// for in/out audio/video dec/enc
ComPtr<IMFMediaType> SelectType(DWORD stream_id, GetAvailableType getAvail, GetScore getScore, int idx, bool* later)
{
    *later = false;
    ComPtr<IMFMediaType> type;
    ComPtr<IMFMediaType> tmp;
    int index = -1;
    int score = -1;
    HRESULT hr = S_OK;
    for (int i = 0; ; i++) {
        MS_WARN((hr = getAvail(stream_id, i, &tmp)));
        if (hr == MF_E_NO_MORE_TYPES)
            return type;
        if (hr == E_NOTIMPL) // An MFT is not required to implement GetInputAvailableType (simple type)
            return nullptr;
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            *later = true;
            return nullptr; // ?
        }
        if (FAILED(hr)) // skip MFCreateMediaType?
            return nullptr;
        ComPtr<IMFAttributes> attr;
        MS_ENSURE(tmp.As(&attr), nullptr);
        MF::dump(attr.Get());
        int new_score = getScore(attr.Get());
        if (idx >= 0) {
            if (idx != i)
                continue;
        } else {
            if (new_score <= score)
                continue;
        }
        score = new_score;
        type = tmp;
        index = i;
    }
    std::clog << "selected IMediaType index: " << index << std::endl;
    return type;
}

ComPtr<IMFMediaType> MFTCodec::selectInputType(DWORD stream_id, bool* later)
{
    std::clog << __FUNCTION__ << std::endl;
    auto type = SelectType(stream_id, [this](DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType **ppType){
                                        return mft_->GetInputAvailableType(dwOutputStreamID, dwTypeIndex, ppType);
                                    }, [this](IMFAttributes* a){
                                        return getInputTypeScore(a);
                                    }, in_type_idx_, later); // optional
    if (*later) {
        std::clog << "at least 1 output type must be set first" << std::endl;
        return nullptr;
    }
    if (!type) {
        std::clog << "GetInputAvailableType is not implemented or failed, try to create IMediaType manually" << std::endl;
        MS_ENSURE(MFCreateMediaType(&type), nullptr);
    }
    ComPtr<IMFAttributes> a;
    MS_ENSURE(type.As(&a), nullptr);
    if (!setInputTypeAttributes(a.Get()))
        return nullptr;
    std::clog << "SetInputType:" << std::endl;
    MS_ENSURE(type.As(&a), nullptr);
    MF::dump(a.Get());
    DWORD flags = 0; // MFT_SET_TYPE_TEST_ONLY
    HRESULT hr = S_OK;
    MS_WARN((hr = mft_->SetInputType(stream_id, type.Get(), flags))); // set null to clear
    // TODO: will it return MF_E_TRANSFORM_TYPE_NOT_SET if GetInputAvailableType did not?
    if (hr == MF_E_TRANSFORM_TYPE_NOT_SET)
        *later = true;
    if (FAILED(hr))
        return nullptr;
    std::clog << "used input type: " << std::endl;
    MS_ENSURE(mft_->GetInputCurrentType(stream_id, &type), nullptr);
    MS_ENSURE(type.As(&a), nullptr);
    MF::dump(a.Get());
    return type;
}

ComPtr<IMFMediaType> MFTCodec::selectOutputType(DWORD stream_id, bool* later)
{
    std::clog << __FUNCTION__ << std::endl;
    auto type = SelectType(stream_id, [this](DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType **ppType){
                                        return mft_->GetOutputAvailableType(dwOutputStreamID, dwTypeIndex, ppType);
                                    }, [this](IMFAttributes* a){
                                        return getOutputTypeScore(a);
                                    }, out_type_idx_, later); // optional
    if (*later) {
        std::clog << "at least 1 input type must be set first" << std::endl;
        return nullptr;
    }
    if (!type) {
        std::clog << "GetOutputAvailableType is not implemented or failed, try to create IMediaType manually" << std::endl;
        MS_ENSURE(MFCreateMediaType(&type), nullptr);
    }
    ComPtr<IMFAttributes> a;
    MS_ENSURE(type.As(&a), nullptr);
    if (!setOutputTypeAttributes(a.Get()))
        return nullptr;
    std::clog << "SetOutputType:" << std::endl;
    MS_ENSURE(type.As(&a), nullptr);
    MF::dump(a.Get());
    DWORD flags = 0; // MFT_SET_TYPE_TEST_ONLY
    HRESULT hr = S_OK;
    MS_WARN((hr = mft_->SetOutputType(stream_id, type.Get(), flags))); // set null to clear
    // TODO: will it return MF_E_TRANSFORM_TYPE_NOT_SET if GetOutputAvailableType did not?
    if (hr == MF_E_TRANSFORM_TYPE_NOT_SET)
        *later = true;
    if (FAILED(hr))
        return nullptr;
    std::clog << "used output type: " << std::endl;
    MS_ENSURE(mft_->GetOutputCurrentType(stream_id, &type), nullptr);
    MS_ENSURE(type.As(&a), nullptr);
    MF::dump(a.Get());
    return type;
}

// https://docs.microsoft.com/zh-cn/windows/desktop/medfound/basic-mft-processing-model#set-media-types
bool MFTCodec::setMediaTypes()
{
    ComPtr<IMFAttributes> a;
    bool in_later = false;
    if (!selectInputType(id_in_, &in_later) && !in_later)
        return false;
    bool out_later = false;
    if (!selectOutputType(id_out_, &out_later))
        return false;
    if (!in_later)
        return true;
    if (!selectInputType(id_in_, &in_later))
        return false;
    return true;
}

bool MFTCodec::flushCodec()
{ // https://docs.microsoft.com/zh-cn/windows/desktop/medfound/basic-mft-processing-model#flushing-an-mft
    MS_ENSURE(mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, ULONG_PTR()), false);
    discontinuity_ = true; // TODO:
    // TODO: async mft does not send another METransformNeedInput event until it receives an MFT_MESSAGE_NOTIFY_START_OF_STREAM message from the client
    // https://docs.microsoft.com/zh-cn/windows/desktop/medfound/mft-message-command-flush
    return true;
}

bool MFTCodec::decodePacket(const Packet& pkt)
{
    if (pkt.isEnd()) {
        MS_ENSURE(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, ULONG_PTR()), false); // not necessary
        // TODO: async must send START_OF_STREAM later to accept inputs. https://docs.microsoft.com/zh-cn/windows/desktop/medfound/mft-message-command-drain
        // when, how: https://docs.microsoft.com/zh-cn/windows/desktop/medfound/basic-mft-processing-model#draining-an-mft
        MS_ENSURE(mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, ULONG_PTR()), false);
        while (processOutput()) {}
        return false;
    }
    size_t size = pkt.buffer->size();
    uint8_t* data = (uint8_t*)pkt.buffer->constData();
    Packet filtered = pkt;
    if (data) {
        auto new_data = filter(data, &size);
        if (new_data) {
            filtered.buffer = std::make_shared<BufferView>(new_data, size);
            data = new_data;
        }
    }
    ComPtr<IMFSample> sample = MF::from(filtered, info_in_.cbAlignment);
    if (!sample)
        return false;
    if (discontinuity_) {
        discontinuity_ = false;
        ComPtr<IMFAttributes> attr;
        if (SUCCEEDED(sample.As(&attr)))
            attr->SetUINT32(MFSampleExtension_Discontinuity, 1);
    }
    auto hr = mft_->ProcessInput(id_in_, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) { // MUST be in 1 state: accept more input, produce more output
        while (processOutput()) {}
        MS_ENSURE(mft_->ProcessInput(id_in_, sample.Get(), 0), false);
    } else if (FAILED(hr)) { // MFT can drop it
        std::clog << "ProcessInput error: " << hr << std::endl;
        discontinuity_ = true;
        MS_ENSURE(hr, false);
        return true;
    }
    while (processOutput()) {} // get output ASAP. https://docs.microsoft.com/zh-cn/windows/desktop/medfound/basic-mft-processing-model#process-data
    if (!(info_in_.dwFlags & MFT_INPUT_STREAM_DOES_NOT_ADDREF)) {// ProcessInput() may but not always hold a reference count on the input samples
// mft bug? ProcessInput() may release the sample and MFCreateSample may reuse last one(not IMFTrackedSample) even if old sample is not released(keep by user)
    }
	return !pkt.isEnd();
}

ComPtr<IMFSample> MFTCodec::getOutSample()
{
    auto set_sample_buffers = [this](IMFSample* sample){
        ComPtr<IMFMediaBuffer> buf;
        const auto align = std::max<int>(16, info_out_.cbAlignment);
        MS_ENSURE(MFCreateAlignedMemoryBuffer(info_out_.cbSize, align - 1, &buf));
        sample->AddBuffer(buf.Get());
    };
    ComPtr<IMFSample> sample;
#if !(MS_WINRT+0)
    typedef HRESULT (STDAPICALLTYPE *MFCreateTrackedSample_fn)(IMFTrackedSample**);
    static HMODULE mfplat_dll = GetModuleHandleW(L"mfplat.dll");
    static auto MFCreateTrackedSample = (MFCreateTrackedSample_fn)GetProcAddress(mfplat_dll, "MFCreateTrackedSample"); // win8, phone8.1
    if (!MFCreateTrackedSample && use_pool_) {
        use_pool_ = false;
        std::clog << "MFCreateTrackedSample is not found in mfplat.dll. can not use IMFTrackedSample to reduce copy" << std::endl;
    }
#endif
    if (!use_pool_ || !pool_cb_) {
        MS_ENSURE(MFCreateSample(&sample), nullptr);
        set_sample_buffers(sample.Get());
        return sample;
    }
#if (_MSC_VER + 0)
    ComPtr<IMFTrackedSample> ts;
    if (!getPool()->pop(&ts)) {
        std::clog << this << " no sample in pool. create one" << std::endl;
        MS_ENSURE(MFCreateTrackedSample(&ts), nullptr);
        MS_ENSURE(ts.As(&sample), nullptr);
        set_sample_buffers(sample.Get());
    }
    ts->SetAllocator(pool_cb_.Get(), ts.Get()); // callback is cleared after invoke()
    MS_ENSURE(ts.As(&sample), nullptr);
#endif // (_MSC_VER + 0)
    return sample;
}

bool MFTCodec::processOutput()
{
    ComPtr<IMFSample> sample;
    const bool kProvidesSample = !!(info_out_.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES|MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));
    if (!kProvidesSample) // sw dec. if mft can provides samples but we want to use our samples, we must create correct sample type to be used by mft(e.g. d3d surface sample)
        sample = getOutSample();
    MFT_OUTPUT_DATA_BUFFER out{};
    out.dwStreamID = id_out_;
    out.pSample = sample.Get(); // nullptr: allocated by MFT if no flag MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER and can provide samples
    out.dwStatus = 0; // set by MFT
    out.pEvents = nullptr; // set by MFT
    DWORD status = 0;
    auto hr = mft_->ProcessOutput(0, 1, &out, &status);
    if (out.pEvents)
        out.pEvents->Release();
    // MFCreateVideoSampleFromSurface. additional ref is added to safe reuse the sample. https://msdn.microsoft.com/en-us/library/windows/desktop/ms697026(v=vs.85).aspx
    // https://docs.microsoft.com/zh-cn/windows/desktop/medfound/supporting-dxva-2-0-in-media-foundation#decoding
    // https://docs.microsoft.com/zh-cn/windows/desktop/medfound/supporting-direct3d-11-video-decoding-in-media-foundation#decoding
    ComPtr<IMFTrackedSample> tracked; // d3d11 or dxva2 provided by mft. or provided by our pool
    // if not provided by mft and need more input, out.pSample is null
    if (out.pSample && SUCCEEDED(out.pSample->QueryInterface(IID_PPV_ARGS(&tracked)))) {
        sample.Attach(out.pSample); // provided by mft or pool. DO NOT Release() pSample here. Otherwise TrackedSample callback is called and sample is recycled.
#ifdef __MINGW32__ // mingw adds ref in attach() https://sourceforge.net/p/mingw-w64/discussion/723797/thread/616a8df0ee . TODO: version check if fixed in mingw
        sample->Release();
#endif
    }
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return false;
    // dwStatus is not bit flags. https://docs.microsoft.com/en-us/windows/desktop/api/mftransform/nf-mftransform-imftransform-processoutput#stream-changes
    if (out.dwStatus & MFT_OUTPUT_DATA_BUFFER_STREAM_END) { // stream is deleted, not eof. stream info flag must have MFT_OUTPUT_STREAM_REMOVABLE
        std::clog << "MFT_OUTPUT_DATA_BUFFER_STREAM_END" << std::endl;
    } else if (out.dwStatus & MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS) {
        std::clog << "MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS" << std::endl;
    } else if (out.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE) {
        std::clog << "MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE" << std::endl;
    } else if (out.dwStatus & MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE) {
        std::clog << "MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE" << std::endl;
    }
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) { // TODO: status == MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS?
        std::clog << "MF_E_TRANSFORM_STREAM_CHANGE" << std::endl;
        sample.Reset(); // recycle if tracked
#if (_MSC_VER + 0)
        getPool()->clear(); // different buffer parameters. FIXME: how to clear all samples outside the pool? double pool and swap?
#endif
        // TODO: GetStreamIDs() again? https://docs.microsoft.com/zh-cn/windows/desktop/api/mftransform/ne-mftransform-_mft_process_output_status
        bool later = false;
        if (!selectOutputType(id_out_, &later))
            return false;
        ComPtr<IMFMediaType> type;
        MS_ENSURE(mft_->GetOutputCurrentType(id_out_, &type), false);
        if (!onOutputTypeChanged(id_out_, type))
            return false;
        return true;
    }
    if (FAILED(hr)) {
        MS_WARN(hr);
        if (use_pool_) { // h264? hevc works
            std::clog << "FIXME: ProcessOutput error and may be caused by output sample pool. Disable to workaround for now." << std::endl;
            use_pool_ = false;
        }
        return false;
    }
    return onOutput(sample);
}
MDK_NS_END