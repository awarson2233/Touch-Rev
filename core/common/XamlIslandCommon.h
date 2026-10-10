#pragma once

#include <windows.h>
#include <inspectable.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>

struct IDesktopWindowXamlSourceNative : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AttachToWindow(HWND parentWnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hWnd) = 0;
};

inline constexpr GUID kIDesktopWindowXamlSourceNative = {
    0x3cbcf1bf,
    0x2f76,
    0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54},
};

// Window::Current 上的私有接口，关掉 XAML Island 子窗口的不透明重定向位面。
// 不打开它，宿主窗口的 Mica / 亚克力一个像素都透不上来。
struct IXamlSourceTransparency : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_IsBackgroundTransparent(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBackgroundTransparent(boolean value) = 0;
};

inline constexpr GUID kIXamlSourceTransparency = {
    0x06636c29,
    0x5a17,
    0x458d,
    {0x8e, 0xa2, 0x24, 0x22, 0xd9, 0x97, 0xa9, 0x22},
};

namespace touchrev::common::xaml
{
// 作用于线程共享的 Window::Current，每个 island 宿主都应显式调用，不能依赖初始化顺序。
inline HRESULT EnableIslandTransparency()
{
    auto currentWindow = winrt::Windows::UI::Xaml::Window::Current();
    if (!currentWindow)
    {
        return E_NOT_VALID_STATE;
    }

    winrt::com_ptr<IXamlSourceTransparency> transparency;
    auto* windowAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(currentWindow));
    const HRESULT hr = windowAbi->QueryInterface(kIXamlSourceTransparency, transparency.put_void());
    if (FAILED(hr) || !transparency)
    {
        return hr;
    }

    return transparency->put_IsBackgroundTransparent(true);
}

inline winrt::Windows::UI::Xaml::Media::SolidColorBrush Brush(winrt::Windows::UI::Color color)
{
    return winrt::Windows::UI::Xaml::Media::SolidColorBrush(color);
}
}
