#include "ui/config/ConfigWindow.h"

#include "common/AppSettings.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "dwmapi.lib")

#if defined(TOUCHREV_BUILD_BLOCKER)
#include "injector/inject.h"
#include "injector/process_find.h"
#include "common/winutil.h"
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>

#include <tlhelp32.h>

namespace
{
constexpr wchar_t kWindowTitle[] = L"Touch-Rev 配置";

bool AppendThemeResources(winrt::Windows::UI::Xaml::ResourceDictionary const& resources)
{
    try
    {
        const std::wstring themeXaml = touchrev::common::LoadTextFileUtf8(
            touchrev::common::ModuleRelativePath(L"xaml/ThemeResources.xaml"));
        if (themeXaml.empty())
        {
            return false;
        }

        auto themeRes = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{themeXaml})
                            .as<winrt::Windows::UI::Xaml::ResourceDictionary>();
        resources.MergedDictionaries().Append(themeRes);
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}
}

bool ConfigWindow::Initialize(HINSTANCE instance, int showCommand)
{
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = ConfigWindow::WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = ConfigWindow::WindowClassName;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    // 如果未注册则进行注册
    RegisterClassExW(&windowClass);

    // 获取系统 DPI 并缩放 560x680 的精致窗口
    const UINT dpi = GetDpiForSystem();
    const int clientWidth = MulDiv(560, dpi, 96);
    const int clientHeight = MulDiv(680, dpi, 96);

    // 使用标准的 Win32 窗口样式，但禁用最大化按钮（我们限死了窗口大小）
    constexpr DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;

    RECT rect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenWidth - width) / 2;
    const int y = (screenHeight - height) / 2;

    hwnd_ = CreateWindowExW(
        0,
        ConfigWindow::WindowClassName,
        kWindowTitle,
        style,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_)
    {
        return false;
    }

    themeManager_.Initialize(hwnd_, true);
    UpdateXamlTheme();

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

void ConfigWindow::Show()
{
    if (hwnd_)
    {
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        SyncConfigToUi();
    }
}

