#pragma once

#include <windows.h>
#include <inspectable.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

struct IDesktopWindowXamlSourceNative : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AttachToWindow(HWND parentWnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hWnd) = 0;
};

class ThinXamlAppSwitcherHost
{
public:
    bool Initialize(HWND parentHwnd);
    void Shutdown();
    void Resize(UINT width, UINT height);

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
