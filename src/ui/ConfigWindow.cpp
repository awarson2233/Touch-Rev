#include "ui/ConfigWindow.h"

#include "common/AppSettings.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"

#if defined(TOUCHREV_BUILD_BLOCKER)
#include "injector/inject.h"
#include "injector/process_find.h"
#include "common/winutil.h"
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>

#include <tlhelp32.h>

struct IDesktopWindowXamlSourceNative : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AttachToWindow(HWND parentWnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hWnd) = 0;
};

namespace
{
constexpr GUID kIDesktopWindowXamlSourceNative = {
    0x3cbcf1bf,
    0x2f76,
    0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54},
};

constexpr wchar_t kWindowTitle[] = L"Touch-Rev 配置";
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

    // 使用非大小调整的重叠窗口
    constexpr DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME;

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

    themeManager_.Initialize(hwnd_);

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

        // 加载 ConfigRoot.xaml
        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(
            touchrev::common::ModuleRelativePath(L"xaml/ConfigRoot.xaml"));
        if (xaml.empty())
        {
            return E_FAIL;
        }

        auto object = XamlReader::Load(winrt::hstring{xaml});
        auto root = object.as<winrt::Windows::UI::Xaml::Controls::Grid>();
        xamlSource_.Content(root);

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
    hookToggle_ = nullptr;
    gestureToggle_ = nullptr;
    windowToggle_ = nullptr;
    exitButton_ = nullptr;

    if (xamlSource_)
    {
        xamlSource_.Content(nullptr);
        xamlSource_ = nullptr;
    }
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
