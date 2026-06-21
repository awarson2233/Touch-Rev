#include "hookdll/hooks.h"

#include "common/constants.h"
#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/direct_hook_resolver.h"
#include "hookdll/gesture_blocker.h"
#include "hookdll/twinui_gesture_hooks.h"

#include <detours.h>
#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>

namespace touchrev {
namespace {

using SendMessageWFn = LRESULT(WINAPI*)(HWND hwnd,
                                        UINT msg,
                                        WPARAM wParam,
                                        LPARAM lParam);
using PostMessageWFn = BOOL(WINAPI*)(HWND hwnd,
                                     UINT msg,
                                     WPARAM wParam,
                                     LPARAM lParam);
using SendMessageCallbackWFn = BOOL(WINAPI*)(HWND hwnd,
                                             UINT msg,
                                             WPARAM wParam,
                                             LPARAM lParam,
                                             SENDASYNCPROC resultCallback,
                                             ULONG_PTR data);
using SetForegroundWindowFn = BOOL(WINAPI*)(HWND hwnd);
using BringWindowToTopFn = BOOL(WINAPI*)(HWND hwnd);
using SwitchToThisWindowFn = void(WINAPI*)(HWND hwnd, BOOL altTab);

SendMessageWFn SendMessageW_Original = ::SendMessageW;
PostMessageWFn PostMessageW_Original = ::PostMessageW;
SendMessageCallbackWFn SendMessageCallbackW_Original = ::SendMessageCallbackW;
ShowWindowFn ShowWindow_Original = ::ShowWindow;
SetWindowPosFn SetWindowPos_Original = ::SetWindowPos;
SetForegroundWindowFn SetForegroundWindow_Original = ::SetForegroundWindow;
BringWindowToTopFn BringWindowToTop_Original = ::BringWindowToTop;
SwitchToThisWindowFn SwitchToThisWindow_Original = nullptr;

std::atomic_bool g_hooksInstalled{false};
std::atomic_bool g_multitaskingStateQueryResolved{false};
std::atomic<void*> g_multitaskingStateQueryAddress{nullptr};
thread_local bool g_inHook = false;

constexpr wchar_t kTwinuiPcshellModuleName[] = L"twinui.pcshell.dll";
constexpr wchar_t kMultitaskingViewStateQueryName[] =
    L"MultitaskingViewGestureHandler::GetMultitaskingViewState";
constexpr std::uintptr_t kMultitaskingViewStateQueryMaxBytes = 0x200;
constexpr std::uintptr_t kMultitaskingViewStateQuerySendMessageReturnRvas[] = {
    0x16192C,
    0xA9E6F4,
};

struct HookReentryScope {
    HookReentryScope() { g_inHook = true; }
    ~HookReentryScope() { g_inHook = false; }