void ConfigWindow::Hide()
{
    if (hwnd_)
    {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

LRESULT CALLBACK ConfigWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<ConfigWindow*>(createStruct->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    auto* app = reinterpret_cast<ConfigWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app)
    {
        return app->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}



LRESULT ConfigWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{


    switch (message)
    {
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"Registry") == 0)
        {
            if (themeManager_.Refresh(hwnd_))
            {
                UpdateXamlTheme();
            }
        }
        return 0;

    case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = GetDpiForWindow(hwnd_);
            const int clientWidth = MulDiv(560, dpi, 96);
            const int clientHeight = MulDiv(680, dpi, 96);

            RECT rect = { 0, 0, clientWidth, clientHeight };
            AdjustWindowRectExForDpi(&rect, GetWindowLongW(hwnd_, GWL_STYLE), FALSE, GetWindowLongW(hwnd_, GWL_EXSTYLE), dpi);
            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;

            mmi->ptMinTrackSize.x = width;
            mmi->ptMinTrackSize.y = height;
            mmi->ptMaxTrackSize.x = width;
            mmi->ptMaxTrackSize.y = height;
            return 0;
        }

    case WM_CREATE:
        if (FAILED(OnCreate()))
        {
            return -1;
        }
        return 0;

    case WM_SIZE:
        if (xamlHwnd_)
        {
            SetWindowPos(
                xamlHwnd_,
                nullptr,
                0,
                0,
                LOWORD(lParam),
                HIWORD(lParam),
                SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        return 0;

    case WM_DPICHANGED:
    {
        const auto suggestedRect = reinterpret_cast<RECT*>(lParam);
        if (suggestedRect)
        {
            SetWindowPos(
                hwnd_,
                nullptr,
                suggestedRect->left,
                suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_CLOSE:
        // 用户点击关闭按钮，仅隐藏配置窗口，不退出应用
        Hide();
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            if (themeManager_.Refresh(hwnd_))
            {
                UpdateXamlTheme();
            }
            SyncConfigToUi();
        }
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

HRESULT ConfigWindow::OnCreate()
{
    try
    {
        using namespace winrt::Windows::UI::Xaml::Hosting;
        using namespace winrt::Windows::UI::Xaml::Markup;

        // 初始化局部的 XAML Island 宿主
        xamlManager_ = WindowsXamlManager::InitializeForCurrentThread();
        xamlSource_ = DesktopWindowXamlSource();

        winrt::com_ptr<IDesktopWindowXamlSourceNative> nativeSource;
        auto* xamlSourceAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(xamlSource_));
        HRESULT hr = xamlSourceAbi->QueryInterface(kIDesktopWindowXamlSourceNative, nativeSource.put_void());
        if (FAILED(hr))
        {
            return hr;
        }

        hr = nativeSource->AttachToWindow(hwnd_);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = nativeSource->get_WindowHandle(&xamlHwnd_);
        if (FAILED(hr))
        {
            return hr;
        }



        // 开启窗口 Mica 材质与沉浸式扩展
        MARGINS margins = { -1, -1, -1, -1 };
        ::DwmExtendFrameIntoClientArea(hwnd_, &margins);

        #if defined(DWMWA_SYSTEMBACKDROP_TYPE)
        constexpr DWORD kDwmwaSystemBackdropType = DWMWA_SYSTEMBACKDROP_TYPE;
        #else
        constexpr DWORD kDwmwaSystemBackdropType = 38;
        #endif
        DWORD backdropType = 2; // Mica
        ::DwmSetWindowAttribute(hwnd_, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType));

        // 隐式实例化全局 Application 对象，以支持所有局部 XAML 在反序列化时的全局主题资源查询
        try
        {
            if (!winrt::Windows::UI::Xaml::Application::Current())
            {
                auto app = winrt::Windows::UI::Xaml::Application();
            }
            auto currentApp = winrt::Windows::UI::Xaml::Application::Current();
            if (currentApp)
            {
                AppendThemeResources(currentApp.Resources());
            }
        }
        catch (...)
        {
        }

        // 加载 ConfigRoot.xaml
        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(
            touchrev::common::ModuleRelativePath(L"xaml/ConfigRoot.xaml"));
        if (xaml.empty())
        {
            return E_FAIL;
        }

        auto object = XamlReader::Load(winrt::hstring{xaml});
        auto root = object.as<winrt::Windows::UI::Xaml::Controls::Grid>();
        root_ = root;
        UpdateXamlTheme();
        xamlSource_.Content(root_);

        // 检索 XAML 中的配置控件与状态栏
        hookStatusText_ = root.FindName(L"HookStatusText").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();



        // 检索 XAML 中的配置控件
        hookToggle_ = root.FindName(L"HookToggle").as<winrt::Windows::UI::Xaml::Controls::ToggleSwitch>();
        gestureToggle_ = root.FindName(L"GestureToggle").as<winrt::Windows::UI::Xaml::Controls::ToggleSwitch>();
        windowToggle_ = root.FindName(L"WindowToggle").as<winrt::Windows::UI::Xaml::Controls::ToggleSwitch>();
        exitButton_ = root.FindName(L"ExitButton").as<winrt::Windows::UI::Xaml::Controls::Button>();



        // 同步当前的配置状态到 UI
        SyncConfigToUi();

        // 绑定事件处理器，防振荡和死循环
        if (hookToggle_)
        {
            hookToggle_.Toggled([this](auto const&, auto const&) {
                if (isTogglingHook_) return;
                const bool target = hookToggle_.IsOn();
                if (target != QueryHookStatus())
                {
                    ToggleHook(target);
                }
            });
        }

        if (gestureToggle_)
        {
            gestureToggle_.Toggled([this](auto const&, auto const&) {
                touchrev::settings::g_IsGestureEnabled = gestureToggle_.IsOn();
            });
        }

        if (windowToggle_)
        {
            windowToggle_.Toggled([this](auto const&, auto const&) {
                touchrev::settings::g_IsSwitcherWindowEnabled = windowToggle_.IsOn();
            });
        }

        if (exitButton_)
        {
            exitButton_.Click([](auto const&, auto const&) {
                // 完全退出应用程序，通过发送退出消息退出消息循环
                PostQuitMessage(0);
            });
        }

        // 设置一秒的定时器，定时检测 Hook 状态
        SetTimer(hwnd_, 1, 1000, nullptr);

        // 获取并调整 initial 坐标
        RECT client{};
        GetClientRect(hwnd_, &client);
        SetWindowPos(
            xamlHwnd_,
            nullptr,
            0,
            0,
            client.right - client.left,
            client.bottom - client.top,
            SWP_NOZORDER | SWP_SHOWWINDOW);



        return S_OK;
    }
    catch (const winrt::hresult_error& error)
    {
        return error.code();
    }
}

void ConfigWindow::OnDestroy()
{
    KillTimer(hwnd_, 1);
    
    // 清理 XAML 引用
    hookStatusText_ = nullptr;
    hookToggle_ = nullptr;
    gestureToggle_ = nullptr;
    windowToggle_ = nullptr;
    exitButton_ = nullptr;

    if (xamlSource_)
    {
        xamlSource_.Content(nullptr);
        xamlSource_ = nullptr;
    }
    root_ = nullptr;
    xamlManager_ = nullptr;
    hwnd_ = nullptr;
    xamlHwnd_ = nullptr;
}

void ConfigWindow::SyncConfigToUi()
{
    // 用 isTogglingHook_ 标志作 UI 同步保护，防止触发 toggled 回调造成注入/反注入死循环
    isTogglingHook_ = true;

    const bool hookLoaded = QueryHookStatus();
    if (hookToggle_ && hookToggle_.IsOn() != hookLoaded)
    {
        hookToggle_.IsOn(hookLoaded);
    }

    if (gestureToggle_ && gestureToggle_.IsOn() != touchrev::settings::g_IsGestureEnabled)
    {
        gestureToggle_.IsOn(touchrev::settings::g_IsGestureEnabled);
    }

    if (windowToggle_ && windowToggle_.IsOn() != touchrev::settings::g_IsSwitcherWindowEnabled)
    {
        windowToggle_.IsOn(touchrev::settings::g_IsSwitcherWindowEnabled);
    }

    if (hookStatusText_)
    {
        const bool isDark = (themeManager_.Mode() == AppThemeMode::Dark);
        if (hookLoaded)
        {
            DWORD pdbStatus = 0;
            DWORD hookStatus = 0;
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Touch-Rev", 0, KEY_READ, &key) == ERROR_SUCCESS)
            {
                DWORD size = sizeof(pdbStatus);
                RegQueryValueExW(key, L"PdbStatus", nullptr, nullptr, reinterpret_cast<LPBYTE>(&pdbStatus), &size);
                size = sizeof(hookStatus);
                RegQueryValueExW(key, L"HookStatus", nullptr, nullptr, reinterpret_cast<LPBYTE>(&hookStatus), &size);
                RegCloseKey(key);
            }

            if (pdbStatus == 1)
            {
                hookStatusText_.Text(L"运行状态：PDB 符号下载中，正在拦截...");
                auto yellowColor = isDark ? winrt::Windows::UI::Color{255, 0xFF, 0xC8, 0x3C} : winrt::Windows::UI::Color{255, 0xE0, 0x80, 0x00};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(yellowColor));
            }
            else if (pdbStatus == 3)
            {
                hookStatusText_.Text(L"运行状态：无法下载 PDB 符号 (网络错误)，拦截已失效！");
                auto redColor = isDark ? winrt::Windows::UI::Color{255, 0xFF, 0x73, 0x73} : winrt::Windows::UI::Color{255, 0xD3, 0x2F, 0x2F};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(redColor));
            }
            else if (pdbStatus == 4)
            {
                hookStatusText_.Text(L"运行状态：本地 PDB 缓存已过期/损坏，正在重新下载...");
                auto yellowColor = isDark ? winrt::Windows::UI::Color{255, 0xFF, 0xC8, 0x3C} : winrt::Windows::UI::Color{255, 0xE0, 0x80, 0x00};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(yellowColor));
            }
            else if (hookStatus == 2)
            {
                hookStatusText_.Text(L"运行状态：Hook 符号补丁配置失效，拦截已失效！");
                auto redColor = isDark ? winrt::Windows::UI::Color{255, 0xFF, 0x73, 0x73} : winrt::Windows::UI::Color{255, 0xD3, 0x2F, 0x2F};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(redColor));
            }
            else if (hookStatus == 1 || pdbStatus == 2)
            {
                hookStatusText_.Text(L"运行状态：已拦截 Windows 默认手势 (运行中)");
                auto greenColor = isDark ? winrt::Windows::UI::Color{255, 0x4C, 0xE2, 0x8A} : winrt::Windows::UI::Color{255, 0x0F, 0x7B, 0x43};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(greenColor));
            }
            else
            {
                hookStatusText_.Text(L"运行状态：已注入拦截模块，正在初始化...");
                auto blueColor = isDark ? winrt::Windows::UI::Color{255, 0x33, 0xAA, 0xFF} : winrt::Windows::UI::Color{255, 0x00, 0x78, 0xD4};
                hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(blueColor));
            }
        }
        else
        {
            hookStatusText_.Text(L"运行状态：未拦截 (未注入)");
            auto grayColor = isDark ? winrt::Windows::UI::Color{255, 0xAA, 0xAA, 0xAA} : winrt::Windows::UI::Color{255, 0x66, 0x66, 0x66};
            hookStatusText_.Foreground(winrt::Windows::UI::Xaml::Media::SolidColorBrush(grayColor));
        }
    }

    isTogglingHook_ = false;
}

