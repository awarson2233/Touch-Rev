#pragma once

#include "AppSwitcherLayoutEngine.h"

#include <windows.h>

#include <vector>

namespace touchrev::appswitcher
{
std::vector<AppSwitcherWindowItem> EnumerateSwitcherWindows(HWND excludeHwnd);
void ActivateWindow(HWND targetHwnd);
bool RequestCloseWindow(HWND targetHwnd);
void ExpandWindowAroundPoint(HWND targetHwnd, POINT centerPoint, const RECT& fallbackWorkAreaPx);
}
