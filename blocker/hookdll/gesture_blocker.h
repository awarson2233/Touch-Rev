#pragma once

#include <windows.h>

namespace touchrev {

using ShowWindowFn = BOOL(WINAPI*)(HWND hwnd, int nCmdShow);
using SetWindowPosFn = BOOL(WINAPI*)(HWND hwnd,
                                     HWND hwndInsertAfter,
                                     int x,
                                     int y,
                                     int cx,
                                     int cy,
                                     UINT flags);

bool IsRecentTaskSwitcherBlockActive();
void StartThreeFingerSwipeUpBlockWindow();
bool ShouldBlockMessage(HWND hwnd,
                        UINT msg,
                        WPARAM wParam,
                        LPARAM lParam,
                        bool allowShellMultitaskingStateQuery,
                        PCWSTR* reason);
bool ShouldBlockDirectAppThumbnailWindow(HWND hwnd);
bool ShouldBlockForegroundCommitWindow(HWND hwnd);
void LogBlockedForegroundCommit(PCWSTR api, HWND hwnd, PCWSTR reason);
void ActivateFollowUpBlockers(HWND hwnd,
                              UINT msg,
                              WPARAM wParam,
                              LPARAM lParam,
                              bool allowShellMultitaskingStateQuery,
                              PCWSTR reason);
void HideAppThumbnailWindow(HWND hwnd,
                            PCWSTR reason,
                            ShowWindowFn showWindowOriginal,
                            SetWindowPosFn setWindowPosOriginal);
void HideMessageWindows(HWND hwnd,
                        LPARAM lParam,
                        PCWSTR api,
                        ShowWindowFn showWindowOriginal,
                        SetWindowPosFn setWindowPosOriginal);
void LogBlockedMessage(PCWSTR api,
                       HWND hwnd,
                       UINT msg,
                       WPARAM wParam,
                       LPARAM lParam,
                       PCWSTR reason);

}  // namespace touchrev
