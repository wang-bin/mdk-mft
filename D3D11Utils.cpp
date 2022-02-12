/*
 * Copyright (c) 2018-2022 WangBin <wbsecg1 at gmail.com>
 */
#include "D3D11Utils.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <type_traits>
#include <d3d11_1.h>
#include "base/fmt.h"
#pragma comment(lib, "dxguid.lib") // WKPDID_D3DDebugObjectName
// gpu select: https://github.com/walbourn/directx-vs-templates/commit/e6406ee9afaf2719ff29b68c63c7adbcac57c02a
using namespace std;

namespace DXGI {

// TODO: enum + reflection
// https://gamedev.stackexchange.com/questions/31625/get-video-chipset-manufacturer-in-direct3d
static const struct {
    UINT id;
    const char* name;
} vendor[] = {
    {0x1414, "MicroSoft"},
    {0x1002, "AMD"},
    {0x1022, "AMD"},
    {0x10DE, "NVIDIA"},
    {0x1106, "VIA"},
    {0x163C, "INTEL"},
    {0x8086, "INTEL"},
    {0x8087, "INTEL"},
    {0x5333, "S3"},
    {0x4D4F4351, "QUALCOMM"},
    {0x344c5250, "Parallels"},
    // "ParallelDesktop", "VMWare", "VirtualBox"
};

const char* VendorName(UINT id)
{
    for (const auto& v : vendor) {
        if (v.id == id)
            return v.name;
    }
    return "Unknown";
}

static int VendorIndex(ComPtr<IDXGIFactory> dxgi, const char* vendor)
{
    if (!dxgi || !vendor)
        return 0;
    ComPtr<IDXGIAdapter> adapter;
    HRESULT hr = S_OK;
    for (int i = 0; hr != DXGI_ERROR_NOT_FOUND; ++i) {
        hr = dxgi->EnumAdapters(i, &adapter); // dxgi2?
        if (FAILED(hr) && hr != DXGI_ERROR_NOT_FOUND)
            return -1;
        DXGI_ADAPTER_DESC desc;
        MS_ENSURE(adapter->GetDesc(&desc), 0);
        string s(DXGI::VendorName(desc.VendorId));
        transform(s.begin(), s.end(), s.begin(), [](char c){ return (char)tolower(c);});
        if (s.find(vendor) != string::npos)
            return i;
        s.resize(256);
        snprintf(&s[0], s.size(), "%ls", desc.Description);
        transform(s.begin(), s.end(), s.begin(), [](char c){ return (char)tolower(c);});
        if (s.find(vendor) != string::npos)
            return i;
    }
    return -1;
}
} // namespace DXGI

