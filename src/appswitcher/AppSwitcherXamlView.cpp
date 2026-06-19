#include "AppSwitcherXamlView.h"

#include "AppSwitcherWindowing.h"
#include "common/Win32Error.h"

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
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
constexpr wchar_t kRootXamlPath[] = L"xaml/AppSwitcherRoot.xaml";
constexpr wchar_t kItemXamlPath[] = L"xaml/SwitcherItem.xaml";

int RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

double Distance(PointDip a, PointDip b)
{
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

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

std::wstring ToWideFromUtf8(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (length <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        length);
    return wide;
}
}

bool AppSwitcherXamlView::Initialize(HWND hwnd, ThinXamlAppSwitcherHost& host)
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

void AppSwitcherXamlView::Shutdown()
{
    for (auto& item : items_)
    {
        item.Reset(palette_);
    }
    items_.clear();
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

void AppSwitcherXamlView::Resize(UINT widthPx, UINT heightPx, double scale)
{
    if (!initialized_)
    {
        return;
    }

    RenderSample(widthPx, heightPx, scale);
}

void AppSwitcherXamlView::CancelInteraction()
{
    ResetInteractionState();
}

bool AppSwitcherXamlView::HitTest(PointDip point) const
{
    if (!initialized_)
    {
        return false;
    }

    for (const auto& item : items_)
    {
        if (!item.visible)
        {
            continue;
        }

        if (point.x >= item.layoutPosition.x && point.y >= item.layoutPosition.y &&
            point.x <= item.layoutPosition.x + item.layoutSize.width &&
            point.y <= item.layoutPosition.y + item.layoutSize.height)
        {
            return true;
        }
    }

    return false;
}

RECT AppSwitcherXamlView::VisibleBoundsPx() const
{
    const double safeScale = std::max(0.01, currentDpiScale_);
    return {
        static_cast<LONG>(std::floor(static_cast<double>(visibleOriginDip_.x) * safeScale)),
        static_cast<LONG>(std::floor(static_cast<double>(visibleOriginDip_.y) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(visibleOriginDip_.x + visibleBoundsDip_.width) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(visibleOriginDip_.y + visibleBoundsDip_.height) * safeScale))};
}

RECT AppSwitcherXamlView::ContainerBoundsPx() const
{
    const double safeScale = std::max(0.01, currentDpiScale_);
    return {
        static_cast<LONG>(std::floor(static_cast<double>(contentOriginDip_.x) * safeScale)),
        static_cast<LONG>(std::floor(static_cast<double>(contentOriginDip_.y) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(contentOriginDip_.x + contentBoundsDip_.width) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(contentOriginDip_.y + contentBoundsDip_.height) * safeScale))};
}

void AppSwitcherXamlView::SetDragPosition(PointDip position)
{
    dragPosition_ = position;
}

void AppSwitcherXamlView::SetBoundsChangedCallback(std::function<void()> callback)
{
    boundsChangedCallback_ = std::move(callback);
}

void AppSwitcherXamlView::SetMissedInputCallback(std::function<void()> callback)
{
    missedInputCallback_ = std::move(callback);
}

void AppSwitcherXamlView::SetItemActivatedCallback(std::function<void(HWND)> callback)
{
    itemActivatedCallback_ = std::move(callback);
}

void AppSwitcherXamlView::SetItemDragReleasedCallback(std::function<void(HWND, POINT)> callback)
{
    itemDragReleasedCallback_ = std::move(callback);
}

void AppSwitcherXamlView::SetItemCloseRequestedCallback(std::function<bool(HWND)> callback)
{
    itemCloseRequestedCallback_ = std::move(callback);
}

bool AppSwitcherXamlView::CanNavigateSelection() const
{
    return initialized_ && !xamlPointerPressed_ && !xamlPointerDragging_;
}

bool AppSwitcherXamlView::SetSelectedIndex(size_t index)
{
    if (index >= items_.size() || !items_[index].visible)
    {
        return false;
    }

    selectedItemIndex_ = index;
    UpdateSelectionVisual();
    return true;
}

bool AppSwitcherXamlView::MoveSelectionNext()
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

    for (size_t i = selectedItemIndex_ + 1; i < items_.size(); ++i)
    {
        if (items_[i].visible)
        {
            return SetSelectedIndex(i);
        }
    }

    for (size_t i = 0; i < selectedItemIndex_; ++i)
    {
        if (items_[i].visible)
        {
            return SetSelectedIndex(i);
        }
    }

    return false;
}

