#pragma once

#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"
#include "ui/ThemeManager.h"

#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>

#include <memory>
#include <string>
#include <functional>

namespace touchrev::appswitcher
{
struct CardCallbacks
{
    std::function<void(size_t index)> onCloseClicked;
    std::function<void(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)> onPointerPressed;
    std::function<void(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)> onPointerMoved;
    std::function<void(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)> onPointerReleased;
    std::function<void(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)> onPointerCanceled;
};


class CardView
{
public:
    winrt::Windows::UI::Xaml::FrameworkElement root{nullptr};
    winrt::Windows::UI::Xaml::Media::CompositeTransform transform{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid layoutGrid{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border mainCard{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border titleBorder{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock title{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock defaultIcon{nullptr};
    winrt::Windows::UI::Xaml::Controls::Image appIcon{nullptr};
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
    bool closeButtonHovered = false;
    AppSwitcherPalette palette_{};

    static std::unique_ptr<CardView> Create(
        const AppSwitcherPalette& palette,
        size_t index,
        CardCallbacks callbacks);

    CardView() = default;
    ~CardView() { Destroy(); }

    double TitleRowWeight() const { return titleRowWeight_; }
    double ContentRowWeight() const { return contentRowWeight_; }

    CardView(CardView&&) noexcept = default;
    CardView& operator=(CardView&&) noexcept = default;

    CardView(const CardView&) = delete;
    CardView& operator=(const CardView&) = delete;

    bool Initialize(
        winrt::Windows::UI::Xaml::FrameworkElement rootElement,
        const AppSwitcherPalette& palette,
        size_t index,
        CardCallbacks callbacks);

    void ApplyRowWeights();
    void ApplyTheme(const AppSwitcherPalette& palette, bool active = true);
    void ApplyInteractionState(const AppSwitcherPalette& palette);
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
    void UpdateState(
        HWND newHwnd,
        const std::wstring& titleText,
        size_t index,
        double x,
        double y,
        double w,
        double h,
        bool isDragging,
        touchrev::thumbnail::PrivateThumbnailManager& thumbnailManager,
        double dpiScale);

private:
    void UpdateVisualState();
    void Destroy();
    void LoadAppIcon();

    double titleRowWeight_ = 1.8;
    double contentRowWeight_ = 8.2;
    double pressedScale_ = 0.985;
    double grabbedScale_ = 0.90;
};
}



