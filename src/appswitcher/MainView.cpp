#include "MainView.h"

#include "WindowController.h"
#include "common/Win32Error.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"
#include "common/GeometryUtils.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace touchrev::appswitcher
{
namespace
{
constexpr wchar_t kRootXamlPath[] = L"xaml/AppSwitcherRoot.xaml";
constexpr wchar_t kItemXamlPath[] = L"xaml/SwitcherItem.xaml";

winrt::Windows::UI::Xaml::Media::SolidColorBrush Brush(winrt::Windows::UI::Color color)
{
    return winrt::Windows::UI::Xaml::Media::SolidColorBrush(color);
}

winrt::Windows::UI::Xaml::Media::Brush AcrylicBrush(const AppSwitcherPalette& palette)
{
    try
    {
        auto brush = winrt::Windows::UI::Xaml::Media::AcrylicBrush();
        brush.BackgroundSource(winrt::Windows::UI::Xaml::Media::AcrylicBackgroundSource::HostBackdrop);
        brush.TintColor(palette.containerAcrylicTint);
        brush.TintOpacity(palette.containerAcrylicTintOpacity);
        brush.FallbackColor(palette.containerAcrylicFallback);
        return brush;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Create AcrylicBrush", error.code());
        return Brush(palette.containerAcrylicFallback);
    }
}
}
std::vector<ItemGeometry> MainView::GetItemGeometries() const
{
    std::vector<ItemGeometry> geometries;
    geometries.reserve(cards_.size());
    for (const auto& item : cards_)
    {
        geometries.push_back({item->layoutPosition, item->layoutSize, item->visible});
    }
    return geometries;
}

bool MainView::Initialize(HWND hwnd, ThinXamlAppSwitcherHost& host)
{
    hwnd_ = hwnd;
    host_ = &host;
    if (!host_->IsInitialized())
    {
        return false;
    }

    initialized_ = LoadRoot();
    if (!initialized_)
    {
        return false;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    RenderSample(
        static_cast<UINT>(std::max<LONG>(1, client.right - client.left)),
        static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top)),
        static_cast<double>(GetDpiForWindow(hwnd_)) / 96.0);
    return true;
}

void MainView::Shutdown()
{
    for (auto& item : cards_)
    {
        item->Reset(palette_);
    }
    cards_.clear();
    appSwitcherContainer_ = nullptr;
    layoutCanvas_ = nullptr;
    focusBorder_ = nullptr;
    emptyGrid_ = nullptr;
    emptyIcon_ = nullptr;
    emptyText_ = nullptr;
    root_ = nullptr;
    host_ = nullptr;
    hwnd_ = nullptr;
    initialized_ = false;
}

void MainView::Resize(UINT widthPx, UINT heightPx, double scale)
{
    if (!initialized_)
    {
        return;
    }

    RenderSample(widthPx, heightPx, scale);
}

void MainView::CancelInteraction()
{
    ResetInteractionState();
}

bool MainView::HitTest(PointDip point) const
{
    if (!initialized_)
    {
        return false;
    }

    for (const auto& item : cards_)
    {
        if (!item->visible)
        {
            continue;
        }

        if (point.x >= item->layoutPosition.x && point.y >= item->layoutPosition.y &&
            point.x <= item->layoutPosition.x + item->layoutSize.width &&
            point.y <= item->layoutPosition.y + item->layoutSize.height)
        {
            return true;
        }
    }

    return false;
}

RECT MainView::VisibleBoundsPx() const
{
    return touchrev::common::ScaleToPx(visibleOriginDip_, visibleBoundsDip_, currentDpiScale_);
}

RECT MainView::ContainerBoundsPx() const
{
    return touchrev::common::ScaleToPx(contentOriginDip_, contentBoundsDip_, currentDpiScale_);
}

void MainView::SetDragPosition(PointDip position)
{
    dragPosition_ = position;
}