bool AppSwitcherXamlView::MoveSelectionPrevious()
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

    for (size_t i = selectedItemIndex_; i > 0; --i)
    {
        const size_t candidate = i - 1;
        if (items_[candidate].visible)
        {
            return SetSelectedIndex(candidate);
        }
    }

    for (size_t i = items_.size(); i > selectedItemIndex_ + 1; --i)
    {
        const size_t candidate = i - 1;
        if (items_[candidate].visible)
        {
            return SetSelectedIndex(candidate);
        }
    }

    return false;
}

bool AppSwitcherXamlView::MoveSelection(int stepX, int stepY)
{
    if (!CanNavigateSelection() || (stepX == 0 && stepY == 0))
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1) || selectedItemIndex_ >= items_.size())
    {
        return false;
    }

    const auto& current = items_[selectedItemIndex_];
    const double currentCenterX = current.layoutPosition.x + current.layoutSize.width * 0.5;
    const double currentCenterY = current.layoutPosition.y + current.layoutSize.height * 0.5;
    size_t bestIndex = static_cast<size_t>(-1);
    double bestDistance = std::numeric_limits<double>::max();

    for (size_t i = 0; i < items_.size(); ++i)
    {
        if (i == selectedItemIndex_ || !items_[i].visible)
        {
            continue;
        }

        const auto& candidate = items_[i];
        const double candidateCenterX = candidate.layoutPosition.x + candidate.layoutSize.width * 0.5;
        const double candidateCenterY = candidate.layoutPosition.y + candidate.layoutSize.height * 0.5;
        const double dx = candidateCenterX - currentCenterX;
        const double dy = candidateCenterY - currentCenterY;
        bool matchesDirection = false;

        if (stepX > 0 && dx > 10.0 && std::abs(dy) < candidate.layoutSize.height)
        {
            matchesDirection = true;
        }
        else if (stepX < 0 && dx < -10.0 && std::abs(dy) < candidate.layoutSize.height)
        {
            matchesDirection = true;
        }
        else if (stepY > 0 && dy > 10.0 && std::abs(dx) < candidate.layoutSize.width)
        {
            matchesDirection = true;
        }
        else if (stepY < 0 && dy < -10.0 && std::abs(dx) < candidate.layoutSize.width)
        {
            matchesDirection = true;
        }

        if (!matchesDirection)
        {
            continue;
        }

        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex != static_cast<size_t>(-1))
    {
        return SetSelectedIndex(bestIndex);
    }

    if (stepX > 0)
    {
        return MoveSelectionNext();
    }
    if (stepX < 0)
    {
        return MoveSelectionPrevious();
    }
    return false;
}

bool AppSwitcherXamlView::ActivateSelectedItem()
{
    if (!CanNavigateSelection())
    {
        return false;
    }

    EnsureSelectedIndex();
    if (selectedItemIndex_ == static_cast<size_t>(-1) || selectedItemIndex_ >= items_.size())
    {
        return false;
    }

    HWND hwnd = items_[selectedItemIndex_].hwnd;
    if (!hwnd || !itemActivatedCallback_)
    {
        return false;
    }

    itemActivatedCallback_(hwnd);
    return true;
}

void AppSwitcherXamlView::AttachPointerHandlers()
{
    // Item-level pointer handlers are attached when SwitcherItem instances are created.
}

