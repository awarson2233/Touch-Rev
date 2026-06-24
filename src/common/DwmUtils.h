#pragma once
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace touchrev::common::dwm
{
inline bool IsWindowCloaked(HWND hwnd)
{
    int cloakedState = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedState, sizeof(cloakedState))))
    {
        return cloakedState != 0;
    }
    return false;
}

inline bool SetWindowCloaked(HWND hwnd, bool cloaked)
{
    const int value = cloaked ? TRUE : FALSE;
    const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &value, sizeof(value));
    return SUCCEEDED(hr);
}
}
