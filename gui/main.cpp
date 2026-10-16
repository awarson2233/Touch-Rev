#include "hook/HookController.h"
#include "ui/switcher/MainWindow.h"
#include "ui/config/ConfigWindow.h"
#include "ui/shared/TrayIcon.h"
#include "common/AppSettings.h"
#include "common/AutoStart.h"
#include "common/Elevation.h"
#include "common/Win32Error.h"

#include <windows.h>
#include <roapi.h>

#include <string>

namespace
{
// 由旧实例以新权限拉起：旧实例此时仍持有单实例锁、正在退出，需要等它释放而不是转发命令。
constexpr wchar_t kRelaunchArgument[] = L"--relaunch";

bool HasArgument(PCWSTR argument)
{
    return std::wstring(GetCommandLineW()).find(argument) != std::wstring::npos;
}

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

    // 清单已声明 DPI 感知时再次设置返回 ERROR_ACCESS_DENIED，属正常情况，不记录。
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        GetLastError() != ERROR_ACCESS_DENIED)
    {
        DebugLog(L"SetProcessDpiAwarenessContext failed; continuing with process default DPI awareness.");
    }

    const MainWindow::ActivationCommand command = ParseActivationCommand();
    const bool relaunched = HasArgument(kRelaunchArgument);
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, MainWindow::SingleInstanceMutexName);
    const DWORD mutexError = GetLastError();
    // 已提权的实例创建的锁带高完整性标签，普通权限进程打开会得到 ERROR_ACCESS_DENIED，同样说明已有实例。
    bool otherInstance = singleInstanceMutex ? mutexError == ERROR_ALREADY_EXISTS : mutexError == ERROR_ACCESS_DENIED;
    if (otherInstance && relaunched && singleInstanceMutex)
    {
        const DWORD wait = WaitForSingleObject(singleInstanceMutex, 15000);
        otherInstance = wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED;
    }
    if (!singleInstanceMutex && !otherInstance)
    {
        DebugLogHResult(L"CreateMutexW", HRESULT_FROM_WIN32(mutexError));
    }
    else if (otherInstance)
    {
        const bool redirected = RedirectToExistingInstance(command);
        if (singleInstanceMutex)
        {
            CloseHandle(singleInstanceMutex);
        }
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return redirected ? 0 : -1;
    }

    const auto cleanup = [&](int code) {
        if (singleInstanceMutex)
        {
            ReleaseMutex(singleInstanceMutex);
            CloseHandle(singleInstanceMutex);
        }
        if (shouldRoUninitialize)
        {
            RoUninitialize();
        }
        return code;
    };

    // 没有在运行的实例可退出：直接结束，不创建窗口，更不能按启动策略去注入 explorer。
    if (command == MainWindow::ActivationCommand::Exit)
    {
        return cleanup(0);
    }

    // 配置先于窗口载入，手势开关等状态在首帧就是正确的。
    touchrev::settings::Load();

    // 要求以管理员身份运行时自我提权：新实例带 --relaunch 等本进程退出后接管单实例锁。
    // 用户在 UAC 里取消就以普通权限继续，只是拖不动管理员窗口。
    bool elevationDeclined = false;
    if (touchrev::settings::Current().runAsAdmin && !touchrev::common::elevation::IsElevated() && !relaunched)
    {
        std::wstring arguments = kRelaunchArgument;
        if (command == MainWindow::ActivationCommand::Hide)
        {
            arguments += L" --hide";
        }
        else if (command == MainWindow::ActivationCommand::Toggle)
        {
            arguments += L" --toggle";
        }
        if (touchrev::common::elevation::RelaunchElevated(arguments))
        {
            return cleanup(0);
        }
        elevationDeclined = true;
    }

    // 切换过"以管理员身份运行"后，把开机自启改写成对应形式（Run 项 / 计划任务）。
    touchrev::common::autostart::Sync();

    MainWindow app;
    ConfigWindow config;
    touchrev::ui::TrayIcon tray;
    touchrev::hook::HookController hook;

    // 拦截操作在后台线程完成后回到这里：刷新界面；用户在控制中心发起的操作弹框反馈，其余走托盘气泡。
    const bool hookReady = hook.Initialize(instance, [&config, &tray](const touchrev::hook::OperationResult& result) {
        config.RefreshHookUi();
        if (result.message.empty())
        {
            return;
        }

        const bool isError = !result.succeeded;
        if (result.userInitiated && config.IsVisible())
        {
            MessageBoxW(config.Hwnd(), result.message.c_str(), L"Touch-Rev",
                        MB_OK | (isError ? MB_ICONERROR : MB_ICONINFORMATION));
        }
        else
        {
            tray.Notify(L"Touch-Rev", result.message, isError);
        }
    });
    if (!hookReady)
    {
        DebugLog(L"HookController initialization failed; hook operations are unavailable.");
    }

    if (!app.Initialize(instance, SW_HIDE))
    {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        hook.Shutdown(false);
        return cleanup(-1);
    }

    config.SetHookController(&hook);
    // 所有退出入口统一走 ExitApplication，保证窗口与 XAML 按序拆除。
    config.SetExitCallback([&app]() { app.ExitApplication(); });
    if (!config.Initialize(instance, command == MainWindow::ActivationCommand::Hide ? SW_HIDE : SW_SHOWNORMAL))
    {
        MessageBoxW(nullptr, L"Failed to create the config window.", L"TouchRevGUI", MB_ICONERROR | MB_OK);
        hook.Shutdown(false);
        return cleanup(-1);
    }

    app.SetConfigVisibilityCallback([&config](bool visible) {
        if (visible)
        {
            config.Show();
        }
        else
        {
            config.Hide();
        }
    });

    const auto notifyHookBusy = [&tray]() {
        tray.Notify(L"Touch-Rev", L"上一个拦截操作仍在进行，请稍候。");
    };

    touchrev::ui::TrayCallbacks trayCallbacks;
    trayCallbacks.onOpenConfig = [&config]() { config.Show(); };
    trayCallbacks.onQueryHookEnabled = [&hook]() { return hook.IsEffective(); };
    trayCallbacks.onQueryHookBusy = [&hook]() { return hook.IsBusy(); };
    trayCallbacks.onToggleHook = [&hook, &config, notifyHookBusy](bool enable) {
        const bool accepted = enable ? hook.RequestEnable(true) : hook.RequestDisable(true);
        if (!accepted)
        {
            notifyHookBusy();
        }
        config.RefreshHookUi();
    };
    trayCallbacks.onClearPdbCache = [&hook, &config, notifyHookBusy]() {
        if (!hook.RequestClearCache(true))
        {
            notifyHookBusy();
        }
        config.RefreshHookUi();
    };
    trayCallbacks.onExit = [&app]() { app.ExitApplication(); };
    trayCallbacks.onTaskbarCreated = [&hook, &config]() {
        hook.OnExplorerRestarted();
        config.RefreshHookUi();
    };
    if (!tray.Initialize(instance, std::move(trayCallbacks)))
    {
        DebugLog(L"Tray icon initialization failed; continuing without a tray entry.");
    }
    if (elevationDeclined)
    {
        tray.Notify(L"Touch-Rev", L"未获得管理员权限，当前以普通权限运行，无法拖动或切换任务管理器等管理员窗口。");
    }

    switch (command)
    {
    case MainWindow::ActivationCommand::Hide:
        config.Hide();
        app.Hide();
        break;
    case MainWindow::ActivationCommand::Toggle:
        app.ToggleSwitcher();
        break;
    case MainWindow::ActivationCommand::Show:
    case MainWindow::ActivationCommand::Exit:
        config.Show();
        break;
    }

    // 注入在后台线程执行，不阻塞首个消息循环；失败通过托盘气泡反馈。
    hook.ApplyStartupPolicy(touchrev::settings::Current().hookAutoInstall);
    config.RefreshHookUi();

    const int result = app.Run();
    tray.Shutdown();
    // 退出时卸载拦截，恢复 Windows 默认三指手势并释放 explorer 对 DLL 的占用。
    hook.Shutdown(true);
    return cleanup(result);
}
