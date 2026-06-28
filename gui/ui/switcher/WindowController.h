#pragma once

#include "LayoutEngine.h"

#include <windows.h>

#include <vector>

namespace touchrev::appswitcher
{
std::vector<WindowItem> EnumerateActiveWindows(HWND excludeHwnd);
void ActivateWindow(HWND targetHwnd);
bool RequestCloseWindow(HWND targetHwnd);
void ActivateWindow(HWND targetHwnd, POINT centerPoint, const RECT& fallbackWorkAreaPx);
}
