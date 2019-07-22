/*
 * Copyright (c) 2018 WangBin <wbsecg1 at gmail.com>
 */
#include "MSUtils.h"

// xxxxxxxx-0000-0010-8000-00AA00389B71. https://docs.microsoft.com/en-us/windows/win32/directshow/fourcc-codes
uint32_t to_fourcc(const GUID id)
{
    if (id.Data2 == 0 && id.Data3 == 0x0010
        && id.Data4[0] == 0x80 && id.Data4[1] == 0x00
        && id.Data4[2] == 0x00 && id.Data4[3] == 0xAA && id.Data4[4] == 0x00
        && id.Data4[5] == 0x38 && id.Data4[6] == 0x9B && id.Data4[7] == 0x71)
        return id.Data1;
    return 0;
}