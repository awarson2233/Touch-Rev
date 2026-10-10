#include "ui/config/ConfigWindow.h"

#include "common/AppSettings.h"
#include "common/AutoStart.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"
#include "common/PdbCache.h"
#include "common/RegistryUtils.h"
#include "common/Win32Error.h"
#include "hook/HookController.h"
#include "ui/shared/AppIcon.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr wchar_t kWindowTitle[] = L"Touch-Rev 控制中心";
constexpr int kClientWidthDip = 520;
constexpr int kClientHeightDip = 660;
constexpr UINT_PTR kRefreshTimerId = 1;

constexpr DWORD kPdbStatusNone = 0;
constexpr DWORD kPdbStatusResolving = 1;
constexpr DWORD kPdbStatusLoaded = 2;
constexpr DWORD kPdbStatusDownloadFailed = 3;
constexpr DWORD kPdbStatusIdentityMismatch = 4;

constexpr DWORD kPdbSourceNone = 0;
constexpr DWORD kPdbSourceCache = 1;
constexpr DWORD kPdbSourceDownload = 2;
constexpr DWORD kPdbSourceExternal = 3;

constexpr DWORD kHookStatusNone = 0;
constexpr DWORD kHookStatusInstalled = 1;
constexpr DWORD kHookStatusSymbolResolveFailed = 2;
constexpr DWORD kHookStatusRangeValidationFailed = 3;
constexpr DWORD kHookStatusNoMatchingEntries = 4;
constexpr DWORD kHookStatusWriteFailed = 5;

enum class StatusVisual {
    Gray,
    Blue,
    Green,
    Yellow,
    Red,
};

struct BlockerRuntimeStatus {
    DWORD pdbStatus = kPdbStatusNone;
    DWORD pdbSource = kPdbSourceNone;
    DWORD hookStatus = kHookStatusNone;
    DWORD pdbLastError = ERROR_SUCCESS;
    DWORD hookLastError = ERROR_SUCCESS;
};

winrt::Windows::UI::Color StatusColor(bool isDark, StatusVisual visual)
{
    switch (visual)
    {
    case StatusVisual::Blue:
        return isDark ? winrt::Windows::UI::Color{255, 0x33, 0xAA, 0xFF}
                      : winrt::Windows::UI::Color{255, 0x00, 0x78, 0xD4};
    case StatusVisual::Green:
        return isDark ? winrt::Windows::UI::Color{255, 0x4C, 0xE2, 0x8A}
                      : winrt::Windows::UI::Color{255, 0x0F, 0x7B, 0x43};
    case StatusVisual::Yellow:
        return isDark ? winrt::Windows::UI::Color{255, 0xFF, 0xC8, 0x3C}
                      : winrt::Windows::UI::Color{255, 0xE0, 0x80, 0x00};
    case StatusVisual::Red:
        return isDark ? winrt::Windows::UI::Color{255, 0xFF, 0x73, 0x73}
                      : winrt::Windows::UI::Color{255, 0xD3, 0x2F, 0x2F};
    case StatusVisual::Gray:
    default:
        return isDark ? winrt::Windows::UI::Color{255, 0xAA, 0xAA, 0xAA}
                      : winrt::Windows::UI::Color{255, 0x66, 0x66, 0x66};
    }
}

BlockerRuntimeStatus ReadBlockerRuntimeStatus()
{
    namespace registry = touchrev::common::registry;
    constexpr wchar_t kStatusKey[] = L"Software\\Touch-Rev";

    BlockerRuntimeStatus status{};
    status.pdbStatus = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"PdbStatus", kPdbStatusNone);
    status.pdbSource = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"PdbSource", kPdbSourceNone);
    status.hookStatus = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"HookStatus", kHookStatusNone);
    status.pdbLastError = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"PdbLastError", ERROR_SUCCESS);
    status.hookLastError = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"HookLastError", ERROR_SUCCESS);
    return status;
}

std::wstring PdbSourceText(DWORD source)
{
    switch (source)
    {
    case kPdbSourceCache:
        return L"本地缓存";
    case kPdbSourceDownload:
        return L"本次下载";
    case kPdbSourceExternal:
        return L"外部符号路径";
    case kPdbSourceNone:
    default:
        return L"未知来源";
    }
}

