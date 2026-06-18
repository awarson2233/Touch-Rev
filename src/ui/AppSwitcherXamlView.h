#pragma once

#include "AppSwitcherLayoutEngine.h"
#include "AppTheme.h"
#include "ThinXamlAppSwitcherHost.h"
#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AppSwitcherXamlView
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
    void SetTargetMonitor(HMONITOR monitor);
    void SetBoundsChangedCallback(std::function<void()> callback);
    void SetMissedInputCallback(std::function<void()> callback);
    void SetItemActivatedCallback(std::function<void(HWND)> callback);
    void SetItemDragReleasedCallback(std::function<void(HWND, POINT)> callback);
    void SetItemCloseRequestedCallback(std::function<bool(HWND)> callback);
    bool MoveSelection(int stepX, int stepY);
    bool MoveSelectionNext();
    bool MoveSelectionPrevious();
    bool ActivateSelectedItem();

private:
    struct ItemView
    {
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
    };

    bool LoadRoot();
    ItemView CreateItem();
    void EnsureItemCount(size_t count);
    void ApplyLayout(const std::vector<AppSwitcherWindowItem>& windows, UINT widthPx, UINT heightPx, double scale);
    void AttachPointerHandlers();
    void UpdateVisibleBoundsAndPositions();
    void EnsureSelectedIndex();
    void UpdateSelectionVisual();
    bool CanNavigateSelection() const;
    bool SetSelectedIndex(size_t index);
    void ApplyItemTheme(ItemView& item);
    void ApplyItemRowWeights(ItemView& item);
    void ApplyItemInteractionState(ItemView& item);
    void ResetInteractionState();
    void BeginGrab(size_t itemIndex);
    void FinishPressedItem(size_t itemIndex, PointDip releasePoint);
    void HandleItemCloseRequested(size_t itemIndex);
    POINT DipPointToScreenPixel(PointDip point) const;
    void ClearItemThumbnail(ItemView& item);
    void ResetItem(ItemView& item);

    static std::wstring LoadTextFileUtf8(const std::wstring& path);
    static std::wstring ModuleRelativePath(const std::wstring& relativePath);

    HWND hwnd_ = nullptr;
    HMONITOR targetMonitor_ = nullptr;
    ThinXamlAppSwitcherHost* host_ = nullptr;
    bool initialized_ = false;

    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border appSwitcherContainer_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Canvas layoutCanvas_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border focusBorder_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement emptyGrid_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock emptyIcon_{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock emptyText_{nullptr};
    std::vector<ItemView> items_;
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
    AppSwitcherPalette palette_ = PaletteForTheme(AppThemeMode::Dark);
    touchrev::thumbnail::PrivateThumbnailManager thumbnailManager_;
};
