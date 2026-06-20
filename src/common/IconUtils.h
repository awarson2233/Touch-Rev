#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Media.h>

namespace touchrev::common::icon
{
    // 获取指定窗口的图标并转换为 WinRT 的 ImageSource。如果失败，则返回 nullptr。
    winrt::Windows::UI::Xaml::Media::ImageSource GetWindowIcon(HWND hwnd);

    // 将 HICON 转换为 WinRT 的 ImageSource
    winrt::Windows::UI::Xaml::Media::ImageSource HIconToImageSource(HICON hIcon);
}
