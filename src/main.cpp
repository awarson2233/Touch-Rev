#include "ui/AppWindow.h"
#include "common/Win32Error.h"

#include <windows.h>
#include <roapi.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    const HRESULT roInit = RoInitialize(RO_INIT_SINGLETHREADED);
    const bool shouldRoUninitialize = SUCCEEDED(roInit);
    if (FAILED(roInit) && roInit != RPC_E_CHANGED_MODE)
    {
        MessageBoxW(nullptr, L"WinRT initialization failed.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
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
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return -1;
    }

    const int result = app.Run();

    if (shouldRoUninitialize)
    {
        RoUninitialize();
    }

    return result;
}