namespace D3D11 {
using namespace std;
D3D_FEATURE_LEVEL to_feature_level(float value)
{
    if (value < 9.1f)
        return D3D_FEATURE_LEVEL_12_1;
    if (value >= 12.1f)
        return D3D_FEATURE_LEVEL_12_1;
    else if (value >= 12.0f)
        return D3D_FEATURE_LEVEL_12_0;
    else if (value >= 11.1f)
        return D3D_FEATURE_LEVEL_11_1;
    else if (value >= 11.0f)
        return D3D_FEATURE_LEVEL_11_0;
    else if (value >= 10.1f)
        return D3D_FEATURE_LEVEL_10_1;
    else if (value >= 10.0f)
        return D3D_FEATURE_LEVEL_10_0;
    else if (value >= 9.3f)
        return D3D_FEATURE_LEVEL_9_3;
    else if (value >= 9.2f)
        return D3D_FEATURE_LEVEL_9_2;
    else if (value >= 9.1f)
        return D3D_FEATURE_LEVEL_9_1;
    return D3D_FEATURE_LEVEL_11_1;
}

D3D_FEATURE_LEVEL to_feature_level(const char* name)
{
    float fl = 12.1f; // TODO: check os version. win10: 12.x, win8: 11.1
    if (name)
        fl =  std::stof(name);
    return to_feature_level(fl);
}

static void debug_to_log(ID3D11InfoQueue* iq)
{
    auto nb_msgs = iq->GetNumStoredMessages();
    for (UINT64 i = 0; i < nb_msgs; ++i) {
        SIZE_T len = 0;
        MS_ENSURE(iq->GetMessage(0, NULL, &len));
        D3D11_MESSAGE* msg = (D3D11_MESSAGE*)malloc(len);
        if (FAILED(iq->GetMessage(0, msg, &len))) {
            free(msg);
            return;
        }
        clog << fmt::to_string("ID %d(Severity %d): %.*s", msg->ID, msg->Severity, (int)msg->DescriptionByteLength, msg->pDescription) << endl;
        free(msg);
    }
    iq->ClearStoredMessages();
}

void debug_report(ID3D11Device* dev, const char* prefix)
{
    if (!dev)
        return;
     ComPtr<ID3D11Debug> dbg;
     if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dbg))))
        return;
    ComPtr<ID3D11InfoQueue> iq;
     if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&iq))))
        return;
    // Using ID3D11Debug::ReportLiveDeviceObjects with D3D11_RLDO_DETAIL will help drill into object lifetimes. Objects with Refcount=0 and IntRef=0 will be eventually destroyed through typical Immediate Context usage. However, if the application requires these objects to be destroyed sooner, ClearState followed by Flush on the Immediate Context will realize their destruction.
    {
        ComPtr<ID3D11DeviceContext> ctx;
        dev->GetImmediateContext(&ctx);
        ctx->ClearState();
        ctx->Flush();
    }
    const auto extra = prefix ? prefix : " ?";
    clog << fmt::to_string("------------debug report detail begin. device %p. %s------------", dev, extra) << endl;
    dbg->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL); //D3D11_RLDO_IGNORE_INTERNAL
    debug_to_log(iq.Get());
    clog << fmt::to_string("------------debug report summary begin. device %p. %s------------", dev, extra) << endl;
    dbg->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY);
    debug_to_log(iq.Get());
    clog << fmt::to_string("------------debug report end. device %p. %s------------", dev, extra) << endl;
}

ComPtr<IDXGIFactory> CreateDXGI() // TODO: int version = 2. 1.0/1.1 can not be mixed
{
#if (MS_API_DESKTOP+0)
    dll_t h{nullptr, &FreeLibrary};
    HMODULE dxgi_dll = GetModuleHandleA("dxgi.dll");
    if (!dxgi_dll) {
        h.reset(LoadLibraryA("dxgi.dll"));
        if (!h)
            return nullptr;
        dxgi_dll = GetModuleHandleA("dxgi.dll");
        if (!dxgi_dll)
            return nullptr;
    }
    typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID riid, void **ppFactory);
    auto CreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY)GetProcAddress(dxgi_dll, "CreateDXGIFactory");
    if (!CreateDXGIFactory)
        return nullptr;
#else
    auto CreateDXGIFactory = &CreateDXGIFactory1; // no CreateDXGIFactory, only CreateDXGIFactory1/2
#endif
    ComPtr<IDXGIFactory2> dxgi2;
    if (SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi2))))
        return dxgi2;
    ComPtr<IDXGIFactory1> dxgi1;
    if (SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi1))))
        return dxgi1;
    ComPtr<IDXGIFactory> dxgi;
    if (SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi))))
        return dxgi;
    return nullptr;
}

