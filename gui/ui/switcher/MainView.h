#pragma once

#include "CardView.h"
#include "LayoutEngine.h"
#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"
#include "ui/shared/ThemeManager.h"
#include "ThinXamlAppSwitcherHost.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Foundation.Numerics.h>


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
    // 窗口铺满整个显示器，但卡片只在工作区内排版：四边内缩为任务栏等占用的像素（相对客户区）。
    void SetWorkAreaInsetsPx(const RECT& insets) { workAreaInsetsPx_ = insets; }
    void RenderSample(UINT widthPx, UINT heightPx, double scale);
    // 强制在当前调用栈内跑完 measure/arrange，消除"属性已设、布局未跑"的窗口期。
    void EnsureLayoutCommitted();
    void CancelInteraction();
    bool HitTest(PointDip point) const;
    RECT VisibleBoundsPx() const;
    RECT ContainerBoundsPx() const;
    PointDip DragPosition() const { return dragPosition_; }
    void SetDragPosition(PointDip position);
    void ApplyTheme(AppThemeMode mode, bool active = true);
    void SetBoundsChangedCallback(std::function<void()> callback);
    void SetMissedInputCallback(std::function<void()> callback);
    void SetItemActivatedCallback(std::function<void(HWND)> callback);
    void SetItemDragReleasedCallback(std::function<void(HWND, POINT)> callback);
    void SetItemCloseRequestedCallback(std::function<bool(HWND)> callback);
    bool MoveSelection(int stepX, int stepY, bool allowWrap = true);
    bool MoveSelectionNext();
    bool MoveSelectionPrevious();
    bool ActivateSelectedItem();
    bool AccumulateAndMoveSelection(double deltaX, double deltaY);
    void ClearGestureAccumulator();
    void ResetSelection();

private:
    std::vector<ItemGeometry> GetItemGeometries() const;
    bool LoadRoot();
    std::unique_ptr<CardView> CreateItem();
    void EnsureItemCount(size_t count);
    void ApplyLayout(const std::vector<WindowItem>& windows, UINT widthPx, UINT heightPx, double scale);
    void AttachPointerHandlers();
    void UpdateVisibleBoundsAndPositions();
    void EnsureSelectedIndex();
    // animate=true 仅用于选中项切换；布局重算时卡片本身是瞬移的，选择框也直接跳到位。
    void UpdateSelectionVisual(bool animate = false);
    void CreateFocusRing();
    void UpdateFocusRingStyle();
    void HideFocusRing(bool animate);
    // 外层描边朝 (x, y) 符号所指方向伸出 |x|、|y| DIP；内层描边不动。
    void SetFocusGrowth(float x, float y, bool animate);
    // 累积量占阈值的比例 → 生长量；该方向没有下一张卡片时按撞墙阻尼缩小。
    float GrowthForAxis(double accumulated, double threshold, int stepX, int stepY) const;
    double FocusResource(const wchar_t* key, double fallback) const;
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
    void UpdateContainerAcrylicBrush();

    HWND hwnd_ = nullptr;

    ThinXamlAppSwitcherHost* host_ = nullptr;
    bool initialized_ = false;

    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border appSwitcherContainer_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border appSwitcherStroke_{nullptr};
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
    RECT workAreaInsetsPx_{};
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
    AppThemeMode themeMode_ = AppThemeMode::Dark;
    winrt::Windows::UI::Composition::SpriteVisual containerAcrylicVisual_{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry containerAcrylicClip_{nullptr};
    // 选择框：props 的 Offset/Size 为强调色外沿的静止矩形（弹簧动画），Growth 为手势生长量；各几何经表达式绑定到 props。
    winrt::Windows::UI::Composition::ShapeVisual focusVisual_{nullptr};
    winrt::Windows::UI::Composition::CompositionPropertySet focusProps_{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry focusOuterGeometry_{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry focusInnerGeometry_{nullptr};
    // 裁剪出强调色外沿；外层几何是一条覆盖最大生长量的宽描边，只有内沿是真实边界。
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry focusClipGeometry_{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush focusAccentBrush_{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush focusInnerBrush_{nullptr};
    // 上一次的目标外层几何；重复布局给出相同目标时跳过，避免打断正在进行的弹簧。
    winrt::Windows::Foundation::Numerics::float2 focusTargetOffset_{};
    winrt::Windows::Foundation::Numerics::float2 focusTargetSize_{};
    bool focusRingShown_ = false;
    bool animationsEnabled_ = true;
    touchrev::thumbnail::PrivateThumbnailManager thumbnailManager_;

    double gestureAccX_ = 0.0;
    double gestureAccY_ = 0.0;
    bool isWindowActive_ = true;
};
}
