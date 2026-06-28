#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/twinui_gesture_table_patch.h"
#include "hookdll/dbghelp_symbol_provider.h"

#include <windows.h>

#if !defined(TOUCHREV_ARCH_ARM64) && !defined(TOUCHREV_ARCH_X64)
#error TouchRevHook.dll must be built as ARM64 or x64.
#endif

namespace touchrev {
void UpdateRegistryStatus(unsigned long pdbStatus, unsigned long hookStatus) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Touch-Rev", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        DWORD dwPdb = pdbStatus;
        DWORD dwHook = hookStatus;
        ::RegSetValueExW(key, L"PdbStatus", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwPdb), sizeof(dwPdb));
        ::RegSetValueExW(key, L"HookStatus", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwHook), sizeof(dwHook));
        ::RegCloseKey(key);
    }
}
}

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
        touchrev::UpdateRegistryStatus(0, 0);
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
        touchrev::UpdateRegistryStatus(0, 0);
        touchrev::ShutdownDbgHelpSession();
        touchrev::ShutdownLog();
        break;
    default:
        break;
    }

    return TRUE;
}