    HookReentryScope(const HookReentryScope&) = delete;
    HookReentryScope& operator=(const HookReentryScope&) = delete;
};

SwitchToThisWindowFn ResolveSwitchToThisWindow() {
    DirectHookSpec spec{};
    spec.moduleName = L"user32.dll";
    spec.displayName = L"SwitchToThisWindow";
    spec.exportName = "SwitchToThisWindow";
    spec.optional = true;

    DirectHookTarget target{};
    if (!ResolveDirectHookTarget(spec, &target)) {
        return nullptr;
    }

    return reinterpret_cast<SwitchToThisWindowFn>(target.address);
}

void* ResolveMultitaskingViewStateQueryAddress() {
    if (!g_multitaskingStateQueryResolved.exchange(true)) {
        DirectHookSpec spec{};
        spec.moduleName = kTwinuiPcshellModuleName;
        spec.displayName = kMultitaskingViewStateQueryName;
        spec.symbolName = kMultitaskingViewStateQueryName;
        spec.optional = true;
        spec.requireExecutableAddress = true;

        DirectHookTarget target{};
        if (ResolveDirectHookTarget(spec, &target)) {
            g_multitaskingStateQueryAddress.store(target.address,
                                                  std::memory_order_release);
            LogMessage(L"hookdll", LogLevel::Info,
                       L"event=MULTITASKING_STATE_QUERY_RESOLVED addr=0x%p rva=0x%Ix",
                       target.address, target.rva);
        } else {
            LogMessage(L"hookdll", LogLevel::Warning,
                       L"event=MULTITASKING_STATE_QUERY_RESOLVE_FAILED");
        }
    }

    return g_multitaskingStateQueryAddress.load(std::memory_order_acquire);
}

bool TryComputeTwinuiRva(void* address, std::uintptr_t* rva) {
    if (rva) {
        *rva = 0;
    }
    if (!address) {
        return false;
    }

    HMODULE twinui = GetModuleHandleW(kTwinuiPcshellModuleName);
    if (!twinui) {
        return false;
    }

    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(twinui);
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(address);
    if (current < base) {
        return false;
    }

    if (rva) {
        *rva = current - base;
    }
    return true;
}

bool IsReturnAddressAtKnownMultitaskingStateQueryCallsite(void* returnAddress) {
    std::uintptr_t rva = 0;
    if (!TryComputeTwinuiRva(returnAddress, &rva)) {
        return false;
    }

    for (std::uintptr_t knownRva :
         kMultitaskingViewStateQuerySendMessageReturnRvas) {
        if (rva == knownRva) {
            return true;
        }
    }

    return false;
}

bool IsReturnAddressInMultitaskingViewStateQuery(void* returnAddress) {
    if (!returnAddress) {
        return false;
    }

    void* functionAddress = ResolveMultitaskingViewStateQueryAddress();
    if (functionAddress) {
        std::uintptr_t start = reinterpret_cast<std::uintptr_t>(functionAddress);
        std::uintptr_t current = reinterpret_cast<std::uintptr_t>(returnAddress);
        if (current >= start &&
            current < start + kMultitaskingViewStateQueryMaxBytes) {
            return true;
        }
    }

    return IsReturnAddressAtKnownMultitaskingStateQueryCallsite(returnAddress);
}

bool ShouldAllowShellMultitaskingStateQuery(HWND hwnd,
                                            UINT msg,
                                            WPARAM wParam,
                                            LPARAM lParam,
                                            void* returnAddress) {
    return msg == kShellTrayThreeFingerLongPressMessage && wParam == 0 &&
           lParam == 0 && WindowClassEquals(hwnd, kClassShellTrayWnd) &&
           IsReturnAddressInMultitaskingViewStateQuery(returnAddress);
}

LRESULT WINAPI SendMessageW_Hook(HWND hwnd,
                                 UINT msg,
                                 WPARAM wParam,
                                 LPARAM lParam) {
    void* returnAddress = _ReturnAddress();
    if (!g_inHook) {
        {
            HookReentryScope scope;
            bool allowShellMultitaskingStateQuery =
                ShouldAllowShellMultitaskingStateQuery(hwnd, msg, wParam, lParam,
                                                       returnAddress);
            PCWSTR reason = L"";
            if (ShouldBlockMessage(hwnd, msg, wParam, lParam,
                                   allowShellMultitaskingStateQuery, &reason)) {
                ActivateFollowUpBlockers(hwnd, msg, wParam, lParam,
                                         allowShellMultitaskingStateQuery, reason);
                HideMessageWindows(hwnd, lParam, L"SendMessageW",
                                   ShowWindow_Original, SetWindowPos_Original);
                LogBlockedMessage(L"SendMessageW", hwnd, msg, wParam, lParam,
                                  reason);
                return 0;
            }
        }
    }

    return SendMessageW_Original(hwnd, msg, wParam, lParam);
}

BOOL WINAPI PostMessageW_Hook(HWND hwnd,
                              UINT msg,
                              WPARAM wParam,
                              LPARAM lParam) {
    if (!g_inHook) {
        {
            HookReentryScope scope;
            PCWSTR reason = L"";
            if (ShouldBlockMessage(hwnd, msg, wParam, lParam, false, &reason)) {
                ActivateFollowUpBlockers(hwnd, msg, wParam, lParam, false, reason);
                HideMessageWindows(hwnd, lParam, L"PostMessageW",
                                   ShowWindow_Original, SetWindowPos_Original);
                LogBlockedMessage(L"PostMessageW", hwnd, msg, wParam, lParam,
                                  reason);
                return TRUE;
            }
        }
    }

    return PostMessageW_Original(hwnd, msg, wParam, lParam);
}

BOOL WINAPI SendMessageCallbackW_Hook(HWND hwnd,
                                      UINT msg,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      SENDASYNCPROC resultCallback,
                                      ULONG_PTR data) {
    if (!g_inHook) {
        {
            HookReentryScope scope;
            PCWSTR reason = L"";
            if (ShouldBlockMessage(hwnd, msg, wParam, lParam, false, &reason)) {
                ActivateFollowUpBlockers(hwnd, msg, wParam, lParam, false, reason);
                HideMessageWindows(hwnd, lParam, L"SendMessageCallbackW",
                                   ShowWindow_Original, SetWindowPos_Original);
                LogBlockedMessage(L"SendMessageCallbackW", hwnd, msg, wParam,
                                  lParam, reason);
                return TRUE;
            }
        }
    }

    return SendMessageCallbackW_Original(hwnd, msg, wParam, lParam,
                                         resultCallback, data);
}

BOOL WINAPI ShowWindow_Hook(HWND hwnd, int nCmdShow) {
    if (!g_inHook && ShouldBlockDirectAppThumbnailWindow(hwnd)) {
        HookReentryScope scope;
        HideAppThumbnailWindow(hwnd, L"ShowWindow", ShowWindow_Original,
                               SetWindowPos_Original);
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=BLOCKED api=ShowWindow hwnd=0x%p nCmdShow=%d",
                   hwnd, nCmdShow);
        return TRUE;
    }

