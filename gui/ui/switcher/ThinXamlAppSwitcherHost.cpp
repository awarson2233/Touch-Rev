#include "ThinXamlAppSwitcherHost.h"

#include "common/Win32Error.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>

#include <algorithm>

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

        const HRESULT transparencyHr = touchrev::common::xaml::EnableIslandTransparency();
        if (FAILED(transparencyHr))
        {
            DebugLogHResult(L"IXamlSourceTransparency::put_IsBackgroundTransparent", transparencyHr);
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
        // 同一线程上可能还有别的岛（拖动代理），每个源都要显式关闭，否则其子窗口与合成资源会残留。
        xamlSource_.Close();
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