void MainView::SetBoundsChangedCallback(std::function<void()> callback)
{
    boundsChangedCallback_ = std::move(callback);
}

void MainView::SetMissedInputCallback(std::function<void()> callback)
{
    missedInputCallback_ = std::move(callback);
}

void MainView::SetItemActivatedCallback(std::function<void(HWND)> callback)
{
    itemActivatedCallback_ = std::move(callback);
}

void MainView::SetItemDragReleasedCallback(std::function<void(HWND, POINT)> callback)
{
    itemDragReleasedCallback_ = std::move(callback);
}

void MainView::SetItemCloseRequestedCallback(std::function<bool(HWND)> callback)
{
    itemCloseRequestedCallback_ = std::move(callback);
}

bool MainView::CanNavigateSelection() const
{
    return initialized_ && !xamlPointerPressed_ && !xamlPointerDragging_;
}

bool MainView::SetSelectedIndex(size_t index)
{
    if (index >= cards_.size() || !cards_[index]->visible)
    {
        return false;
    }

    selectedItemIndex_ = index;
    UpdateSelectionVisual();
    return true;
}

bool MainView::MoveSelectionNext()
{
    if (!CanNavigateSelection())
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1))
    {
        return false;
    }

    const size_t nextIndex = LayoutEngine::GetNextVisibleIndex(GetItemGeometries(), selectedItemIndex_, true);
    if (nextIndex != static_cast<size_t>(-1))
    {
        return SetSelectedIndex(nextIndex);
    }
    return false;
}

bool MainView::MoveSelectionPrevious()
{
    if (!CanNavigateSelection())
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1))
    {
        return false;
    }

    const size_t nextIndex = LayoutEngine::GetNextVisibleIndex(GetItemGeometries(), selectedItemIndex_, false);
    if (nextIndex != static_cast<size_t>(-1))
    {
        return SetSelectedIndex(nextIndex);
    }
    return false;
}


bool MainView::MoveSelection(int stepX, int stepY, bool allowWrap)
{
    if (!CanNavigateSelection() || (stepX == 0 && stepY == 0))
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1) || selectedItemIndex_ >= cards_.size())
    {
        return false;
    }

    const size_t nextIndex = LayoutEngine::CalculateNextSelection(
        GetItemGeometries(),
        selectedItemIndex_,
        stepX,
        stepY);

    if (nextIndex != static_cast<size_t>(-1))
    {
        return SetSelectedIndex(nextIndex);
    }

    if (!allowWrap)
    {
        return false;
    }

    if (stepX > 0)
    {
        return MoveSelectionNext();
    }
    if (stepX < 0)
    {
        return MoveSelectionPrevious();
    }
    if (stepY > 0)
    {
        // 向下撞墙，环绕至同一列最上方
        const size_t wrapIndex = LayoutEngine::FindColumnExtreme(GetItemGeometries(), selectedItemIndex_, false);
        if (wrapIndex != static_cast<size_t>(-1) && wrapIndex != selectedItemIndex_)
        {
            return SetSelectedIndex(wrapIndex);
        }
    }
    if (stepY < 0)
    {
        // 向上撞墙，环绕至同一列最下方
        const size_t wrapIndex = LayoutEngine::FindColumnExtreme(GetItemGeometries(), selectedItemIndex_, true);
        if (wrapIndex != static_cast<size_t>(-1) && wrapIndex != selectedItemIndex_)
        {
            return SetSelectedIndex(wrapIndex);
        }
    }
    return false;
}


bool MainView::ActivateSelectedItem()
{
    if (!CanNavigateSelection())
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1) || selectedItemIndex_ >= cards_.size())
    {
        return false;
    }

    HWND hwnd = cards_[selectedItemIndex_]->hwnd;
    if (!hwnd || !itemActivatedCallback_)
    {
        return false;
    }

    itemActivatedCallback_(hwnd);
    return true;
}

void MainView::AttachPointerHandlers()
{
    // Item-level pointer handlers are attached when SwitcherItem instances are created.
}