void AppSwitcherXamlView::ApplyTheme(const AppSwitcherPalette& palette)
{
    palette_ = palette;

    if (root_)
    {
        root_.Background(Brush(palette_.rootBackdrop));
    }

    if (appSwitcherContainer_)
    {
        appSwitcherContainer_.Background(Brush(palette_.containerBackground));
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

    for (auto& item : items_)
    {
        item.ApplyTheme(palette_);
    }
}

void AppSwitcherXamlView::ResetInteractionState()
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

    for (auto& item : items_)
    {
        item.hovered = false;
        item.pressed = false;
        item.grabbed = false;
        if (item.root)
        {
            item.root.Visibility(item.visible
                                     ? winrt::Windows::UI::Xaml::Visibility::Visible
                                     : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        item.ApplyInteractionState(palette_);
    }
}

void AppSwitcherXamlView::BeginGrab(size_t itemIndex)
{
    if (itemIndex >= items_.size())
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

    for (size_t i = 0; i < items_.size(); ++i)
    {
        auto& item = items_[i];
        item.pressed = false;
        item.grabbed = i == itemIndex;
        if (item.root && item.visible)
        {
            item.root.Visibility(i == itemIndex
                                     ? winrt::Windows::UI::Xaml::Visibility::Visible
                                     : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        item.ApplyInteractionState(palette_);
    }
}

void AppSwitcherXamlView::FinishPressedItem(size_t itemIndex, PointDip releasePoint)
{
    if (itemIndex >= items_.size())
    {
        ResetInteractionState();
        return;
    }

    HWND hwnd = items_[itemIndex].hwnd;
    const bool wasGrabbed = items_[itemIndex].grabbed;
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

void AppSwitcherXamlView::HandleItemCloseRequested(size_t itemIndex)
{
    if (itemIndex >= items_.size())
    {
        return;
    }

    HWND hwnd = items_[itemIndex].hwnd;
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

POINT AppSwitcherXamlView::DipPointToScreenPixel(PointDip point) const
{
    POINT clientPoint{
        static_cast<LONG>(std::lround(static_cast<double>(point.x) * currentDpiScale_)),
        static_cast<LONG>(std::lround(static_cast<double>(point.y) * currentDpiScale_))};
    ClientToScreen(hwnd_, &clientPoint);
    return clientPoint;
}

void AppSwitcherXamlView::RenderSample(UINT widthPx, UINT heightPx, double scale)
{
    clientSizeDip_ = {
        static_cast<float>(static_cast<double>(widthPx) / std::max(0.01, scale)),
        static_cast<float>(static_cast<double>(heightPx) / std::max(0.01, scale))};

    dismissedHwnds_.erase(
        std::remove_if(dismissedHwnds_.begin(), dismissedHwnds_.end(), [](HWND hwnd) {
            return !IsWindow(hwnd);
        }),
        dismissedHwnds_.end());

    auto windows = touchrev::appswitcher::EnumerateSwitcherWindows(hwnd_);
    if (!dismissedHwnds_.empty())
    {
        windows.erase(
            std::remove_if(windows.begin(), windows.end(), [this](const AppSwitcherWindowItem& window) {
                return std::find(dismissedHwnds_.begin(), dismissedHwnds_.end(), window.hwnd) != dismissedHwnds_.end();
            }),
            windows.end());
    }

    std::wstringstream log;
    log << L"EnumerateSwitcherWindows: count=" << windows.size();
    for (const auto& window : windows)
    {
        log << L"\n  hwnd=" << window.hwnd << L" title=" << window.title;
    }
    DebugLog(log.str());
    ApplyLayout(windows, widthPx, heightPx, scale);
}

bool AppSwitcherXamlView::LoadRoot()
{
    try
    {
        const std::wstring xaml = LoadTextFileUtf8(ModuleRelativePath(kRootXamlPath));
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

AppSwitcherItemView AppSwitcherXamlView::CreateItem()
{
    AppSwitcherItemView item;
    try
    {
        const std::wstring xaml = LoadTextFileUtf8(ModuleRelativePath(kItemXamlPath));
        auto object = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{xaml});
        item.root = object.as<winrt::Windows::UI::Xaml::FrameworkElement>();
        item.transform = item.root.FindName(L"ItemTransform").as<winrt::Windows::UI::Xaml::Media::CompositeTransform>();
        item.layoutGrid = item.root.FindName(L"ItemLayoutGrid").as<winrt::Windows::UI::Xaml::Controls::Grid>();
        item.mainCard = item.root.FindName(L"MainCard").as<winrt::Windows::UI::Xaml::Controls::Border>();
        item.titleBorder = item.root.FindName(L"TitleBorder").as<winrt::Windows::UI::Xaml::Controls::Border>();
        item.title = item.root.FindName(L"TitleText").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        item.defaultIcon = item.root.FindName(L"DefaultIcon").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        item.closeButton = item.root.FindName(L"CloseButton").as<winrt::Windows::UI::Xaml::Controls::Button>();
        item.thumbnailHost = item.root.FindName(L"ContentFrame").as<winrt::Windows::UI::Xaml::Controls::Border>();
        item.pressOverlay = item.root.FindName(L"PressOverlay").as<winrt::Windows::UI::Xaml::Controls::Border>();
        item.ApplyRowWeights();
        item.ApplyTheme(palette_);

        const size_t itemIndex = items_.size();
        item.root.PointerEntered([this, itemIndex](auto const&, auto const&) {
            if (itemIndex >= items_.size() || xamlPointerPressed_ || xamlPointerDragging_)
            {
                return;
            }

            items_[itemIndex].hovered = true;
            items_[itemIndex].ApplyInteractionState(palette_);
        });

        item.root.PointerExited([this, itemIndex](auto const&, auto const&) {
            if (itemIndex >= items_.size() || xamlPointerPressed_ || xamlPointerDragging_)
            {
                return;
            }

            items_[itemIndex].hovered = false;
            items_[itemIndex].ApplyInteractionState(palette_);
        });

        if (item.closeButton)
        {
            item.closeButton.Click([this, itemIndex](auto const&, winrt::Windows::UI::Xaml::RoutedEventArgs const&) {
                HandleItemCloseRequested(itemIndex);
            });
            item.closeButton.PointerEntered([this, itemIndex](auto const&, auto const&) {
                if (itemIndex < items_.size())
                {
                    items_[itemIndex].ApplyCloseButtonHoverState(palette_, true);
                }
            });
            item.closeButton.PointerExited([this, itemIndex](auto const&, auto const&) {
                if (itemIndex < items_.size())
                {
                    items_[itemIndex].ApplyCloseButtonHoverState(palette_, false);
                }
            });
        }

        item.root.PointerPressed([this, itemIndex](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!root_ || itemIndex >= items_.size())
            {
                return;
            }

            const auto point = args.GetCurrentPoint(root_).Position();
            const PointDip logicalPoint{
                static_cast<float>(point.X + visibleOriginDip_.x),
                static_cast<float>(point.Y + visibleOriginDip_.y)};
            selectedItemIndex_ = itemIndex;
            pressedItemIndex_ = itemIndex;
            activeDragItemIndex_ = itemIndex;
            xamlPointerPressed_ = true;
            xamlPointerDragging_ = false;
            pressPointDip_ = logicalPoint;
            xamlDragOffsetX_ = logicalPoint.x - items_[itemIndex].layoutPosition.x;
            xamlDragOffsetY_ = logicalPoint.y - items_[itemIndex].layoutPosition.y;
            items_[itemIndex].pressed = true;
            items_[itemIndex].hovered = false;
            items_[itemIndex].ApplyInteractionState(palette_);
            sender.template as<winrt::Windows::UI::Xaml::UIElement>().CapturePointer(args.Pointer());
            args.Handled(true);
        });

        item.root.PointerMoved([this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if ((!xamlPointerPressed_ && !xamlPointerDragging_) || !root_ || activeDragItemIndex_ >= items_.size())
            {
                return;
            }

            const auto point = args.GetCurrentPoint(root_).Position();
            const PointDip logicalPoint{
                static_cast<float>(point.X + visibleOriginDip_.x),
                static_cast<float>(point.Y + visibleOriginDip_.y)};
            auto& activeItem = items_[activeDragItemIndex_];
            constexpr double dragThresholdDip = 10.0;
            if (xamlPointerPressed_ && Distance(logicalPoint, pressPointDip_) > dragThresholdDip)
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
        });

        auto endInteraction = [this](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!xamlPointerPressed_ && !xamlPointerDragging_)
            {
                return;
            }

            const size_t itemIndexToFinish = activeDragItemIndex_;
            const auto point = root_ ? args.GetCurrentPoint(root_).Position() : winrt::Windows::Foundation::Point{};
            const PointDip releasePoint{
                static_cast<float>(point.X + visibleOriginDip_.x),
                static_cast<float>(point.Y + visibleOriginDip_.y)};
            sender.template as<winrt::Windows::UI::Xaml::UIElement>().ReleasePointerCapture(args.Pointer());
            FinishPressedItem(itemIndexToFinish, releasePoint);
            args.Handled(true);
        };

        item.root.PointerReleased(endInteraction);
        item.root.PointerCanceled([this](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            sender.template as<winrt::Windows::UI::Xaml::UIElement>().ReleasePointerCapture(args.Pointer());
            ResetInteractionState();
            args.Handled(true);
        });

        winrt::Windows::UI::Xaml::Controls::Canvas::SetZIndex(item.root, 10);
        layoutCanvas_.Children().Append(item.root);
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"XamlReader::Load(SwitcherItem)", error.code());
    }
    return item;
}

void AppSwitcherXamlView::EnsureItemCount(size_t count)
{
    while (items_.size() < count)
    {
        AppSwitcherItemView item = CreateItem();
        if (!item.root)
        {
            break;
        }
        items_.push_back(std::move(item));
    }
}

void AppSwitcherXamlView::UpdateVisibleBoundsAndPositions()
{
    visibleOriginDip_ = {};
    visibleBoundsDip_ = {
        std::max(1.0f, clientSizeDip_.width),
        std::max(1.0f, clientSizeDip_.height)};

    if (root_)
    {
        root_.Width(visibleBoundsDip_.width);
        root_.Height(visibleBoundsDip_.height);
    }

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

    for (auto& item : items_)
    {
        if (!item.root || !item.visible)
        {
            continue;
        }

        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(item.root, item.layoutPosition.x);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(item.root, item.layoutPosition.y);
    }

    EnsureSelectedIndex();
    UpdateSelectionVisual();
}

void AppSwitcherXamlView::EnsureSelectedIndex()
{
    if (selectedItemIndex_ < items_.size() && items_[selectedItemIndex_].visible)
    {
        return;
    }

    selectedItemIndex_ = static_cast<size_t>(-1);
    for (size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].visible)
        {
            selectedItemIndex_ = i;
            return;
        }
    }
}

void AppSwitcherXamlView::UpdateSelectionVisual()
{
    if (!focusBorder_)
    {
        return;
    }

    if (xamlPointerDragging_ || selectedItemIndex_ == static_cast<size_t>(-1) ||
        selectedItemIndex_ >= items_.size() || !items_[selectedItemIndex_].visible)
    {
        focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        return;
    }

    const auto& selectedItem = items_[selectedItemIndex_];
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

void AppSwitcherXamlView::ApplyLayout(
    const std::vector<AppSwitcherWindowItem>& windows,
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
    const AppSwitcherLayoutResult layout = AppSwitcherLayoutEngine::Calculate(windows, workArea, safeScale);
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

    for (size_t i = 0; i < items_.size(); ++i)
    {
        const bool visible = i < windows.size() && i < layout.items.size();
        auto& item = items_[i];
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
        const double w = static_cast<double>(RectWidth(rect)) / safeScale;
        const double h = static_cast<double>(RectHeight(rect)) / safeScale;

        item.AssignWindow(windows[i].hwnd);
        item.visible = true;
        item.layoutSize = {static_cast<float>(w), static_cast<float>(h)};
        if (!xamlPointerDragging_ || activeDragItemIndex_ != i)
        {
            item.layoutPosition = {
                static_cast<float>(contentOriginDip_.x + x),
                static_cast<float>(contentOriginDip_.y + y)};
        }
        item.root.Width(w);
        item.root.Height(h);
        item.ApplyTitle(windows[i].title, i + 1);
        item.ApplyCloseButtonWidth(std::max(28.0, h * 0.18));

        const double thumbnailWidth = std::max(1.0, w);
        const double thumbnailHeight = std::max(
            1.0,
            h * AppSwitcherLayoutEngine::ContentRowWeight / AppSwitcherLayoutEngine::TotalRowWeight);
        item.EnsureThumbnail(thumbnailManager_, thumbnailWidth, thumbnailHeight, currentDpiScale_);
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

std::wstring AppSwitcherXamlView::LoadTextFileUtf8(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return ToWideFromUtf8(stream.str());
}

std::wstring AppSwitcherXamlView::ModuleRelativePath(const std::wstring& relativePath)
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    std::wstring path(modulePath, modulePath + length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += relativePath;
    return path;
}
