#include <windows.h>

#if !defined(TOUCHREV_ARCH_ARM64) && !defined(TOUCHREV_ARCH_X64)
#error TouchRevHook.dll must be built as ARM64 or x64.
#endif

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