void MainView::ApplyTheme(const AppSwitcherPalette& palette)
{
    palette_ = palette;

    if (root_)
    {
        root_.Background(Brush(palette_.rootBackdrop));
    }

    if (appSwitcherContainer_)
    {
        appSwitcherContainer_.Background(AcrylicBrush(palette_));
        appSwitcherContainer_.BorderBrush(Brush(palette_.containerBorder));
    }

    if (focusBorder_)
    {
        focusBorder_.BorderBrush(Brush(palette_.focusBorder));
        focusBorder_.Background(Brush(palette_.focusFill));
    }

    if (emptyIcon_)
    {
        emptyIcon_.Foreground(Brush(palette_.secondaryText));
    }

    if (emptyText_)
    {
        emptyText_.Foreground(Brush(palette_.secondaryText));
    }

    for (auto& item : cards_)
    {
        item->ApplyTheme(palette_);
    }
}

void MainView::ResetInteractionState()
{
    xamlPointerDragging_ = false;
    xamlPointerPressed_ = false;
    activeDragItemIndex_ = static_cast<size_t>(-1);
    pressedItemIndex_ = static_cast<size_t>(-1);
    pressPointDip_ = {};

    if (appSwitcherContainer_)
    {
        appSwitcherContainer_.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
    }

    for (auto& item : cards_)
    {
        item->hovered = false;
        item->pressed = false;
        item->grabbed = false;
        if (item->root)
        {
            item->root.Visibility(item->visible
                                     ? winrt::Windows::UI::Xaml::Visibility::Visible
                                     : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        item->ApplyInteractionState(palette_);
    }
}

void MainView::BeginGrab(size_t itemIndex)
{
    if (itemIndex >= cards_.size())
    {
        return;
    }

    xamlPointerDragging_ = true;
    xamlPointerPressed_ = false;
    activeDragItemIndex_ = itemIndex;
    pressedItemIndex_ = static_cast<size_t>(-1);

    if (appSwitcherContainer_)
    {
        appSwitcherContainer_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
    }

    for (size_t i = 0; i < cards_.size(); ++i)
    {
        auto& item = cards_[i];
        item->pressed = false;
        item->grabbed = i == itemIndex;
        if (item->root && item->visible)
        {
            item->root.Visibility(i == itemIndex
                                     ? winrt::Windows::UI::Xaml::Visibility::Visible
                                     : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        item->ApplyInteractionState(palette_);
    }
}

void MainView::FinishPressedItem(size_t itemIndex, PointDip releasePoint)
{
    if (itemIndex >= cards_.size())
    {
        ResetInteractionState();
        return;
    }

    HWND hwnd = cards_[itemIndex]->hwnd;
    const bool wasGrabbed = cards_[itemIndex]->grabbed;
    const POINT releaseScreenPoint = DipPointToScreenPixel(releasePoint);
    ResetInteractionState();

    if (wasGrabbed)
    {
        if (itemDragReleasedCallback_ && hwnd)
        {
            itemDragReleasedCallback_(hwnd, releaseScreenPoint);
        }
        return;
    }

    if (itemActivatedCallback_ && hwnd)
    {
        itemActivatedCallback_(hwnd);
    }
}

void MainView::HandleItemCloseRequested(size_t itemIndex)
{
    if (itemIndex >= cards_.size())
    {
        return;
    }

    HWND hwnd = cards_[itemIndex]->hwnd;
    if (!hwnd)
    {
        return;
    }

    ResetInteractionState();
    if (!itemCloseRequestedCallback_ || !itemCloseRequestedCallback_(hwnd))
    {
        return;
    }

    if (std::find(dismissedHwnds_.begin(), dismissedHwnds_.end(), hwnd) == dismissedHwnds_.end())
    {
        dismissedHwnds_.push_back(hwnd);
    }

    const UINT widthPx = static_cast<UINT>(std::max(1.0, std::round(static_cast<double>(clientSizeDip_.width) * currentDpiScale_)));
    const UINT heightPx = static_cast<UINT>(std::max(1.0, std::round(static_cast<double>(clientSizeDip_.height) * currentDpiScale_)));
    RenderSample(widthPx, heightPx, currentDpiScale_);
}

POINT MainView::DipPointToScreenPixel(PointDip point) const
{
    POINT clientPoint{
        static_cast<LONG>(std::lround(static_cast<double>(point.x) * currentDpiScale_)),
        static_cast<LONG>(std::lround(static_cast<double>(point.y) * currentDpiScale_))};
    ClientToScreen(hwnd_, &clientPoint);
    return clientPoint;
}

void MainView::RenderSample(UINT widthPx, UINT heightPx, double scale)
{
    clientSizeDip_ = {
        static_cast<float>(static_cast<double>(widthPx) / std::max(0.01, scale)),
        static_cast<float>(static_cast<double>(heightPx) / std::max(0.01, scale))};

    dismissedHwnds_.erase(
        std::remove_if(dismissedHwnds_.begin(), dismissedHwnds_.end(), [](HWND hwnd) {
            return !IsWindow(hwnd);
        }),
        dismissedHwnds_.end());

    auto windows = touchrev::appswitcher::EnumerateActiveWindows(hwnd_);
    if (!dismissedHwnds_.empty())
    {
        windows.erase(
            std::remove_if(windows.begin(), windows.end(), [this](const WindowItem& window) {
                return std::find(dismissedHwnds_.begin(), dismissedHwnds_.end(), window.hwnd) != dismissedHwnds_.end();
            }),
            windows.end());
    }

    std::wstringstream log;
    log << L"EnumerateActiveWindows: count=" << windows.size();
    for (const auto& window : windows)
    {
        log << L"\n  hwnd=" << window.hwnd << L" title=" << window.title;
    }
    DebugLog(log.str());
    ApplyLayout(windows, widthPx, heightPx, scale);
}

bool MainView::LoadRoot()
{
    try
    {
        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(touchrev::common::ModuleRelativePath(kRootXamlPath));
        if (xaml.empty())
        {
            DebugLog(L"AppSwitcherRoot.xaml is empty or missing.");
            return false;
        }

        auto object = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{xaml});
        root_ = object.as<winrt::Windows::UI::Xaml::Controls::Grid>();
        appSwitcherContainer_ = root_.FindName(L"AppSwitcherContainer").as<winrt::Windows::UI::Xaml::Controls::Border>();
        layoutCanvas_ = root_.FindName(L"LayoutCanvas").as<winrt::Windows::UI::Xaml::Controls::Canvas>();
        focusBorder_ = root_.FindName(L"FocusBorder").as<winrt::Windows::UI::Xaml::Controls::Border>();
        emptyGrid_ = root_.FindName(L"EmptyGrid").as<winrt::Windows::UI::Xaml::FrameworkElement>();
        emptyIcon_ = root_.FindName(L"EmptyIcon").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        emptyText_ = root_.FindName(L"EmptyText").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();

        root_.PointerPressed([this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!root_)
            {
                return;
            }

            const auto point = args.GetCurrentPoint(root_).Position();
            if (!HitTest({static_cast<float>(point.X), static_cast<float>(point.Y)}))
            {
                args.Handled(true);
                if (missedInputCallback_)
                {
                    missedInputCallback_();
                }
            }
        });

        host_->SetRoot(root_);
        ApplyTheme(palette_);
        AttachPointerHandlers();
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"XamlReader::Load(AppSwitcherRoot)", error.code());
        return false;
    }
}

std::unique_ptr<CardView> MainView::CreateItem()
{
    const size_t itemIndex = cards_.size();
    CardCallbacks callbacks;

    callbacks.onCloseClicked = [this](size_t index) {
        HandleItemCloseRequested(index);
    };

    callbacks.onPointerPressed = [this](size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        ProcessItemPressed(index, args);
    };

    callbacks.onPointerMoved = [this](size_t, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        ProcessItemMoved(args);
    };

    callbacks.onPointerReleased = [this](size_t, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        ProcessItemReleased(args);
    };

    callbacks.onPointerCanceled = [this](size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (index < cards_.size())
        {
            cards_[index]->root.ReleasePointerCapture(args.Pointer());
        }
        ResetInteractionState();
        args.Handled(true);
    };

    auto item = CardView::Create(palette_, itemIndex, callbacks);
    if (!item)
    {
        return nullptr;
    }

    winrt::Windows::UI::Xaml::Controls::Canvas::SetZIndex(item->root, 10);
    layoutCanvas_.Children().Append(item->root);
    return item;
}

void MainView::ProcessItemPressed(size_t index, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)
{
    DebugLog(L"[DEBUG] ProcessItemPressed index=" + std::to_wstring(index));
    if (!root_ || index >= cards_.size())
    {
        return;
    }

    const auto point = args.GetCurrentPoint(root_).Position();
    const PointDip logicalPoint{
        static_cast<float>(point.X + visibleOriginDip_.x),
        static_cast<float>(point.Y + visibleOriginDip_.y)};
    selectedItemIndex_ = index;
    pressedItemIndex_ = index;
    activeDragItemIndex_ = index;
    xamlPointerPressed_ = true;
    xamlPointerDragging_ = false;
    pressPointDip_ = logicalPoint;
    xamlDragOffsetX_ = logicalPoint.x - cards_[index]->layoutPosition.x;
    xamlDragOffsetY_ = logicalPoint.y - cards_[index]->layoutPosition.y;
    cards_[index]->pressed = true;
    cards_[index]->hovered = false;
    cards_[index]->ApplyInteractionState(palette_);
    cards_[index]->root.CapturePointer(args.Pointer());
    args.Handled(true);
}

void MainView::ProcessItemMoved(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)
{
    DebugLog(L"[DEBUG] ProcessItemMoved pressed=" + std::to_wstring(xamlPointerPressed_) + L" dragging=" + std::to_wstring(xamlPointerDragging_) + L" activeIndex=" + std::to_wstring(activeDragItemIndex_));
    if ((!xamlPointerPressed_ && !xamlPointerDragging_) || !root_ || activeDragItemIndex_ >= cards_.size())
    {
        return;
    }

    const auto point = args.GetCurrentPoint(root_).Position();
    const PointDip logicalPoint{
        static_cast<float>(point.X + visibleOriginDip_.x),
        static_cast<float>(point.Y + visibleOriginDip_.y)};
    auto& activeItem = *cards_[activeDragItemIndex_];
    constexpr double dragThresholdDip = 10.0;
    if (xamlPointerPressed_ && touchrev::common::Distance(logicalPoint, pressPointDip_) > dragThresholdDip)
    {
        BeginGrab(activeDragItemIndex_);
    }

    if (xamlPointerDragging_)
    {
        activeItem.layoutPosition = {
            static_cast<float>(std::clamp(static_cast<double>(logicalPoint.x) - xamlDragOffsetX_, 0.0, std::max(0.0, static_cast<double>(clientSizeDip_.width) - activeItem.layoutSize.width))),
            static_cast<float>(std::clamp(static_cast<double>(logicalPoint.y) - xamlDragOffsetY_, 0.0, std::max(0.0, static_cast<double>(clientSizeDip_.height) - activeItem.layoutSize.height)))};
        UpdateVisibleBoundsAndPositions();
        if (boundsChangedCallback_)
        {
            boundsChangedCallback_();
        }
    }
    args.Handled(true);
}

void MainView::ProcessItemReleased(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args)
{
    if (!xamlPointerPressed_ && !xamlPointerDragging_)
    {
        return;
    }

    const size_t itemIndexToFinish = activeDragItemIndex_;
    const auto point = root_ ? args.GetCurrentPoint(root_).Position() : winrt::Windows::Foundation::Point{};
    const PointDip releasePoint{
        static_cast<float>(point.X + visibleOriginDip_.x),
        static_cast<float>(point.Y + visibleOriginDip_.y)};
    if (itemIndexToFinish < cards_.size())
    {
        cards_[itemIndexToFinish]->root.ReleasePointerCapture(args.Pointer());
    }
    FinishPressedItem(itemIndexToFinish, releasePoint);
    args.Handled(true);
}


void MainView::EnsureItemCount(size_t count)
{
    while (cards_.size() < count)
    {
        auto item = CreateItem();
        if (!item || !item->root)
        {
            break;
        }
        cards_.push_back(std::move(item));
    }
}

void MainView::UpdateVisibleBoundsAndPositions()
{
    visibleOriginDip_ = {};
    visibleBoundsDip_ = {
        std::max(1.0f, clientSizeDip_.width),
        std::max(1.0f, clientSizeDip_.height)};

    // Let the root Grid automatically stretch to fill the host container instead of hardcoding DIP size.

    if (layoutCanvas_)
    {
        layoutCanvas_.Width(visibleBoundsDip_.width);
        layoutCanvas_.Height(visibleBoundsDip_.height);
    }

    if (appSwitcherContainer_)
    {
        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(appSwitcherContainer_, contentOriginDip_.x);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(appSwitcherContainer_, contentOriginDip_.y);
        appSwitcherContainer_.Width(contentBoundsDip_.width);
        appSwitcherContainer_.Height(contentBoundsDip_.height);
    }

    for (auto& item : cards_)
    {
        if (!item->root || !item->visible)
        {
            continue;
        }

        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(item->root, item->layoutPosition.x);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(item->root, item->layoutPosition.y);
    }

    EnsureSelectedIndex();
    UpdateSelectionVisual();
}

void MainView::EnsureSelectedIndex()
{
    if (selectedItemIndex_ < cards_.size() && cards_[selectedItemIndex_]->visible)
    {
        return;
    }

    selectedItemIndex_ = static_cast<size_t>(-1);
    for (size_t i = 0; i < cards_.size(); ++i)
    {
        if (cards_[i]->visible)
        {
            selectedItemIndex_ = i;
            return;
        }
    }
}

void MainView::UpdateSelectionVisual()
{
    if (!focusBorder_)
    {
        return;
    }

    if (xamlPointerDragging_ || selectedItemIndex_ == static_cast<size_t>(-1) ||
        selectedItemIndex_ >= cards_.size() || !cards_[selectedItemIndex_]->visible)
    {
        focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        return;
    }

    const auto& selectedItem = *cards_[selectedItemIndex_];
    constexpr double inflationDip = 10.0;
    winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(
        focusBorder_,
        selectedItem.layoutPosition.x - inflationDip);
    winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(
        focusBorder_,
        selectedItem.layoutPosition.y - inflationDip);
    focusBorder_.Width(selectedItem.layoutSize.width + inflationDip * 2.0);
    focusBorder_.Height(selectedItem.layoutSize.height + inflationDip * 2.0);
    focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
}

void MainView::ApplyLayout(
    const std::vector<WindowItem>& windows,
    UINT widthPx,
    UINT heightPx,
    double scale)
{
    if (!layoutCanvas_)
    {
        return;
    }

    EnsureItemCount(windows.size());
    const double safeScale = std::max(0.01, scale);
    currentDpiScale_ = safeScale;
    RECT workArea{0, 0, static_cast<LONG>(std::max(1u, widthPx)), static_cast<LONG>(std::max(1u, heightPx))};
    const LayoutResult layout = LayoutEngine::Calculate(windows, workArea, safeScale);
    contentBoundsDip_ = {
        static_cast<float>(static_cast<double>(layout.totalSizePx.cx) / safeScale),
        static_cast<float>(static_cast<double>(layout.totalSizePx.cy) / safeScale)};
    contentOriginDip_ = {
        static_cast<float>(std::max(0.0, (static_cast<double>(clientSizeDip_.width) - contentBoundsDip_.width) * 0.5)),
        static_cast<float>(std::max(0.0, (static_cast<double>(clientSizeDip_.height) - contentBoundsDip_.height) * 0.5))};
    if (activeDragItemIndex_ >= windows.size())
    {
        xamlPointerDragging_ = false;
        xamlPointerPressed_ = false;
        activeDragItemIndex_ = static_cast<size_t>(-1);
        pressedItemIndex_ = static_cast<size_t>(-1);
    }

    for (size_t i = 0; i < cards_.size(); ++i)
    {
        const bool visible = i < windows.size() && i < layout.items.size();
        auto& item = *cards_[i];
        if (!item.root)
        {
            continue;
        }

        item.SetRootVisibility(visible);
        if (!visible)
        {
            item.Reset(palette_);
            continue;
        }

        const RECT& rect = layout.items[i].rectPx;
        const double x = static_cast<double>(rect.left) / safeScale;
        const double y = static_cast<double>(rect.top) / safeScale;
        const double w = static_cast<double>(touchrev::common::RectWidth(rect)) / safeScale;
        const double h = static_cast<double>(touchrev::common::RectHeight(rect)) / safeScale;

        item.UpdateState(
            windows[i].hwnd,
            windows[i].title,
            i + 1,
            contentOriginDip_.x + x,
            contentOriginDip_.y + y,
            w,
            h,
            xamlPointerDragging_ && activeDragItemIndex_ == i,
            thumbnailManager_,
            currentDpiScale_);
    }

    if (emptyGrid_)
    {
        emptyGrid_.Visibility(windows.empty()
                                  ? winrt::Windows::UI::Xaml::Visibility::Visible
                                  : winrt::Windows::UI::Xaml::Visibility::Collapsed);
    }

    UpdateVisibleBoundsAndPositions();
    if (boundsChangedCallback_)
    {
        boundsChangedCallback_();
    }
}

bool MainView::AccumulateAndMoveSelection(double deltaX, double deltaY)
{
    if (!CanNavigateSelection())
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1) || selectedItemIndex_ >= cards_.size())
    {
        return false;
    }

    gestureAccX_ += deltaX;
    gestureAccY_ += deltaY;

    double currentSnapThresholdX = 50.0;
    double currentSnapThresholdY = 50.0;

    auto& card = cards_[selectedItemIndex_];
    if (card && card->layoutSize.width > 0 && card->layoutSize.height > 0)
    {
        double gap = 32.0;
        double ratio = 0.4;
        currentSnapThresholdX = (card->layoutSize.width + gap) * ratio;
        currentSnapThresholdY = (card->layoutSize.height + gap) * ratio;
    }

    bool selectedChanged = false;

    if (std::abs(gestureAccX_) > currentSnapThresholdX)
    {
        int steps = static_cast<int>(gestureAccX_ / currentSnapThresholdX);
        if (steps != 0)
        {
            if (MoveSelection(steps, 0, false))
            {
                selectedChanged = true;
            }
            gestureAccX_ = std::fmod(gestureAccX_, currentSnapThresholdX);
        }
    }

    if (std::abs(gestureAccY_) > currentSnapThresholdY)
    {
        int steps = static_cast<int>(gestureAccY_ / currentSnapThresholdY);
        if (steps != 0)
        {
            if (MoveSelection(0, steps, false))
            {
                selectedChanged = true;
            }
            gestureAccY_ = std::fmod(gestureAccY_, currentSnapThresholdY);
        }
    }

    return selectedChanged;
}

void MainView::ClearGestureAccumulator()
{
    gestureAccX_ = 0.0;
    gestureAccY_ = 0.0;
}

void MainView::ResetSelection()
{
    selectedItemIndex_ = static_cast<size_t>(-1);
}
}



