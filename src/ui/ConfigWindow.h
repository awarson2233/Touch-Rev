#pragma once

#include "ThemeManager.h"

#include <windows.h>
#include <inspectable.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

class ConfigWindow
{
public:
    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.ConfigWindow";

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    void Show();
    void Hide();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HRESULT OnCreate();
    void OnDestroy();

    void SyncConfigToUi();
    void ToggleHook(bool enable);
    bool QueryHookStatus();

    HWND hwnd_ = nullptr;
    HWND xamlHwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ThemeManager themeManager_;

    // 独立的 XAML Island 宿主相关成员，解耦 ThinXamlAppSwitcherHost
    winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager xamlManager_{nullptr};
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource xamlSource_{nullptr};

    // UI elements
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch hookToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch gestureToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch windowToggle_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button exitButton_{nullptr};

    bool isTogglingHook_ = false;
};