    return ShowWindow_Original(hwnd, nCmdShow);
}

BOOL WINAPI SetWindowPos_Hook(HWND hwnd,
                              HWND hwndInsertAfter,
                              int x,
                              int y,
                              int cx,
                              int cy,
                              UINT flags) {
    if (!g_inHook && ShouldBlockDirectAppThumbnailWindow(hwnd)) {
        HookReentryScope scope;
        HideAppThumbnailWindow(hwnd, L"SetWindowPos", ShowWindow_Original,
                               SetWindowPos_Original);
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=BLOCKED api=SetWindowPos hwnd=0x%p flags=0x%08X",
                   hwnd, flags);
        return TRUE;
    }

    return SetWindowPos_Original(hwnd, hwndInsertAfter, x, y, cx, cy, flags);
}

BOOL WINAPI SetForegroundWindow_Hook(HWND hwnd) {
    if (!g_inHook && ShouldBlockForegroundCommitWindow(hwnd)) {
        HookReentryScope scope;
        LogBlockedForegroundCommit(L"SetForegroundWindow", hwnd,
                                   L"active block window -> foreground commit");
        return TRUE;
    }

    return SetForegroundWindow_Original(hwnd);
}

BOOL WINAPI BringWindowToTop_Hook(HWND hwnd) {
    if (!g_inHook && ShouldBlockForegroundCommitWindow(hwnd)) {
        HookReentryScope scope;
        LogBlockedForegroundCommit(L"BringWindowToTop", hwnd,
                                   L"active block window -> foreground commit");
        return TRUE;
    }

    return BringWindowToTop_Original(hwnd);
}

void WINAPI SwitchToThisWindow_Hook(HWND hwnd, BOOL altTab) {
    if (!g_inHook && ShouldBlockForegroundCommitWindow(hwnd)) {
        HookReentryScope scope;
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=BLOCKED api=SwitchToThisWindow hwnd=0x%p altTab=%d "
                   L"reason=active block window -> foreground commit",
                   hwnd, altTab);
        return;
    }

    SwitchToThisWindow_Original(hwnd, altTab);
}

bool AttachHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    LONG error = DetourAttach(targetPointer, hookPointer);
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=HOOK_ATTACH_FAILED api=%s error=%ld", apiName,
                   error);
        return false;
    }

    return true;
}

bool AttachOptionalHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    if (!targetPointer || !*targetPointer) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=HOOK_SKIPPED api=%s reason=missing-export", apiName);
        return true;
    }

    return AttachHook(targetPointer, hookPointer, apiName);
}

bool DetachHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    LONG error = DetourDetach(targetPointer, hookPointer);
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=HOOK_DETACH_FAILED api=%s error=%ld", apiName,
                   error);
        return false;
    }

    return true;
}

bool DetachOptionalHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    if (!targetPointer || !*targetPointer) {
        return true;
    }

    return DetachHook(targetPointer, hookPointer, apiName);
}

}  // namespace

