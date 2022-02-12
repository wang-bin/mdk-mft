/*
 * Copyright (c) 2018-2022 WangBin <wbsecg1 at gmail.com>
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
// result feature level will be <= requested value
ComPtr<ID3D11Device> CreateDevice(ComPtr<IDXGIFactory> dxgi, int adapterIndex = 0, D3D_FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1, UINT flags = 0);
// vendor != null: try to match vendor
// vendor == null && adapterIndex == -1: default
ComPtr<ID3D11Device> CreateDevice(const char* vendor = nullptr, int adapterIndex = 0, D3D_FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1, UINT flags = 0);
D3D_FEATURE_LEVEL to_feature_level(float value = 0);
D3D_FEATURE_LEVEL to_feature_level(const char* name = nullptr);
void debug_report(ID3D11Device* dev, const char* prefix = nullptr);

// IDXGIObject, ID3D11Device, ID3D11DeviceChild
void SetDebugName(ComPtr<IUnknown> obj, const char* name);

void trace(ComPtr<IUnknown> obj, const char* name = nullptr);

class Manager
{
public:
    ~Manager();
// vendor != null: try to match vendor
// vendor == null && adapterIndex == -1: default
    bool init(const char* vendor = nullptr, int adapterIndex = 0, D3D_FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1, UINT flags = 0);
    ComPtr<IMFDXGIDeviceManager> create(); // win8+
private:
    dll_t d3d11_dll_{nullptr, &FreeLibrary};
    dll_t mfplat_dll_{nullptr, &FreeLibrary};
    ComPtr<ID3D11Device> dev_;
    ComPtr<IMFDXGIDeviceManager> mgr_;
};
} // namespace D3D11
