/*
 * Copyright (c) 2018-2019 WangBin <wbsecg1 at gmail.com>
 */
#pragma once
#include <memory>
#include <iostream>
#include <string>
#include <system_error>
#include <windows.h>
#ifdef WINAPI_FAMILY
# include <winapifamily.h>
# if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#   define MS_API_DESKTOP 1
# else
#   define MS_WINRT 1
#   define MS_API_APP 1
# endif
#else
# define MS_API_DESKTOP 1
#endif //WINAPI_FAMILY

// vista is required to build with wrl, but xp runtime works
#pragma push_macro("_WIN32_WINNT")
#if _WIN32_WINNT < _WIN32_WINNT_VISTA
# undef _WIN32_WINNT
# define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif
#include <wrl/client.h>
#if (_MSC_VER + 0) // missing headers in mingw
#include <wrl/implements.h> // RuntimeClass
#endif
#pragma pop_macro("_WIN32_WINNT")
using namespace Microsoft::WRL; //ComPtr

// TODO: define MS_ERROR_STR before including this header to handle module specific errors?
#define MS_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define MS_WARN(f) MS_CHECK(f)
#define MS_CHECK(f, ...)  do { \
        while (FAILED(GetLastError())) {} \
        HRESULT __ms_hr__ = f; \
        if (FAILED(__ms_hr__)) { \
            std::clog << #f "  ERROR@" << __LINE__ << __FUNCTION__ << ": (" << std::hex << __ms_hr__ << std::dec << ") " << std::error_code(__ms_hr__, std::system_category()).message() << std::endl; \
            __VA_ARGS__ \
        } \
    } while (false)

#define DX_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define DX_WARN(f) MS_CHECK(f)


using module_t = std::remove_pointer<HMODULE>::type;
using dll_t = std::unique_ptr<module_t, decltype(&FreeLibrary)>;

uint32_t to_fourcc(const GUID id);