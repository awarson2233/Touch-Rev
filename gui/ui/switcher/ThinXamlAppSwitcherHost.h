#pragma once

#include <windows.h>
#include <inspectable.h>

#include "common/XamlIslandCommon.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

class ThinXamlAppSwitcherHost
{
public:
    bool Initialize(HWND parentHwnd);
    void Shutdown();
    void Resize(UINT width, UINT height);
    void SetBounds(int left, int top, UINT width, UINT height);

    bool IsInitialized() const { return initialized_; }
    HWND ChildHwnd() const { return xamlHwnd_; }

    winrt::Windows::UI::Xaml::Controls::Grid Root() const { return root_; }
    void SetRoot(winrt::Windows::UI::Xaml::Controls::Grid const& root);

private:
    HWND parentHwnd_ = nullptr;
    HWND xamlHwnd_ = nullptr;
    bool initialized_ = false;

    winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager xamlManager_{nullptr};
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource xamlSource_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
};
