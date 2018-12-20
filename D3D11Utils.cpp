/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "D3D11Utils.h"
#include <iostream>
// gpu select: https://github.com/walbourn/directx-vs-templates/commit/e6406ee9afaf2719ff29b68c63c7adbcac57c02a

namespace D3D11 {

ComPtr<IDXGIFactory> CreateDXGI() // TODO: int version = 2. 1.0/1.1 can not be mixed
{
#if (MS_API_DESKTOP+0)
    HMODULE dxgi_dll = GetModuleHandleA("dxgi.dll");
    if (!dxgi_dll)
        return nullptr;
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
ComPtr<ID3D11Device> CreateDevice(ComPtr<IDXGIFactory> dxgi, UINT adapterIndex)
{
    if (!dxgi)
        return nullptr;
#if (MS_API_DESKTOP+0)
    HMODULE d3d11_dll = GetModuleHandleA("d3d11.dll");
    if (!d3d11_dll)
        return nullptr;
    auto D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11_dll, "D3D11CreateDevice");
    if (!D3D11CreateDevice)
        return nullptr;
#endif
    ComPtr<IDXGIAdapter> adapter;
    MS_WARN(dxgi->EnumAdapters(adapterIndex, &adapter)); // dxgi2?
    DXGI_ADAPTER_DESC desc;
    MS_ENSURE(adapter->GetDesc(&desc), nullptr);
    char description[256]{};
    snprintf(description, sizeof(description), "%ls", desc.Description);
    std::clog << "adapter " << adapterIndex << ": " << description << std::endl;
    const D3D_FEATURE_LEVEL* fls = nullptr;
    ComPtr<ID3D11Device> dev;
    // adapter non-null, type D3D_DRIVER_TYPE_HARDWARE: E_INVALIDARG 
    MS_ENSURE(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE
        , nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, fls, 0
        , D3D11_SDK_VERSION, &dev, nullptr, nullptr), nullptr);
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(dev.As(&mt)))
        mt->SetMultithreadProtected(TRUE);
    return dev;
}


bool Manager::init(UINT adapterIndex)
{
    if (!dxgi_) {
#if (MS_API_DESKTOP+0)
        dxgi_dll_.reset(LoadLibraryA("dxgi.dll"));
        if (!dxgi_dll_)
            return false;
#endif
        dxgi_ = D3D11::CreateDXGI();
    }
    if (!dxgi_)
        return false;
#if (MS_API_DESKTOP+0)
    d3d11_dll_.reset(LoadLibraryA("d3d11.dll"));
#endif
    dev_ = D3D11::CreateDevice(dxgi_, adapterIndex);
    if (!dev_)
        return false;
    return true;
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
    typedef HRESULT (*MFCreateDXGIDeviceManager_pfn)(UINT*, IMFDXGIDeviceManager**);
    auto MFCreateDXGIDeviceManager = (MFCreateDXGIDeviceManager_pfn)GetProcAddress(mfplat_dll_.get(), "MFCreateDXGIDeviceManager");
    if (!MFCreateDXGIDeviceManager)
        return nullptr;
#endif
    MS_ENSURE(MFCreateDXGIDeviceManager(&token, &mgr_), nullptr);
    mgr_->ResetDevice(dev_.Get(), token); // MUST create with D3D11_CREATE_DEVICE_VIDEO_SUPPORT
    // ResetDevice(nullptr, token) in dtor?
    return mgr_;
}
} // namespace D3D11
