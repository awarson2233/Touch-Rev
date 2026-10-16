#pragma once

#include "LayoutEngine.h"

#include <windows.h>

#include <vector>

namespace touchrev::appswitcher
{
std::vector<WindowItem> EnumerateActiveWindows(HWND excludeHwnd);
// 注入空鼠标输入，让本进程获得 SetForegroundWindow 的权限。手势经 raw input 旁听，本进程收不到真实输入。
void AcquireForegroundRights();
void ActivateWindow(HWND targetHwnd);
bool RequestCloseWindow(HWND targetHwnd);
void ActivateWindow(HWND targetHwnd, POINT centerPoint, const RECT& fallbackWorkAreaPx);

// 系统移动循环只认鼠标：触摸/笔拖动要先把当前主指针转成鼠标拖拽才能交给它。
bool ConvertPrimaryPointerToMouseDrag();
// 经 explorer 的应用视图 cloak/恢复任意进程的窗口（DWM_CLOAKED_SHELL）；DWM 的 cloak 接口只允许操作本进程窗口。
// 被 shell cloak 的窗口不可见，也不参与贴靠建议。
bool SetShellCloak(HWND targetHwnd, bool cloaked);
// 还原目标窗口并把它的可见边界摆到 visibleRect（屏幕像素），再激活。
void PlaceWindowVisibleRect(HWND targetHwnd, const RECT& visibleRect);
}
