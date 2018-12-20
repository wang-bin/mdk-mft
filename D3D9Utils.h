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
#include "base/ms/MSUtils.h"
#if (MS_API_DESKTOP+0)
#include <d3d9types.h>
#include <d3d9.h> // ensure IDirect3DDeviceManager9 is defined
#include <dxva2api.h>
namespace D3D9 {
ComPtr<IDirect3D9> Create();
// TODO: CreateVideoDevice()
ComPtr<IDirect3DDevice9> CreateDevice(ComPtr<IDirect3D9> id3d9, UINT adapter = D3DADAPTER_DEFAULT);

class Manager
{
public:
    bool init(UINT adapter = D3DADAPTER_DEFAULT);
    ComPtr<IDirect3DDeviceManager9> create();
private:
    dll_t d3d9_dll_{nullptr, &FreeLibrary};
    dll_t dxva2_dll_{nullptr, &FreeLibrary};
    ComPtr<IDirect3D9> id3d9_; // create first, destroy last
    ComPtr<IDirect3DDevice9> dev_;
    ComPtr<IDirect3DDeviceManager9> mgr_;
};
} // namespace D3D9
#endif // (MS_API_DESKTOP+0)