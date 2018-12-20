/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK MFT plugin
 * Source code: https://github.com/wang-bin/mdk-mft
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "D3D9Utils.h"
#if (MS_API_DESKTOP+0)
namespace D3D9 {

ComPtr<IDirect3D9> Create()
{
    HMODULE dll = GetModuleHandleA("d3d9.dll");
    if (!dll) {
        std::clog << "d3d9.dll is not loaded" << std::endl;
        return nullptr;
    }
    ComPtr<IDirect3D9> id3d9;
    typedef HRESULT (WINAPI *Create9ExFunc)(UINT SDKVersion, IDirect3D9Ex **ppD3D); //IDirect3D9Ex: void is ok
    auto create9ex = (Create9ExFunc)GetProcAddress(dll, "Direct3DCreate9Ex");
    if (create9ex) {
        ComPtr<IDirect3D9Ex> id3d9ex;
        MS_WARN(create9ex(D3D_SDK_VERSION, &id3d9ex));
        id3d9 = id3d9ex;
    }
    if (id3d9)
        return id3d9;
    IDirect3D9 * WINAPI Direct3DCreate9(UINT SDKVersion);
    typedef IDirect3D9* (WINAPI *Create9Func)(UINT SDKVersion);
    Create9Func create9 = (Create9Func)GetProcAddress(dll, "Direct3DCreate9");
    if (!create9)
        return nullptr;
    id3d9 = create9(D3D_SDK_VERSION);
    if (id3d9)
        return id3d9;
    std::clog << "Failed to create d3d9" << std::endl;
    return nullptr;
}

// getenv("DXADAPTOR") here or in caller? different adapters works together?
ComPtr<IDirect3DDevice9> CreateDevice(ComPtr<IDirect3D9> id3d9, UINT adapter)
{
    D3DADAPTER_IDENTIFIER9 ai{};
    MS_ENSURE(id3d9->GetAdapterIdentifier(adapter, 0, &ai), nullptr); // adapter < GetAdapterCount()
    std::clog << "D3D9 adapter " << adapter << " driver: " << ai.Driver << ", device: " << ai.DeviceName << ", detail: "<< ai.Description << std::endl;
    ComPtr<IDirect3DDevice9> dev;
    D3DPRESENT_PARAMETERS pp{};
    pp.Flags                  = D3DPRESENTFLAG_VIDEO;
    pp.Windowed               = TRUE;
    pp.hDeviceWindow          = ::GetShellWindow(); //NULL;
    pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    //pp.MultiSampleType        = D3DMULTISAMPLE_NONE;
    //pp.PresentationInterval   = D3DPRESENT_INTERVAL_DEFAULT;
    pp.BackBufferCount        = 1; //0;                  /* FIXME what to put here */
    pp.BackBufferFormat       = D3DFMT_UNKNOWN; //D3DFMT_X8R8G8B8;    /* FIXME what to put here */
    pp.BackBufferWidth        = 1; //0;
    pp.BackBufferHeight       = 1; //0;
    //pp.EnableAutoDepthStencil = FALSE;

    // D3DCREATE_MULTITHREADED is required by gl interop. https://www.opengl.org/registry/specs/NV/DX_interop.txt
    // D3DCREATE_SOFTWARE_VERTEXPROCESSING in other dxva decoders. D3DCREATE_HARDWARE_VERTEXPROCESSING is required by cuda in cuD3D9CtxCreate()
    DWORD flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING;
    ComPtr<IDirect3D9Ex> id3d9ex;
    if (SUCCEEDED(id3d9.As(&id3d9ex))) {
        ComPtr<IDirect3DDevice9Ex> devex;
        MS_WARN(id3d9ex->CreateDeviceEx(adapter, D3DDEVTYPE_HAL, GetShellWindow(),// GetDesktopWindow(), //GetShellWindow()?
                                         flags, &pp, nullptr, &devex));
        dev = devex;
    }
    if (dev) {
        std::clog << "IDirect3DDevice9Ex is created" << std::endl;
        return dev;
    }
    MS_ENSURE(id3d9->CreateDevice(adapter, D3DDEVTYPE_HAL, GetShellWindow(),// GetDesktopWindow(), //GetShellWindow()?
                                   flags, &pp, &dev), nullptr);
    return dev;
}

bool Manager::init(UINT adapter)
{
    if (!id3d9_) {
        d3d9_dll_.reset(LoadLibraryA("d3d9.dll"));
        if (!d3d9_dll_)
            return false;
        id3d9_ = D3D9::Create();
    }
    if (!id3d9_)
        return false;
    dev_ = D3D9::CreateDevice(id3d9_, adapter);
    if (!dev_)
        return false;
    return true;
}

ComPtr<IDirect3DDeviceManager9> Manager::create()
{
    if (!dev_)
        return nullptr;
    dxva2_dll_.reset(LoadLibraryA("dxva2.dll"));
    if (!dxva2_dll_)
        return nullptr;
    typedef HRESULT (WINAPI *DXVA2CreateDirect3DDeviceManager9_f)(UINT *pResetToken, IDirect3DDeviceManager9 **);
    auto DXVA2CreateDirect3DDeviceManager9 = (DXVA2CreateDirect3DDeviceManager9_f)GetProcAddress(dxva2_dll_.get(), "DXVA2CreateDirect3DDeviceManager9");
    UINT token = 0;
    MS_ENSURE(DXVA2CreateDirect3DDeviceManager9(&token, &mgr_), nullptr);
    mgr_->ResetDevice(dev_.Get(), token);
    return mgr_;
}
} // namespace D3D9
#endif // (MS_API_DESKTOP+0)