bool InstallHooks() {
    bool expected = false;
    if (!g_hooksInstalled.compare_exchange_strong(expected, true)) {
        return true;
    }

    DetourRestoreAfterWith();
    SwitchToThisWindow_Original = ResolveSwitchToThisWindow();

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=HOOK_TRANSACTION_BEGIN_FAILED error=%ld", error);
        g_hooksInstalled.store(false);
        return false;
    }

    error = DetourUpdateThread(GetCurrentThread());
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=HOOK_UPDATE_THREAD_FAILED error=%ld", error);
        DetourTransactionAbort();
        g_hooksInstalled.store(false);
        return false;
    }

    bool ok = true;
    ok = AttachHook(reinterpret_cast<PVOID*>(&SendMessageW_Original),
                    reinterpret_cast<PVOID>(SendMessageW_Hook),
                    L"SendMessageW") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&PostMessageW_Original),
                    reinterpret_cast<PVOID>(PostMessageW_Hook),
                    L"PostMessageW") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&SendMessageCallbackW_Original),
                    reinterpret_cast<PVOID>(SendMessageCallbackW_Hook),
                    L"SendMessageCallbackW") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&ShowWindow_Original),
                    reinterpret_cast<PVOID>(ShowWindow_Hook), L"ShowWindow") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&SetWindowPos_Original),
                    reinterpret_cast<PVOID>(SetWindowPos_Hook), L"SetWindowPos") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&SetForegroundWindow_Original),
                    reinterpret_cast<PVOID>(SetForegroundWindow_Hook),
                    L"SetForegroundWindow") &&
         ok;
    ok = AttachHook(reinterpret_cast<PVOID*>(&BringWindowToTop_Original),
                    reinterpret_cast<PVOID>(BringWindowToTop_Hook),
                    L"BringWindowToTop") &&
         ok;
    ok = AttachOptionalHook(reinterpret_cast<PVOID*>(&SwitchToThisWindow_Original),
                            reinterpret_cast<PVOID>(SwitchToThisWindow_Hook),
                            L"SwitchToThisWindow") &&
         ok;

    if (!ok) {
        DetourTransactionAbort();
        g_hooksInstalled.store(false);
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=HOOK_TRANSACTION_COMMIT_FAILED error=%ld", error);
        g_hooksInstalled.store(false);
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=SendMessageW status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=PostMessageW status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=SendMessageCallbackW status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=ShowWindow status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=SetWindowPos status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=SetForegroundWindow status=OK");
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=HOOK_INSTALLED api=BringWindowToTop status=OK");
    if (SwitchToThisWindow_Original) {
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=HOOK_INSTALLED api=SwitchToThisWindow status=OK");
    }

    if (!InstallTwinuiGestureHooks()) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_INSTALL_FAILED_CONTINUING");
    }
    return true;
}

bool UninstallHooks() {
    bool expected = true;
    if (!g_hooksInstalled.compare_exchange_strong(expected, false)) {
        return true;
    }

    bool twinuiOk = UninstallTwinuiGestureHooks();

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=UNHOOK_TRANSACTION_BEGIN_FAILED error=%ld", error);
        return false;
    }

    error = DetourUpdateThread(GetCurrentThread());
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=UNHOOK_UPDATE_THREAD_FAILED error=%ld", error);
        DetourTransactionAbort();
        return false;
    }

    bool ok = true;
    ok = DetachHook(reinterpret_cast<PVOID*>(&SendMessageW_Original),
                    reinterpret_cast<PVOID>(SendMessageW_Hook),
                    L"SendMessageW") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&PostMessageW_Original),
                    reinterpret_cast<PVOID>(PostMessageW_Hook),
                    L"PostMessageW") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&SendMessageCallbackW_Original),
                    reinterpret_cast<PVOID>(SendMessageCallbackW_Hook),
                    L"SendMessageCallbackW") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&ShowWindow_Original),
                    reinterpret_cast<PVOID>(ShowWindow_Hook), L"ShowWindow") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&SetWindowPos_Original),
                    reinterpret_cast<PVOID>(SetWindowPos_Hook), L"SetWindowPos") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&SetForegroundWindow_Original),
                    reinterpret_cast<PVOID>(SetForegroundWindow_Hook),
                    L"SetForegroundWindow") &&
         ok;
    ok = DetachHook(reinterpret_cast<PVOID*>(&BringWindowToTop_Original),
                    reinterpret_cast<PVOID>(BringWindowToTop_Hook),
                    L"BringWindowToTop") &&
         ok;
    ok = DetachOptionalHook(reinterpret_cast<PVOID*>(&SwitchToThisWindow_Original),
                            reinterpret_cast<PVOID>(SwitchToThisWindow_Hook),
                            L"SwitchToThisWindow") &&
         ok;

    if (!ok) {
        DetourTransactionAbort();
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=UNHOOK_TRANSACTION_COMMIT_FAILED error=%ld", error);
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info, L"event=HOOKS_UNINSTALLED");
    return twinuiOk;
}

}  // namespace touchrev