// 依据 PDB / Hook 两级状态给出面向用户的一句话结论与指示灯颜色。
std::pair<std::wstring, StatusVisual> DescribeStatus(bool hookLoaded)
{
    if (!hookLoaded)
    {
        return {L"运行状态：未拦截系统默认手势", StatusVisual::Gray};
    }

    const BlockerRuntimeStatus status = ReadBlockerRuntimeStatus();
    switch (status.pdbStatus)
    {
    case kPdbStatusDownloadFailed:
        return {L"运行状态：无法下载 PDB 符号，拦截已失效", StatusVisual::Red};
    case kPdbStatusIdentityMismatch:
        return {L"运行状态：PDB identity 不匹配或缓存损坏，等待重新下载", StatusVisual::Yellow};
    case kPdbStatusResolving:
        if (status.pdbSource == kPdbSourceCache)
        {
            return {L"运行状态：正在加载本地 PDB 缓存...", StatusVisual::Yellow};
        }
        if (status.pdbSource == kPdbSourceDownload)
        {
            return {L"运行状态：PDB 符号下载中...", StatusVisual::Yellow};
        }
        return {L"运行状态：正在解析 PDB 符号...", StatusVisual::Blue};
    default:
        break;
    }

    switch (status.hookStatus)
    {
    case kHookStatusSymbolResolveFailed:
        return {L"运行状态：PDB 已加载，但手势表 data symbol 未解析", StatusVisual::Red};
    case kHookStatusRangeValidationFailed:
        return {L"运行状态：PDB 已加载，但手势表区间校验失败", StatusVisual::Red};
    case kHookStatusNoMatchingEntries:
        return {L"运行状态：PDB 已加载，但未找到可屏蔽的三指表项", StatusVisual::Red};
    case kHookStatusWriteFailed:
        return {L"运行状态：手势表 patch 写入失败，拦截已失效", StatusVisual::Red};
    case kHookStatusInstalled:
        return {L"运行状态：已拦截 Windows 默认手势 (PDB: " + PdbSourceText(status.pdbSource) + L")",
                StatusVisual::Green};
    default:
        break;
    }

    if (status.pdbStatus == kPdbStatusLoaded)
    {
        return {L"运行状态：PDB 已加载 (" + PdbSourceText(status.pdbSource) + L")，Hook 初始化中...",
                StatusVisual::Blue};
    }
    return {L"运行状态：已注入拦截模块，正在初始化...", StatusVisual::Blue};
}

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

template <typename T>
T FindElement(winrt::Windows::UI::Xaml::Controls::Grid const& root, PCWSTR name)
{
    auto element = root.FindName(name);
    return element ? element.try_as<T>() : T{nullptr};
}
}

bool ConfigWindow::Initialize(HINSTANCE instance, int showCommand)
{
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = ConfigWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hIcon = touchrev::ui::CreateAppIcon(GetSystemMetrics(SM_CXICON));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = ConfigWindow::WindowClassName;
    windowClass.hIconSm = touchrev::ui::CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&windowClass);

    const UINT dpi = GetDpiForSystem();
    const SIZE clientSize = ClientSizePx(dpi);

    // 固定尺寸窗口：保留 WS_THICKFRAME 只为拿到 DWM 的阴影与圆角，实际尺寸由 WM_GETMINMAXINFO 锁死。
    constexpr DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;

    RECT rect = {0, 0, clientSize.cx, clientSize.cy};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

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

    themeManager_.Initialize(hwnd_, WindowBackdrop::Mica, true);
    UpdateXamlTheme();

    // 客户区已经改写，强制重算一次非客户区，让标题栏立即并入客户区。
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    if (showCommand != SW_HIDE)
    {
        Show();
    }
    return true;
}

SIZE ConfigWindow::ClientSizePx(UINT dpi) const
{
    return SIZE{MulDiv(kClientWidthDip, dpi, 96), MulDiv(kClientHeightDip, dpi, 96)};
}

void ConfigWindow::Show()
{
    if (!hwnd_)
    {
        return;
    }

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SyncConfigToUi();
    RefreshAutoStartUi();
    RefreshPdbCacheUi();
    SetTimer(hwnd_, kRefreshTimerId, 1000, nullptr);
}

