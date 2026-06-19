#include "hookdll/twinui_gesture_hooks.h"

#include "common/log.h"
#include "hookdll/direct_hook_resolver.h"
#include "hookdll/gesture_blocker.h"

#include <detours.h>
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>

namespace touchrev {
namespace {

#if defined(TOUCHREV_ARCH_ARM64) || defined(TOUCHREV_ARCH_X64)

constexpr wchar_t kTwinuiPcshellModuleName[] = L"twinui.pcshell.dll";
constexpr wchar_t kTouchGestureProcessorStartSwipeName[] =
    L"TouchGestureProcessor::StartSwipe";

struct TouchGesturePointView {
    float deltaX;
    float deltaY;
};

static_assert(offsetof(TouchGesturePointView, deltaY) == 4);

using TouchGestureProcessorStartSwipeFn = void(WINAPI*)(
    void* self,
    unsigned int fingerCount,
    const TouchGesturePointView* point);

TouchGestureProcessorStartSwipeFn TouchGestureProcessorStartSwipe_Original = nullptr;
std::atomic_bool g_twinuiHooksInstalled{false};
thread_local bool g_inTwinuiHook = false;

void ClearTwinuiOriginals() {
    TouchGestureProcessorStartSwipe_Original = nullptr;
}

bool IsAnyThreeFingerSwipe(int fingerCount,
                           float deltaX,
                           float deltaY,
                           int* dx,
                           int* dy) {
    if (dx) {
        *dx = 0;
    }
    if (dy) {
        *dy = 0;
    }

    if (fingerCount != 3 || !std::isfinite(deltaX) ||
        !std::isfinite(deltaY)) {
        return false;
    }

    int currentDx = static_cast<int>(deltaX);
    int currentDy = static_cast<int>(deltaY);
    if (dx) {
        *dx = currentDx;
    }
    if (dy) {
        *dy = currentDy;
    }

    return true;
}

void WINAPI TouchGestureProcessorStartSwipe_Hook(
    void* self,
    unsigned int fingerCount,
    const TouchGesturePointView* point) {
    int dx = 0;
    int dy = 0;
    bool shouldBlock =
        point && IsAnyThreeFingerSwipe(static_cast<int>(fingerCount), point->deltaX,
                                       point->deltaY, &dx, &dy);
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TRACE api=twinui::TouchGestureProcessor::StartSwipe "
               L"fingerCount=%u dx=%d dy=%d block=%d",
               fingerCount, dx, dy, shouldBlock ? 1 : 0);

    if (!g_inTwinuiHook && shouldBlock) {
        g_inTwinuiHook = true;
        StartThreeFingerSwipeUpBlockWindow();
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=BLOCKED "
                   L"api=twinui::TouchGestureProcessor::StartSwipe "
                   L"fingerCount=%u dx=%d dy=%d "
                   L"reason=any-three-finger-swipe",
                   fingerCount, dx, dy);
        g_inTwinuiHook = false;
        return;
    }

    if (TouchGestureProcessorStartSwipe_Original) {
        TouchGestureProcessorStartSwipe_Original(self, fingerCount, point);
    }
}

bool AttachTwinuiHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    LONG error = DetourAttach(targetPointer, hookPointer);
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_ATTACH_FAILED api=%s error=%ld", apiName,
                   error);
        return false;
    }

    return true;
}

bool DetachTwinuiHook(PVOID* targetPointer, PVOID hookPointer, PCWSTR apiName) {
    LONG error = DetourDetach(targetPointer, hookPointer);
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_UNHOOK_DETACH_FAILED api=%s error=%ld", apiName,
                   error);
        return false;
    }

    return true;
}

#endif  // TOUCHREV_ARCH_ARM64 || TOUCHREV_ARCH_X64

}  // namespace

