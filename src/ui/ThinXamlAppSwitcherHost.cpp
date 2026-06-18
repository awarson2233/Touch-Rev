#include "ThinXamlAppSwitcherHost.h"

#include "common/Win32Error.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.UI.Xaml.h>

#include <algorithm>

namespace
{
constexpr GUID kIDesktopWindowXamlSourceNative = {
    0x3cbcf1bf,
    0x2f76,
    0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54},
};

constexpr GUID kIXamlSourceTransparency = {
    0x06636c29,
    0x5a17,
    0x458d,
    {0x8e, 0xa2, 0x24, 0x22, 0xd9, 0x97, 0xa9, 0x22},
};

struct IXamlSourceTransparency : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_IsBackgroundTransparent(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBackgroundTransparent(boolean value) = 0;
};

}

bool ThinXamlAppSwitcherHost::Initialize(HWND parentHwnd)
{
    if (initialized_)
    {
        return true;
    }

    parentHwnd_ = parentHwnd;
    try
    {
        using namespace winrt::Windows::UI::Xaml::Hosting;

        xamlManager_ = WindowsXamlManager::InitializeForCurrentThread();
        xamlSource_ = DesktopWindowXamlSource();

        auto currentWindow = winrt::Windows::UI::Xaml::Window::Current();
        if (currentWindow)
        {
            winrt::com_ptr<IXamlSourceTransparency> transparency;
            auto* windowAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(currentWindow));
            HRESULT transparencyHr = windowAbi->QueryInterface(kIXamlSourceTransparency, transparency.put_void());
            if (SUCCEEDED(transparencyHr) && transparency)
            {
                transparencyHr = transparency->put_IsBackgroundTransparent(true);
            }
            if (FAILED(transparencyHr))
            {
                DebugLogHResult(L"IXamlSourceTransparency::put_IsBackgroundTransparent", transparencyHr);
            }
        }

        winrt::com_ptr<IDesktopWindowXamlSourceNative> nativeSource;
        auto* xamlSourceAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(xamlSource_));
        HRESULT hr = xamlSourceAbi->QueryInterface(kIDesktopWindowXamlSourceNative, nativeSource.put_void());
        if (FAILED(hr))
        {
            DebugLogHResult(L"DesktopWindowXamlSource QI native", hr);
            Shutdown();
            return false;
        }

        hr = nativeSource->AttachToWindow(parentHwnd_);
        if (FAILED(hr))
        {
            DebugLogHResult(L"IDesktopWindowXamlSourceNative::AttachToWindow", hr);
            Shutdown();
            return false;
        }

        hr = nativeSource->get_WindowHandle(&xamlHwnd_);
        if (FAILED(hr))
        {
            DebugLogHResult(L"IDesktopWindowXamlSourceNative::get_WindowHandle", hr);
            Shutdown();
            return false;
        }

        RECT client{};
        GetClientRect(parentHwnd_, &client);
        Resize(
            static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
            static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top)));

        initialized_ = true;
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Initialize XAML Island", error.code());
        Shutdown();
        return false;
    }
}

void ThinXamlAppSwitcherHost::Shutdown()
{
    if (xamlSource_)
    {
        xamlSource_.Content(nullptr);
    }

    root_ = nullptr;
    xamlSource_ = nullptr;
    xamlManager_ = nullptr;
    xamlHwnd_ = nullptr;
    parentHwnd_ = nullptr;
    initialized_ = false;
}

void ThinXamlAppSwitcherHost::Resize(UINT width, UINT height)
{
    SetBounds(0, 0, width, height);
}

void ThinXamlAppSwitcherHost::SetBounds(int left, int top, UINT width, UINT height)
{
    if (!xamlHwnd_)
    {
        return;
    }

    SetWindowPos(
        xamlHwnd_,
        nullptr,
        left,
        top,
        static_cast<int>(std::max(1u, width)),
        static_cast<int>(std::max(1u, height)),
        SWP_NOZORDER | SWP_SHOWWINDOW);
}

void ThinXamlAppSwitcherHost::SetRoot(winrt::Windows::UI::Xaml::Controls::Grid const& root)
{
    root_ = root;
    if (xamlSource_)
    {
        xamlSource_.Content(root_);
    }
}