// Starting with Windows 8, an adapter called the "Microsoft Basic Render Driver" is always present. This adapter has a VendorId of 0x1414 and a DeviceID of 0x8c
ComPtr<ID3D11Device> CreateDevice(ComPtr<IDXGIFactory> dxgi, int adapterIndex, D3D_FEATURE_LEVEL fl, UINT flags)
{
    // always link to d3d11 to ensure d3d11.dll is loaded even if Manager is not used
    ComPtr<IDXGIAdapter> adapter;
    if (dxgi && adapterIndex >= 0) {
        MS_WARN(dxgi->EnumAdapters(adapterIndex, &adapter)); // dxgi2?
        DXGI_ADAPTER_DESC desc;
        MS_ENSURE(adapter->GetDesc(&desc), nullptr);
        char description[256]{};
        snprintf(description, sizeof(description), "%ls", desc.Description);
        clog << fmt::to_string("d3d11 adapter %d: vendor %x, device %x, revision %x, %s", adapterIndex, desc.VendorId, desc.DeviceId, desc.Revision, description) << endl;
    } else {
        std::clog << "d3d11 use default adapter" << std::endl;
    }
    const D3D_FEATURE_LEVEL fls[] = {
        fl,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3, // win7, win10/8 mobile/phone
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1, // low power devices
    };
    ComPtr<ID3D11Device> dev;
    // d3d11.1: MUST explicitly provide an array includes D3D_FEATURE_LEVEL_11_1. If Direct3D 11.1 runtime is not available, immediately fails with E_INVALIDARG.
    HRESULT hr = S_OK;
    size_t fl_index = 0;
    for (int i = 1; i < std::extent<decltype(fls)>::value; ++i) {
        if (fls[i] == fl) {
            fl_index = i;
            break;
        }
    }
    // adapter non-null, type D3D_DRIVER_TYPE_HARDWARE: E_INVALIDARG
    MS_WARN((hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE
        , nullptr, flags, &fls[fl_index], (UINT)(std::extent<decltype(fls)>::value - fl_index)
        , D3D11_SDK_VERSION, &dev, nullptr, nullptr)));
    if (hr == E_INVALIDARG) // if fls contains 11_1 but not supported by runtime. null fls will try 11_0~9_1
        MS_ENSURE(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE
        , nullptr, flags, nullptr, 0 //&fls[2], std::extent<decltype(fls)>::value - 2
        , D3D11_SDK_VERSION, &dev, nullptr, nullptr), nullptr);
    ComPtr<ID3D11Device1> dev1;
    MS_WARN(dev.As(&dev1));
    clog << fmt::to_string("d3d11.%d device feature level: %#x, requested: %#x.", dev1.Get() ? 1 : 0, dev->GetFeatureLevel(), fl) << endl;
    // if null adapter, print adapter info
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(dev.As(&mt)))
        mt->SetMultithreadProtected(TRUE);
    return dev;
}

ComPtr<ID3D11Device> CreateDevice(const char* vendor, int adapterIndex, D3D_FEATURE_LEVEL fl, UINT flags)
{
    auto dxgi = D3D11::CreateDXGI();
    if (vendor)
        adapterIndex = DXGI::VendorIndex(dxgi, vendor);
    return CreateDevice(D3D11::CreateDXGI(), adapterIndex, fl, flags);
}

static bool is_trace_enabled()
{
    char v[4]{};
    if (GetEnvironmentVariableA("D3D11_TRACE", v, sizeof(v)) > 0)
        return !!std::stoi(v);
    return false;
}

void trace(ComPtr<IUnknown> obj, const char* name)
{
    if (!obj)
        return;
    if (name)
        SetDebugName(obj, name);
    static bool enabled = is_trace_enabled();
    if (!enabled)
        return;
    class __declspec(uuid("50581513-C9A0-454A-B78F-3A3156D06AC4")) DXTracer final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown> {
    public:
    // IInspectable.GetRuntimeClassName? WKPDID_D3DDebugObjectName?
        DXTracer(const char* name) : name_(name) { clog << fmt::to_string("%s %p (%s)", __func__, this, name_) << endl; }
        ~DXTracer() override { clog << fmt::to_string("%s %p (%s)", __func__, this, name_) << endl; }
    private:
        const char* name_ = "?";
    };

    UINT size = sizeof(DXTracer*);
    ComPtr<DXTracer> dt;
    if (ComPtr<ID3D11DeviceChild> x; SUCCEEDED(obj.As(&x)) && SUCCEEDED(x->GetPrivateData(__uuidof(DXTracer), &size, dt.ReleaseAndGetAddressOf())))
        return;
    else if (ComPtr<ID3D11Device> x; SUCCEEDED(obj.As(&x)) && SUCCEEDED(x->GetPrivateData(__uuidof(DXTracer), &size, dt.ReleaseAndGetAddressOf())))
        return;
    else if (ComPtr<IDXGIObject> x; SUCCEEDED(obj.As(&x)) && SUCCEEDED(x->GetPrivateData(__uuidof(DXTracer), &size, dt.ReleaseAndGetAddressOf())))
        return;
    if (dt)
        return;
    dt = Make<DXTracer>(name ? name : "?");
    if (ComPtr<ID3D11DeviceChild> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateDataInterface(__uuidof(DXTracer), dt.Get()));
    else if (ComPtr<ID3D11Device> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateDataInterface(__uuidof(DXTracer), dt.Get()));
    else if (ComPtr<IDXGIObject> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateDataInterface(__uuidof(DXTracer), dt.Get()));
    //SetPrivateDataInterface(__uuidof(xxx), xxx) // xxx ref +1, so manually Release() after set. GetPrivateData()
}

