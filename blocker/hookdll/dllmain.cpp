#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/twinui_gesture_table_patch.h"

#include <windows.h>

#if !defined(TOUCHREV_ARCH_ARM64) && !defined(TOUCHREV_ARCH_X64)
#error TouchRevHook.dll must be built as ARM64 or x64.
#endif

namespace {

DWORD WINAPI InitializeHookThread(LPVOID module) {
    touchrev::InitializeLog(L"hookdll");
    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=INIT arch=%s module=0x%p",
                         touchrev::CurrentBuildArchName(), module);

    if (touchrev::InstallTwinuiGestureTablePatch()) {
        touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                             L"event=READY");
    } else {
        touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Error,
                             L"event=READY_FAILED");
    }

    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitializeHookThread, module, 0,
                                     nullptr);
        if (thread) {
            CloseHandle(thread);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (reserved == nullptr) {
            touchrev::UninstallTwinuiGestureTablePatch();
        }
        touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                             L"event=UNINIT processTerminating=%d",
                             reserved != nullptr);
        touchrev::ShutdownLog();
        break;
    default:
        break;
    }

    return TRUE;
}
