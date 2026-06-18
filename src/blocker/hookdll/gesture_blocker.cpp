#include "hookdll/gesture_blocker.h"

#include "common/constants.h"
#include "common/log.h"
#include "common/winutil.h"

#include <atomic>

namespace touchrev {
namespace {

std::atomic<ULONGLONG> g_recentTaskSwitcherBlockUntilTick{0};
std::atomic<DWORD> g_blockDurationMs{kDefaultBlockDurationMs};

bool IsAppThumbnailWindow(HWND hwnd) {
    return WindowClassEquals(hwnd, kClassAppThumbnailWindow);
}

bool IsShellRoutingWindow(HWND hwnd) {
    return WindowClassEquals(hwnd, kClassShellTrayWnd) ||
           WindowClassEquals(hwnd, kClassWorkerW);
}

bool ShouldBlockThreeFingerLongPressSignal(HWND hwnd, UINT msg) {
    return msg == kShellTrayThreeFingerLongPressMessage &&
           WindowClassEquals(hwnd, kClassShellTrayWnd);
}

bool ShouldBlockWorkerRoutingToAppThumbnail(HWND hwnd,
                                            UINT msg,
                                            WPARAM wParam,
                                            LPARAM lParam) {
    if (msg != kWorkerGestureMessage ||
        (wParam != kWorkerGestureBegin && wParam != kWorkerGestureEnd) ||
        !WindowClassEquals(hwnd, kClassWorkerW)) {
        return false;
    }

    if (!IsRecentTaskSwitcherBlockActive()) {
        return false;
    }

    HWND targetHwnd = reinterpret_cast<HWND>(lParam);
    return targetHwnd && IsWindow(targetHwnd) && IsAppThumbnailWindow(targetHwnd);
}

void StartRecentTaskSwitcherBlockWindow(PCWSTR reason) {
    DWORD durationMs = g_blockDurationMs.load(std::memory_order_relaxed);
    ULONGLONG untilTick = GetTickCount64() + durationMs;
    ULONGLONG current = g_recentTaskSwitcherBlockUntilTick.load();

    while (untilTick > current &&
           !g_recentTaskSwitcherBlockUntilTick.compare_exchange_weak(current,
                                                                     untilTick)) {
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=BLOCK_WINDOW_START activeForMs=%lu reason=%s",
               durationMs, reason ? reason : L"");
}

}  // namespace

bool IsRecentTaskSwitcherBlockActive() {
    ULONGLONG untilTick = g_recentTaskSwitcherBlockUntilTick.load();
    return untilTick != 0 && GetTickCount64() <= untilTick;
}

void StartThreeFingerSwipeUpBlockWindow() {
    StartRecentTaskSwitcherBlockWindow(L"twinui three-finger swipe-up");
}

bool ShouldBlockDirectAppThumbnailWindow(HWND hwnd) {
    return IsRecentTaskSwitcherBlockActive() && IsAppThumbnailWindow(hwnd);
}

bool ShouldBlockForegroundCommitWindow(HWND hwnd) {
    if (!IsRecentTaskSwitcherBlockActive() || !hwnd || !IsWindow(hwnd)) {
        return false;
    }

    return !IsShellRoutingWindow(hwnd);
}

void LogBlockedForegroundCommit(PCWSTR api, HWND hwnd, PCWSTR reason) {
    std::wstring className = GetWindowClassNameSafe(hwnd);
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=BLOCKED api=%s hwnd=0x%p class=%s reason=%s",
               api ? api : L"", hwnd, className.c_str(), reason ? reason : L"");
}

bool ShouldBlockMessage(HWND hwnd,
                        UINT msg,
                        WPARAM wParam,
                        LPARAM lParam,
                        PCWSTR* reason) {
    if (reason) {
        *reason = L"";
    }

    if (ShouldBlockThreeFingerLongPressSignal(hwnd, msg)) {
        if (reason) {
            *reason = L"Shell_TrayWnd 0x05C6 three-finger long-press action";
        }
        return true;
    }

    if (ShouldBlockWorkerRoutingToAppThumbnail(hwnd, msg, wParam, lParam)) {
        if (reason) {
            *reason = L"WorkerW 0xC029 -> AppThumbnailWindow";
        }
        return true;
    }

    if (ShouldBlockDirectAppThumbnailWindow(hwnd)) {
        if (reason) {
            *reason = L"active block window -> AppThumbnailWindow";
        }
        return true;
    }

    return false;
}

void ActivateFollowUpBlockers(HWND hwnd,
                              UINT msg,
                              WPARAM /*wParam*/,
                              LPARAM /*lParam*/,
                              PCWSTR reason) {
    if (ShouldBlockThreeFingerLongPressSignal(hwnd, msg)) {
        StartRecentTaskSwitcherBlockWindow(reason);
    }
}

void HideAppThumbnailWindow(HWND hwnd,
                            PCWSTR reason,
                            ShowWindowFn showWindowOriginal,
                            SetWindowPosFn setWindowPosOriginal) {
    if (!IsAppThumbnailWindow(hwnd)) {
        return;
    }

    if (showWindowOriginal) {
        showWindowOriginal(hwnd, SW_HIDE);
    }

    if (setWindowPosOriginal) {
        setWindowPosOriginal(hwnd, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HIDE_APP_THUMBNAIL hwnd=0x%p reason=%s", hwnd,
               reason ? reason : L"");
}

void HideMessageWindows(HWND hwnd,
                        LPARAM lParam,
                        PCWSTR api,
                        ShowWindowFn showWindowOriginal,
                        SetWindowPosFn setWindowPosOriginal) {
    HideAppThumbnailWindow(hwnd, api, showWindowOriginal, setWindowPosOriginal);
    HideAppThumbnailWindow(reinterpret_cast<HWND>(lParam), api,
                           showWindowOriginal, setWindowPosOriginal);
}

void LogBlockedMessage(PCWSTR api,
                       HWND hwnd,
                       UINT msg,
                       WPARAM wParam,
                       LPARAM lParam,
                       PCWSTR reason) {
    std::wstring className = GetWindowClassNameSafe(hwnd);
    HWND targetHwnd = reinterpret_cast<HWND>(lParam);
    std::wstring targetClassName = GetWindowClassNameSafe(targetHwnd);

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=BLOCKED api=%s hwnd=0x%p class=%s msg=0x%04X "
               L"wParam=0x%p lParam=0x%p targetHwnd=0x%p targetClass=%s "
               L"reason=%s",
               api ? api : L"", hwnd, className.c_str(), msg,
               reinterpret_cast<void*>(wParam), reinterpret_cast<void*>(lParam),
               targetHwnd, targetClassName.c_str(), reason ? reason : L"");
}

}  // namespace touchrev
