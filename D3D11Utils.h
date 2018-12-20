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
# pragma push_macro("_WIN32_WINNT")
# if _WIN32_WINNT < 0x0601 // for IDXGIFactory2
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0601
# endif
#include "base/ms/MSUtils.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h> // MFCreateDXGIDeviceManager 
#include <mfidl.h>
# pragma pop_macro("_WIN32_WINNT")

namespace D3D11 {

ComPtr<IDXGIFactory> CreateDXGI();
ComPtr<ID3D11Device> CreateDevice(ComPtr<IDXGIFactory> dxgi, UINT adapterIndex = 0);

class Manager
{
public:
    bool init(UINT adapterIndex = 0);
    ComPtr<IMFDXGIDeviceManager> create(); // win8+
private:
    dll_t d3d11_dll_{nullptr, &FreeLibrary};
    dll_t dxgi_dll_{nullptr, &FreeLibrary};
    dll_t mfplat_dll_{nullptr, &FreeLibrary};
    ComPtr<IDXGIFactory> dxgi_; // create first, destroy last
    ComPtr<ID3D11Device> dev_;
    ComPtr<IMFDXGIDeviceManager> mgr_;
};
} // namespace D3D11