bool InstallTwinuiGestureHooks() {
#if defined(TOUCHREV_ARCH_ARM64) || defined(TOUCHREV_ARCH_X64)
    bool expected = false;
    if (!g_twinuiHooksInstalled.compare_exchange_strong(expected, true)) {
        return true;
    }

    DirectHookSpec startSwipeSpec{};
    startSwipeSpec.moduleName = kTwinuiPcshellModuleName;
    startSwipeSpec.displayName = kTouchGestureProcessorStartSwipeName;
    startSwipeSpec.symbolName = kTouchGestureProcessorStartSwipeName;
    startSwipeSpec.requireExecutableAddress = true;

    DirectHookTarget startSwipe{};
    if (!ResolveDirectHookTarget(startSwipeSpec, &startSwipe)) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_FAILED api=%s reason=symbol-resolve-failed",
                   kTouchGestureProcessorStartSwipeName);
        g_twinuiHooksInstalled.store(false);
        return false;
    }

    TouchGestureProcessorStartSwipe_Original =
        reinterpret_cast<TouchGestureProcessorStartSwipeFn>(startSwipe.address);

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_TRANSACTION_BEGIN_FAILED error=%ld", error);
        ClearTwinuiOriginals();
        g_twinuiHooksInstalled.store(false);
        return false;
    }

    error = DetourUpdateThread(GetCurrentThread());
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_UPDATE_THREAD_FAILED error=%ld", error);
        DetourTransactionAbort();
        ClearTwinuiOriginals();
        g_twinuiHooksInstalled.store(false);
        return false;
    }

    bool ok = AttachTwinuiHook(
        reinterpret_cast<PVOID*>(&TouchGestureProcessorStartSwipe_Original),
        reinterpret_cast<PVOID>(TouchGestureProcessorStartSwipe_Hook),
        kTouchGestureProcessorStartSwipeName);

    if (!ok) {
        DetourTransactionAbort();
        ClearTwinuiOriginals();
        g_twinuiHooksInstalled.store(false);
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_HOOK_TRANSACTION_COMMIT_FAILED error=%ld", error);
        ClearTwinuiOriginals();
        g_twinuiHooksInstalled.store(false);
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_HOOK_INSTALLED api=%s module=0x%p source=%s "
               L"rva=0x%Ix hint=%s path=%s",
               kTouchGestureProcessorStartSwipeName, startSwipe.module,
               DirectHookResolveSourceName(startSwipe.source), startSwipe.rva,
               startSwipe.hint ? startSwipe.hint : L"", startSwipe.modulePath.c_str());
    return true;
#else
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_HOOK_SKIPPED api=twinui.pcshell "
               L"reason=unsupported-build-architecture");
    return true;
#endif
}

bool UninstallTwinuiGestureHooks() {
#if defined(TOUCHREV_ARCH_ARM64) || defined(TOUCHREV_ARCH_X64)
    bool expected = true;
    if (!g_twinuiHooksInstalled.compare_exchange_strong(expected, false)) {
        return true;
    }

    if (!TouchGestureProcessorStartSwipe_Original) {
        return true;
    }

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_UNHOOK_TRANSACTION_BEGIN_FAILED error=%ld", error);
        g_twinuiHooksInstalled.store(true);
        return false;
    }

    error = DetourUpdateThread(GetCurrentThread());
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_UNHOOK_UPDATE_THREAD_FAILED error=%ld", error);
        DetourTransactionAbort();
        g_twinuiHooksInstalled.store(true);
        return false;
    }

    bool ok = DetachTwinuiHook(
        reinterpret_cast<PVOID*>(&TouchGestureProcessorStartSwipe_Original),
        reinterpret_cast<PVOID>(TouchGestureProcessorStartSwipe_Hook),
        kTouchGestureProcessorStartSwipeName);

    if (!ok) {
        DetourTransactionAbort();
        g_twinuiHooksInstalled.store(true);
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_UNHOOK_TRANSACTION_COMMIT_FAILED error=%ld", error);
        g_twinuiHooksInstalled.store(true);
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_HOOK_UNINSTALLED api=%s",
               kTouchGestureProcessorStartSwipeName);
    ClearTwinuiOriginals();
    return true;
#else
    return true;
#endif
}

}  // namespace touchrev
