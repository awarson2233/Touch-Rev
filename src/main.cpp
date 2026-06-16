#include "AppWindow.h"
#include "Win32Error.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldCoUninitialize = SUCCEEDED(coInit);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
    {
        MessageBoxW(nullptr, L"COM initialization failed.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        return -1;
    }

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        DebugLog(L"SetProcessDpiAwarenessContext failed; continuing with process default DPI awareness.");
    }

    AppWindow app;
    if (!app.Initialize(instance, showCommand))
    {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        if (shouldCoUninitialize)
        {
            CoUninitialize();
        }
        return -1;
    }

    const int result = app.Run();

    if (shouldCoUninitialize)
    {
        CoUninitialize();
    }

    return result;
}
