#pragma once

#include "ui/shared/ThemeManager.h"

#include <windows.h>
#include <inspectable.h>

#include "common/XamlIslandCommon.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <functional>

namespace touchrev::hook
{
class HookController;
}

class ConfigWindow
{
public:
    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.ConfigWindow";

    // 拦截模块的状态与操作都委托给 HookController，须在 Initialize 之前设置。
    void SetHookController(touchrev::hook::HookController* hook) { hook_ = hook; }
    // "退出应用"按钮走宿主的统一退出路径，而不是直接结束消息循环。
    void SetExitCallback(std::function<void()> callback) { exitCallback_ = std::move(callback); }

    bool Initialize(HINSTANCE instance, int showCommand);
    void Show();
    void Hide();
    HWND Hwnd() const { return hwnd_; }
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }

    // 拦截操作开始或在后台完成后，回填开关、状态与缓存占用。
    void RefreshHookUi();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HRESULT OnCreate();
    void OnDestroy();

    void SyncConfigToUi();
    void RefreshAutoStartUi();
    void RefreshPdbCacheUi();
    SIZE ClientSizePx(UINT dpi) const;

    HWND hwnd_ = nullptr;
    HWND xamlHwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ThemeManager themeManager_;
    int settingsToken_ = 0;
    touchrev::hook::HookController* hook_ = nullptr;
    std::function<void()> exitCallback_;
    // 私有透明接口不可用时 island 是不透明位面，Mica 透不上来，改为铺主题底色。
    bool islandTransparent_ = true;

    // 标题栏拖动：按下时的指针屏幕坐标与窗口左上角，移动时按位移平移窗口。
    bool titleDragging_ = false;
    uint32_t titleDragPointerId_ = 0;
    POINT titleDragStartScreen_{};
    POINT titleDragStartWindow_{};

    // 独立的 XAML Island 宿主相关成员，解耦 ThinXamlAppSwitcherHost
    winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager xamlManager_{nullptr};
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource xamlSource_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};

    void UpdateXamlTheme();

    // 标题栏
    winrt::Windows::UI::Xaml::Controls::Grid titleBar_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button minimizeButton_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button closeButton_{nullptr};

    // 状态与配置控件
    winrt::Windows::UI::Xaml::Controls::TextBlock hookStatusText_{nullptr};
    winrt::Windows::UI::Xaml::Shapes::Ellipse statusIndicatorDot_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch hookToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch gestureToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch windowToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch autoStartToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch autoInstallToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock autoStartDescription_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock pdbCacheDescription_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button clearCacheButton_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button openCacheFolderButton_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button exitButton_{nullptr};

    // UI 回填期间抑制 Toggled 回调，避免注入/反注入自激
    bool isSyncingUi_ = false;
};
