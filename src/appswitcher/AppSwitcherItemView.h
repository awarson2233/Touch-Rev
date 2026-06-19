#pragma once

#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"
#include "ui/ThemeManager.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <memory>
#include <string>

class AppSwitcherItemView
{
public:
    winrt::Windows::UI::Xaml::FrameworkElement root{nullptr};
    winrt::Windows::UI::Xaml::Media::CompositeTransform transform{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid layoutGrid{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border mainCard{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border titleBorder{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock title{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock defaultIcon{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button closeButton{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border thumbnailHost{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border pressOverlay{nullptr};
    HWND hwnd = nullptr;
    PointDip layoutPosition{};
    SizeDip layoutSize{};
    std::unique_ptr<touchrev::thumbnail::PrivateThumbnailSlot> thumbnailSlot;
    HRESULT thumbnailError = S_OK;
    bool thumbnailFailed = false;
    bool visible = false;
    bool hovered = false;
    bool pressed = false;
    bool grabbed = false;

    void ApplyRowWeights();
    void ApplyTheme(const AppSwitcherPalette& palette);
    void ApplyInteractionState(const AppSwitcherPalette& palette);
    void ApplyCloseButtonHoverState(const AppSwitcherPalette& palette, bool isHovered);
    void ClearThumbnail();
    void Reset(const AppSwitcherPalette& palette);
    void SetRootVisibility(bool isVisible);
    void AssignWindow(HWND newHwnd);
    void ApplyTitle(const std::wstring& titleText, size_t fallbackOrdinal);
    void ApplyCloseButtonWidth(double widthDip);
    void EnsureThumbnail(
        touchrev::thumbnail::PrivateThumbnailManager& thumbnailManager,
        double widthDip,
        double heightDip,
        double dpiScale);
};