void ConfigWindow::Hide()
{
    if (hwnd_)
    {
        KillTimer(hwnd_, kRefreshTimerId);
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
    case WM_NCCALCSIZE:
    {
        if (wParam != TRUE)
        {
            break;
        }

        // 沉浸式标题栏：让 DefWindowProc 算出左右下的边框内缩，再把顶边还原回窗口边缘，
        // 于是原本的 caption 区域并入客户区，由 XAML 自绘标题栏填充。
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        const LONG originalTop = params->rgrc[0].top;
        const LRESULT result = DefWindowProcW(hwnd_, message, wParam, lParam);
        params->rgrc[0].top = originalTop;
        return result;
    }

    case WM_SETTINGCHANGE:
        if (themeManager_.Refresh(hwnd_))
        {
            UpdateXamlTheme();
        }
        return 0;

    case WM_GETMINMAXINFO:
    {
        if (!hwnd_)
        {
            break;
        }

        auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetDpiForWindow(hwnd_);
        const SIZE clientSize = ClientSizePx(dpi);

        RECT rect = {0, 0, clientSize.cx, clientSize.cy};
        AdjustWindowRectExForDpi(&rect, GetWindowLongW(hwnd_, GWL_STYLE), FALSE,
                                 GetWindowLongW(hwnd_, GWL_EXSTYLE), dpi);
        minMaxInfo->ptMinTrackSize.x = rect.right - rect.left;
        minMaxInfo->ptMinTrackSize.y = rect.bottom - rect.top;
        minMaxInfo->ptMaxTrackSize = minMaxInfo->ptMinTrackSize;
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
        // 关闭按钮只收起窗口，应用继续驻留托盘。
        Hide();
        return 0;

    case WM_TIMER:
        if (wParam == kRefreshTimerId)
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
        using namespace winrt::Windows::UI::Xaml;
        using namespace winrt::Windows::UI::Xaml::Hosting;
        using namespace winrt::Windows::UI::Xaml::Markup;

        xamlManager_ = WindowsXamlManager::InitializeForCurrentThread();
        xamlSource_ = DesktopWindowXamlSource();

        // 不打开 island 透明，Mica 会被子窗口的不透明位面整块盖住。
        // 这是私有接口，系统更新后可能不可用，失败时在 UpdateXamlTheme 里退化为主题底色。
        const HRESULT transparencyHr = touchrev::common::xaml::EnableIslandTransparency();
        islandTransparent_ = SUCCEEDED(transparencyHr);
        if (!islandTransparent_)
        {
            DebugLogHResult(L"ConfigWindow IXamlSourceTransparency", transparencyHr);
        }

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

        // 隐式实例化全局 Application 对象，以支持所有局部 XAML 在反序列化时的全局主题资源查询
        try
        {
            if (!Application::Current())
            {
                auto app = Application();
            }
            auto currentApp = Application::Current();
            if (currentApp)
            {
                AppendThemeResources(currentApp.Resources());
            }
        }
        catch (...)
        {
        }

        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(
            touchrev::common::ModuleRelativePath(L"xaml/ConfigRoot.xaml"));
        if (xaml.empty())
        {
            return E_FAIL;
        }

        auto root = XamlReader::Load(winrt::hstring{xaml}).as<Controls::Grid>();
        root_ = root;
        UpdateXamlTheme();
        xamlSource_.Content(root_);

        titleBar_ = FindElement<Controls::Grid>(root, L"TitleBar");
        minimizeButton_ = FindElement<Controls::Button>(root, L"MinimizeButton");
        closeButton_ = FindElement<Controls::Button>(root, L"CloseButton");

        hookStatusText_ = FindElement<Controls::TextBlock>(root, L"HookStatusText");
        statusIndicatorDot_ = FindElement<Shapes::Ellipse>(root, L"StatusIndicatorDot");

        hookToggle_ = FindElement<Controls::ToggleSwitch>(root, L"HookToggle");
        gestureToggle_ = FindElement<Controls::ToggleSwitch>(root, L"GestureToggle");
        windowToggle_ = FindElement<Controls::ToggleSwitch>(root, L"WindowToggle");
        autoStartToggle_ = FindElement<Controls::ToggleSwitch>(root, L"AutoStartToggle");
        autoInstallToggle_ = FindElement<Controls::ToggleSwitch>(root, L"AutoInstallToggle");
        autoStartDescription_ = FindElement<Controls::TextBlock>(root, L"AutoStartDescription");
        pdbCacheDescription_ = FindElement<Controls::TextBlock>(root, L"PdbCacheDescription");
        clearCacheButton_ = FindElement<Controls::Button>(root, L"ClearCacheButton");
        openCacheFolderButton_ = FindElement<Controls::Button>(root, L"OpenCacheFolderButton");
        exitButton_ = FindElement<Controls::Button>(root, L"ExitButton");

        if (titleBar_)
        {
            // island 吞掉全部命中测试，标题栏拖动只能在 XAML 侧实现。
            // 不用 WM_NCLBUTTONDOWN(HTCAPTION)：island 收到的是 WM_POINTER，触摸/笔按下时系统移动循环
            // 看不到鼠标左键按下会立刻退出。这里自己捕获指针，按屏幕坐标位移窗口，三种输入走同一路径。
            // 必须用与窗口位置无关的屏幕绝对坐标：若用"island 相对坐标 + 客户区原点"，窗口一移动原点就变，
            // 而排队中的指针事件仍是按旧位置算的相对坐标，两者错位会让窗口来回跳。
            const auto toScreen = [](winrt::Windows::UI::Xaml::Input::Pointer const& pointer) {
                POINT screen{};
                if (pointer.PointerDeviceType() != winrt::Windows::Devices::Input::PointerDeviceType::Mouse)
                {
                    POINTER_INFO info{};
                    if (GetPointerInfo(pointer.PointerId(), &info))
                    {
                        return info.ptPixelLocation;
                    }
                }
                GetCursorPos(&screen);
                return screen;
            };

            titleBar_.PointerPressed([this, toScreen](auto const&, Input::PointerRoutedEventArgs const& args) {
                const auto point = args.GetCurrentPoint(nullptr);
                if (point.PointerDevice().PointerDeviceType() ==
                        winrt::Windows::Devices::Input::PointerDeviceType::Mouse &&
                    !point.Properties().IsLeftButtonPressed())
                {
                    return;
                }

                RECT windowRect{};
                if (!GetWindowRect(hwnd_, &windowRect) || !titleBar_.CapturePointer(args.Pointer()))
                {
                    return;
                }

                titleDragging_ = true;
                titleDragPointerId_ = args.Pointer().PointerId();
                titleDragStartScreen_ = toScreen(args.Pointer());
                titleDragStartWindow_ = {windowRect.left, windowRect.top};
                args.Handled(true);
            });

            titleBar_.PointerMoved([this, toScreen](auto const&, Input::PointerRoutedEventArgs const& args) {
                if (!titleDragging_ || args.Pointer().PointerId() != titleDragPointerId_)
                {
                    return;
                }

                const POINT current = toScreen(args.Pointer());
                SetWindowPos(hwnd_,
                             nullptr,
                             titleDragStartWindow_.x + (current.x - titleDragStartScreen_.x),
                             titleDragStartWindow_.y + (current.y - titleDragStartScreen_.y),
                             0,
                             0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                args.Handled(true);
            });

            const auto endDrag = [this](auto const&, Input::PointerRoutedEventArgs const& args) {
                if (!titleDragging_ || args.Pointer().PointerId() != titleDragPointerId_)
                {
                    return;
                }
                titleDragging_ = false;
                titleBar_.ReleasePointerCapture(args.Pointer());
                args.Handled(true);
            };
            titleBar_.PointerReleased(endDrag);
            titleBar_.PointerCanceled(endDrag);
            titleBar_.PointerCaptureLost([this](auto const&, Input::PointerRoutedEventArgs const&) {
                titleDragging_ = false;
            });
        }

        if (minimizeButton_)
        {
            minimizeButton_.Click([this](auto const&, auto const&) {
                ShowWindow(hwnd_, SW_MINIMIZE);
            });
        }

        if (closeButton_)
        {
            closeButton_.Click([this](auto const&, auto const&) {
                Hide();
            });
        }

        SyncConfigToUi();
        RefreshAutoStartUi();
        RefreshPdbCacheUi();

        if (hookToggle_)
        {
            hookToggle_.Toggled([this](auto const&, auto const&) {
                if (isSyncingUi_)
                {
                    return;
                }
                const bool target = hookToggle_.IsOn();
                if (!hook_ || target == hook_->IsEffective())
                {
                    return;
                }
                // 注入/卸载在后台进行，期间开关置灰；被拒绝时回填会把开关拨回去，并说明原因。
                const bool accepted = target ? hook_->RequestEnable(true) : hook_->RequestDisable(true);
                SyncConfigToUi();
                if (!accepted)
                {
                    MessageBoxW(hwnd_,
                                hook_->IsBusy() ? L"上一个拦截操作仍在进行，请稍候。"
                                                : L"拦截模块控制器未就绪，无法执行操作。",
                                L"Touch-Rev", MB_OK | MB_ICONWARNING);
                }
            });
        }

        if (gestureToggle_)
        {
            gestureToggle_.Toggled([this](auto const&, auto const&) {
                if (isSyncingUi_)
                {
                    return;
                }
                touchrev::settings::SetGestureEnabled(gestureToggle_.IsOn());
            });
        }

        if (windowToggle_)
        {
            windowToggle_.Toggled([this](auto const&, auto const&) {
                if (isSyncingUi_)
                {
                    return;
                }
                touchrev::settings::SetSwitcherWindowEnabled(windowToggle_.IsOn());
            });
        }

        if (autoInstallToggle_)
        {
            autoInstallToggle_.Toggled([this](auto const&, auto const&) {
                if (isSyncingUi_)
                {
                    return;
                }
                touchrev::settings::SetHookAutoInstall(autoInstallToggle_.IsOn());
            });
        }

        if (autoStartToggle_)
        {
            autoStartToggle_.Toggled([this](auto const&, auto const&) {
                if (isSyncingUi_)
                {
                    return;
                }

                DWORD error = ERROR_SUCCESS;
                if (!touchrev::common::autostart::SetEnabled(autoStartToggle_.IsOn(), &error))
                {
                    MessageBoxW(hwnd_, L"写入开机启动项失败。", L"Touch-Rev", MB_OK | MB_ICONERROR);
                }
                RefreshAutoStartUi();
            });
        }

        if (clearCacheButton_)
        {
            clearCacheButton_.Click([this](auto const&, auto const&) {
                // 拦截运行中时会先关闭、清理、再重新启用；结果由完成回调提示。
                if (hook_)
                {
                    hook_->RequestClearCache(true);
                }
                SyncConfigToUi();
            });
        }

        if (openCacheFolderButton_)
        {
            openCacheFolderButton_.Click([this](auto const&, auto const&) {
                const std::wstring root = touchrev::common::pdbcache::Root();
                if (root.empty())
                {
                    return;
                }
                SHCreateDirectoryExW(nullptr, root.c_str(), nullptr);
                ShellExecuteW(hwnd_, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            });
        }

        if (exitButton_)
        {
            exitButton_.Click([this](auto const&, auto const&) {
                if (MessageBoxW(hwnd_, L"退出后 Touch-Rev 的三指手势将停止工作，Windows 默认三指手势会恢复。确定退出吗？",
                                L"Touch-Rev", MB_OKCANCEL | MB_ICONQUESTION) == IDOK &&
                    exitCallback_)
                {
                    exitCallback_();
                }
            });
        }

        // 托盘菜单改动配置后回填界面，两处开关状态始终一致。
        settingsToken_ = touchrev::settings::Subscribe([this](const touchrev::settings::AppSettings&) {
            SyncConfigToUi();
        });

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
    KillTimer(hwnd_, kRefreshTimerId);
    if (settingsToken_ != 0)
    {
        touchrev::settings::Unsubscribe(settingsToken_);
        settingsToken_ = 0;
    }

    titleBar_ = nullptr;
    minimizeButton_ = nullptr;
    closeButton_ = nullptr;
    hookStatusText_ = nullptr;
    statusIndicatorDot_ = nullptr;
    hookToggle_ = nullptr;
    gestureToggle_ = nullptr;
    windowToggle_ = nullptr;
    autoStartToggle_ = nullptr;
    autoInstallToggle_ = nullptr;
    autoStartDescription_ = nullptr;
    pdbCacheDescription_ = nullptr;
    clearCacheButton_ = nullptr;
    openCacheFolderButton_ = nullptr;
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
    if (!root_)
    {
        return;
    }

    isSyncingUi_ = true;

    const auto& settings = touchrev::settings::Current();
    const bool hookBusy = hook_ && hook_->IsBusy();
    const bool hookLoaded = hook_ && hook_->IsModuleLoaded();
    // 开关反映"拦截是否真正生效"：DLL 已加载但初始化失败时显示为关，用户再打开即可重试。
    const bool hookEffective = hook_ && hook_->IsEffective();

    if (hookToggle_)
    {
        hookToggle_.IsEnabled(!hookBusy);
        if (!hookBusy && hookToggle_.IsOn() != hookEffective)
        {
            hookToggle_.IsOn(hookEffective);
        }
    }
    if (clearCacheButton_)
    {
        clearCacheButton_.IsEnabled(!hookBusy);
    }
    if (gestureToggle_ && gestureToggle_.IsOn() != settings.gestureEnabled)
    {
        gestureToggle_.IsOn(settings.gestureEnabled);
    }
    if (windowToggle_ && windowToggle_.IsOn() != settings.switcherWindowEnabled)
    {
        windowToggle_.IsOn(settings.switcherWindowEnabled);
    }
    if (autoInstallToggle_ && autoInstallToggle_.IsOn() != settings.hookAutoInstall)
    {
        autoInstallToggle_.IsOn(settings.hookAutoInstall);
    }

    if (hookStatusText_)
    {
        const auto [text, visual] = hookBusy
                                        ? std::pair<std::wstring, StatusVisual>{L"运行状态：正在处理拦截模块...", StatusVisual::Blue}
                                        : DescribeStatus(hookLoaded);
        const bool isDark = themeManager_.IsDark();
        hookStatusText_.Text(winrt::hstring{text});
        hookStatusText_.Foreground(
            winrt::Windows::UI::Xaml::Media::SolidColorBrush(StatusColor(isDark, visual)));
        if (statusIndicatorDot_)
        {
            statusIndicatorDot_.Fill(
                winrt::Windows::UI::Xaml::Media::SolidColorBrush(StatusColor(isDark, visual)));
        }
    }

    isSyncingUi_ = false;
}

void ConfigWindow::RefreshAutoStartUi()
{
    const auto state = touchrev::common::autostart::Query();

    isSyncingUi_ = true;
    if (autoStartToggle_ && autoStartToggle_.IsOn() != state.enabled)
    {
        autoStartToggle_.IsOn(state.enabled);
    }
    isSyncingUi_ = false;

    if (!autoStartDescription_)
    {
        return;
    }

    if (state.blockedByTaskManager)
    {
        autoStartDescription_.Text(L"已在任务管理器的\"启动\"页被禁用，重新打开开关即可恢复");
    }
    else if (state.enabled)
    {
        autoStartDescription_.Text(L"登录后静默驻留托盘，不弹出本窗口");
    }
    else
    {
        autoStartDescription_.Text(L"开启后随 Windows 登录自动运行并驻留托盘");
    }
}

void ConfigWindow::RefreshPdbCacheUi()
{
    if (!pdbCacheDescription_)
    {
        return;
    }

    const auto usage = touchrev::common::pdbcache::QueryUsage();
    if (!usage.exists || usage.fileCount == 0)
    {
        pdbCacheDescription_.Text(L"缓存为空，启用拦截时会重新下载符号");
        return;
    }

    const std::wstring text = L"已占用 " + touchrev::common::pdbcache::FormatBytes(usage.totalBytes) +
                              L"（" + std::to_wstring(usage.fileCount) + L" 个文件）；" +
                              L"符号异常时清除可强制重新下载";
    pdbCacheDescription_.Text(winrt::hstring{text});
}

void ConfigWindow::RefreshHookUi()
{
    SyncConfigToUi();
    RefreshPdbCacheUi();
}

void ConfigWindow::UpdateXamlTheme()
{
    if (!root_)
    {
        return;
    }

    using namespace winrt::Windows::UI::Xaml;
    const bool isDark = themeManager_.IsDark();
    root_.RequestedTheme(isDark ? ElementTheme::Dark : ElementTheme::Light);

    // island 无法透明时 Mica 不可见，铺一层与 Mica 观感接近的主题底色，避免露出未定义的位面颜色。
    if (!islandTransparent_)
    {
        root_.Background(Media::SolidColorBrush(isDark ? winrt::Windows::UI::Color{255, 0x20, 0x20, 0x20}
                                                       : winrt::Windows::UI::Color{255, 0xF3, 0xF3, 0xF3}));
    }
}
