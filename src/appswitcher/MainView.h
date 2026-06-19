#pragma once

#include "CardView.h"
#include "LayoutEngine.h"
#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"
#include "ui/ThemeManager.h"
#include "ui/ThinXamlAppSwitcherHost.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Input.h>


#include <functional>
#include <vector>
#include <memory>

namespace touchrev::appswitcher
{
class MainView
{
public:
    bool Initialize(HWND hwnd, ThinXamlAppSwitcherHost& host);
    void Shutdown();
    void Resize(UINT widthPx, UINT heightPx, double scale);
    void RenderSample(UINT widthPx, UINT heightPx, double scale);
    void CancelInteraction();
    bool HitTest(PointDip point) const;
    RECT VisibleBoundsPx() const;
    RECT ContainerBoundsPx() const;
    PointDip DragPosition() const { return dragPosition_; }
    void SetDragPosition(PointDip position);
    void ApplyTheme(const AppSwitcherPalette& palette);
    void SetBoundsChangedCallback(std::function<void()> callback);
    void SetMissedInputCallback(std::function<void()> callback);
    void SetItemActivatedCallback(std::function<void(HWND)> callback);
    void SetItemDragReleasedCallback(std::function<void(HWND, POINT)> callback);
    void SetItemCloseRequestedCallback(std::function<bool(HWND)> callback);
    bool MoveSelection(int stepX, int stepY);
    bool MoveSelectionNext();
    bool MoveSelectionPrevious();
    bool ActivateSelectedItem();
    bool AccumulateAndMoveSelection(double deltaX, double deltaY);
    void ClearGestureAccumulator();

private:
    std::vector<ItemGeometry> GetItemGeometries() const;
    bool LoadRoot();
    std::unique_ptr<CardView> CreateItem();
    void EnsureItemCount(size_t count);
    void ApplyLayout(const std::vector<WindowItem>& windows, UINT widthPx, UINT heightPx, double scale);
    void AttachPointerHandlers();
    void UpdateVisibleBoundsAndPositions();
    void EnsureSelectedIndex();
    void UpdateSelectionVisual();
    bool CanNavigateSelection() const;
    bool SetSelectedIndex(size_t index);
    void ResetInteractionState();
    void BeginGrab(size_t itemIndex);
    void FinishPressedItem(size_t itemIndex, PointDip releasePoint);
    void HandleItemCloseRequested(size_t itemIndex);
    POINT DipPointToScreenPixel(PointDip point) const;
    void ProcessItemPressed(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void ProcessItemMoved(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void ProcessItemReleased(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    HWND hwnd_ = nullptr;

    ThinXamlAppSwitcherHost* host_ = nullptr;
    bool initialized_ = false;

    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border appSwitcherContainer_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Canvas layoutCanvas_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border focusBorder_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement emptyGrid_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock emptyIcon_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock emptyText_{nullptr};
    std::vector<std::unique_ptr<CardView>> cards_;
    PointDip dragPosition_{};
    PointDip contentOriginDip_{};
    PointDip visibleOriginDip_{};
    SizeDip contentBoundsDip_{};
    SizeDip visibleBoundsDip_{};
    SizeDip clientSizeDip_{};
    bool xamlPointerDragging_ = false;
    bool xamlPointerPressed_ = false;
    size_t activeDragItemIndex_ = static_cast<size_t>(-1);
    size_t pressedItemIndex_ = static_cast<size_t>(-1);
    size_t selectedItemIndex_ = static_cast<size_t>(-1);
    PointDip pressPointDip_{};
    double xamlDragOffsetX_ = 0.0;
    double xamlDragOffsetY_ = 0.0;
    double currentDpiScale_ = 1.0;
    std::function<void()> boundsChangedCallback_;
    std::function<void()> missedInputCallback_;
    std::function<void(HWND)> itemActivatedCallback_;
    std::function<void(HWND, POINT)> itemDragReleasedCallback_;
    std::function<bool(HWND)> itemCloseRequestedCallback_;
    std::vector<HWND> dismissedHwnds_;
    AppSwitcherPalette palette_{};
    touchrev::thumbnail::PrivateThumbnailManager thumbnailManager_;
    double gestureAccX_ = 0.0;
    double gestureAccY_ = 0.0;
};
}