void ConfigWindow::ToggleHook(bool enable)
{
    isTogglingHook_ = true;

#if defined(TOUCHREV_BUILD_BLOCKER)
    std::wstring fullDllPath = touchrev::common::ModuleRelativePath(L"TouchRevBlockerHook.dll");

    std::wstring error;
    
    // 1. 确认 DLL 文件存在
    DWORD attributes = GetFileAttributesW(fullDllPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        MessageBoxW(hwnd_, (L"找不到 Hook DLL 文件:\n" + fullDllPath).c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    // 2. 校验 DLL 机器架构类型
    USHORT dllMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!touchrev::GetPeMachineType(fullDllPath, &dllMachine))
    {
        MessageBoxW(hwnd_, L"读取 DLL PE 机器类型失败。", L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    if (dllMachine != touchrev::CurrentBuildMachineType())
    {
        std::wstring msg = L"DLL 架构与当前构建架构不匹配。\n预期: " +
                           std::wstring(touchrev::CurrentBuildArchName()) +
                           L"\n实际: " + touchrev::MachineTypeToString(dllMachine);
        MessageBoxW(hwnd_, msg.c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    // 3. 校验当前进程是否为 native
    if (!touchrev::IsCurrentProcessNativeForCurrentBuild(&error))
    {
        MessageBoxW(hwnd_, (L"当前进程校验失败:\n" + error).c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    // 4. 查找资源管理器进程
    touchrev::TargetProcess target;
    if (!touchrev::FindShellExplorer(&target, &error))
    {
        MessageBoxW(hwnd_, (L"寻找资源管理器失败:\n" + error).c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    // 5. 校验目标进程架构是否匹配
    if (!touchrev::IsNativeMachineForCurrentBuild(target.processMachine, target.nativeMachine))
    {
        std::wstring msg = L"目标 explorer.exe 架构与当前构建架构不匹配。\n预期: " +
                           std::wstring(touchrev::CurrentBuildArchName()) +
                           L"\n进程架构: " + touchrev::MachineTypeToString(target.processMachine) +
                           L"\n原生架构: " + touchrev::MachineTypeToString(target.nativeMachine);
        MessageBoxW(hwnd_, msg.c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    // 6. 查询 DLL 加载状态
    touchrev::RemoteModuleInfo moduleInfo;
    if (!touchrev::QueryRemoteDllModule(target.pid, fullDllPath, &moduleInfo, &error))
    {
        MessageBoxW(hwnd_, (L"查询模块状态失败:\n" + error).c_str(), L"Touch-Rev 错误", MB_OK | MB_ICONERROR);
        isTogglingHook_ = false;
        return;
    }

    if (enable)
    {
        // 安装 Hook
        if (moduleInfo.module)
        {
            // 已加载，无需重复注入
            isTogglingHook_ = false;
            return;
        }

        HMODULE remoteModule = nullptr;
        if (!touchrev::InjectDllIntoProcess(target.pid, fullDllPath, &remoteModule, &error))
        {
            MessageBoxW(hwnd_, (L"注入 Hook DLL 失败:\n" + error).c_str(), L"Touch-Rev 注入失败", MB_OK | MB_ICONERROR);
        }
    }
    else
    {
        // 卸载 Hook
        if (!moduleInfo.module)
        {
            // 未加载，无需卸载
            isTogglingHook_ = false;
            return;
        }

        touchrev::RemoteModuleInfo unloadedModule;
        DWORD freeLibraryResult = 0;
        if (!touchrev::UninstallDllFromProcess(target.pid, fullDllPath, &unloadedModule, &freeLibraryResult, &error))
        {
            MessageBoxW(hwnd_, (L"卸载 Hook DLL 失败:\n" + error).c_str(), L"Touch-Rev 卸载失败", MB_OK | MB_ICONERROR);
        }
    }

#else
    MessageBoxW(hwnd_, L"未启用 Blocker 构建，无法执行 Hook 操作。", L"Touch-Rev 提示", MB_OK | MB_ICONWARNING);
#endif

    isTogglingHook_ = false;
}

bool ConfigWindow::QueryHookStatus()
{
    HWND shellTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shellTray)
    {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(shellTray, &pid);
    if (pid == 0)
    {
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, L"TouchRevHook.dll") == 0 ||
                _wcsicmp(entry.szModule, L"TouchRevBlockerHook.dll") == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void ConfigWindow::UpdateXamlTheme()
{
    if (!root_) return;
    using namespace winrt::Windows::UI::Xaml;

    const bool isDark = (themeManager_.Mode() == AppThemeMode::Dark);

    // 设置 RequestedTheme，自动从合并的官方字典中刷新整个界面的色彩
    root_.RequestedTheme(isDark ? ElementTheme::Dark : ElementTheme::Light);
}
