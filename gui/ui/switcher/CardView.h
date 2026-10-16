#pragma once

#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"
#include "ui/shared/ThemeManager.h"

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
    winrt::Windows::UI::Xaml::Controls::TextBlock title{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock defaultIcon{nullptr};
    winrt::Windows::UI::Xaml::Controls::Image appIcon{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button closeButton{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border thumbnailHost{nullptr};
    // 位于缩略图视觉之下；私有缩略图接口失败时显示说明文字，而不是一块空白卡片。
    winrt::Windows::UI::Xaml::Controls::TextBlock thumbnailPlaceholder{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border pressOverlay{nullptr};
    HWND hwnd = nullptr;
    PointDip layoutPosition{};
    SizeDip layoutSize{};
    std::unique_ptr<touchrev::thumbnail::PrivateThumbnailSlot> thumbnailSlot;
    HRESULT thumbnailError = S_OK;
    bool thumbnailFailed = false;
    bool visible = false;
    bool pressed = false;
    bool grabbed = false;
    static std::unique_ptr<CardView> Create(
        size_t index,
        CardCallbacks callbacks);

    CardView() = default;
    ~CardView() { Destroy(); }

    double TitleHeightDip() const { return titleHeightDip_; }
    double BorderDip() const { return borderDip_; }

    CardView(CardView&&) noexcept = default;
    CardView& operator=(CardView&&) noexcept = default;

    CardView(const CardView&) = delete;
    CardView& operator=(const CardView&) = delete;

    bool Initialize(
        winrt::Windows::UI::Xaml::FrameworkElement rootElement,
        size_t index,
        CardCallbacks callbacks);

    void LoadTemplateMetrics();
    void ApplyTheme(bool active = true);
    void ApplyInteractionState();
    void ClearThumbnail();
    void Reset();
    void SetRootVisibility(bool isVisible);
    void AssignWindow(HWND newHwnd);
    void ApplyTitle(const std::wstring& titleText, size_t fallbackOrdinal);
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

    // 取自卡片模板：标题行固定高度与卡片描边宽度，单位 DIP。
    double titleHeightDip_ = 40.0;
    double borderDip_ = 1.0;
    double pressedScale_ = 0.985;
    double grabbedScale_ = 0.90;
};
}