void SetDebugName(ComPtr<IUnknown> obj, const char* name)
{
    if (!obj)
        return;
    if (ComPtr<ID3D11DeviceChild> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name));
    else if (ComPtr<ID3D11Device> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name));
    else if (ComPtr<IDXGIObject> x; SUCCEEDED(obj.As(&x)))
        MS_ENSURE(x->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name));
}

bool Manager::init(const char* vendor, int adapterIndex, D3D_FEATURE_LEVEL fl, UINT flags)
{
#if (MS_API_DESKTOP+0)
    d3d11_dll_.reset(LoadLibraryA("d3d11.dll"));
#endif
    dev_ = D3D11::CreateDevice(vendor, adapterIndex, fl, D3D11_CREATE_DEVICE_VIDEO_SUPPORT|flags);
    if (!dev_)
        return false;
    trace(dev_, "MFDXDevice");
    debug_report(dev_.Get());
    return true;
}

Manager::~Manager()
{
    mgr_.Reset();
    debug_report(dev_.Get(), __func__);
}

ComPtr<IMFDXGIDeviceManager> Manager::create()
{
    if (!dev_)
        return nullptr;
    UINT token = 0;
#if (MS_API_DESKTOP+0)
    mfplat_dll_.reset(LoadLibraryA("mfplat.dll"));
    if (!mfplat_dll_)
        return nullptr;
    typedef HRESULT (STDAPICALLTYPE *MFCreateDXGIDeviceManager_pfn)(UINT*, IMFDXGIDeviceManager**);
    auto MFCreateDXGIDeviceManager = (MFCreateDXGIDeviceManager_pfn)GetProcAddress(mfplat_dll_.get(), "MFCreateDXGIDeviceManager");
    if (!MFCreateDXGIDeviceManager) {
        std::clog << "MFCreateDXGIDeviceManager is not found in mfplat.dll. try mshtmlmedia.dll" << std::endl;
        // https://chromium.googlesource.com/chromium/src.git/+/62.0.3178.1/media/gpu/dxva_video_decode_accelerator_win.cc?autodive=0%2F%2F%2F%2F
        mfplat_dll_.reset(LoadLibraryA("mshtmlmedia.dll"));
        if (!mfplat_dll_) {
            std::clog << "Failed to load mshtmlmedia.dll" << std::endl;
            return nullptr;
        }
        MFCreateDXGIDeviceManager = (MFCreateDXGIDeviceManager_pfn)GetProcAddress(mfplat_dll_.get(), "MFCreateDXGIDeviceManager");
        if (!MFCreateDXGIDeviceManager) {
            std::clog << "MFCreateDXGIDeviceManager is not found" << std::endl;
            return nullptr;
        }
    }
#endif
    MS_ENSURE(MFCreateDXGIDeviceManager(&token, &mgr_), nullptr);
    mgr_->ResetDevice(dev_.Get(), token); // MUST create with D3D11_CREATE_DEVICE_VIDEO_SUPPORT
    // ResetDevice(nullptr, token) in dtor?
    return mgr_;
}
} // namespace D3D11
