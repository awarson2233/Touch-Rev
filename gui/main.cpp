#include "ui/MainWindow.h"
#include "ui/ConfigWindow.h"
#include "common/Win32Error.h"

#include <windows.h>
#include <roapi.h>

#include <string>

namespace
{
MainWindow::ActivationCommand ParseActivationCommand()
{
    const std::wstring commandLine = GetCommandLineW();
    if (commandLine.find(L"--exit") != std::wstring::npos)
    {
        return MainWindow::ActivationCommand::Exit;
    }
    if (commandLine.find(L"--hide") != std::wstring::npos)
    {
        return MainWindow::ActivationCommand::Hide;
    }
    if (commandLine.find(L"--toggle") != std::wstring::npos)
    {
        return MainWindow::ActivationCommand::Toggle;
    }
    return MainWindow::ActivationCommand::Show;
}

HWND FindExistingAppWindow()
{
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        HWND hwnd = FindWindowW(MainWindow::WindowClassName, nullptr);
        if (hwnd)
        {
            return hwnd;
        }
        Sleep(20);
    }
    return nullptr;
}

bool RedirectToExistingInstance(MainWindow::ActivationCommand command)
{
    HWND hwnd = FindExistingAppWindow();
    if (!hwnd)
    {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != 0)
    {
        AllowSetForegroundWindow(processId);
    }

    const UINT wakeMessage = RegisterWindowMessageW(MainWindow::WakeMessageName);
    if (wakeMessage == 0)
    {
        DebugLogHResult(L"RegisterWindowMessageW", HResultFromLastError());
        return false;
    }

    if (!PostMessageW(hwnd, wakeMessage, static_cast<WPARAM>(command), 0))
    {
        DebugLogHResult(L"PostMessageW(TouchRevGUI.Wake)", HResultFromLastError());
        return false;
    }

    return true;
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int /*showCommand*/)
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

    const MainWindow::ActivationCommand command = ParseActivationCommand();
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, MainWindow::SingleInstanceMutexName);
    if (!singleInstanceMutex)
    {
        DebugLogHResult(L"CreateMutexW", HResultFromLastError());
    }
    else if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        const bool redirected = RedirectToExistingInstance(command);
        CloseHandle(singleInstanceMutex);
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return redirected ? 0 : -1;
    }

    MainWindow app;
    if (!app.Initialize(instance, SW_HIDE))
    {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        if (singleInstanceMutex)
        {
            CloseHandle(singleInstanceMutex);
        }
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return -1;
    }

    ConfigWindow config;
    if (!config.Initialize(instance, command == MainWindow::ActivationCommand::Hide ? SW_HIDE : SW_SHOWNORMAL))
    {
        MessageBoxW(nullptr, L"Failed to create the config window.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        if (singleInstanceMutex)
        {
            CloseHandle(singleInstanceMutex);
        }
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return -1;
    }

    switch (command)
    {
    case MainWindow::ActivationCommand::Hide:
        config.Hide();
        app.Hide();
        break;
    case MainWindow::ActivationCommand::Toggle:
    case MainWindow::ActivationCommand::Show:
        config.Show();
        break;
    case MainWindow::ActivationCommand::Exit:
        app.ExitApplication();
        break;
    }

    const int result = app.Run();

    if (singleInstanceMutex)
    {
        ReleaseMutex(singleInstanceMutex);
        CloseHandle(singleInstanceMutex);
    }

    if (shouldRoUninitialize)
    {
        RoUninitialize();
    }

    return result;
}
