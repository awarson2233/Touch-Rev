#include "AppSwitcherXamlView.h"

#include "common/Win32Error.h"

#include <dwmapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <cmath>
#include <fstream>
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

LONG RectIntersectionArea(const RECT& a, const RECT& b)
{
    const LONG left = std::max(a.left, b.left);
    const LONG top = std::max(a.top, b.top);
    const LONG right = std::min(a.right, b.right);
    const LONG bottom = std::min(a.bottom, b.bottom);
    if (right <= left || bottom <= top)
    {
        return 0;
    }
    return (right - left) * (bottom - top);
}

RECT MonitorRect(HMONITOR monitor)
{
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        return {0, 0, 0, 0};
    }
    return monitorInfo.rcMonitor;
}

bool WindowBelongsToMonitor(const RECT& windowRect, HMONITOR targetMonitor)
{
    if (!targetMonitor)
    {
        return true;
    }

    const RECT targetRect = MonitorRect(targetMonitor);
    const LONG targetArea = RectIntersectionArea(windowRect, targetRect);
    if (targetArea <= 0)
    {
        return false;
    }

    struct EnumState
    {
        RECT windowRect{};
        HMONITOR targetMonitor = nullptr;
        LONG bestArea = 0;
        HMONITOR bestMonitor = nullptr;
    } state{windowRect, targetMonitor};

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT monitorRect, LPARAM param) -> BOOL {
            auto& state = *reinterpret_cast<EnumState*>(param);
            const LONG area = RectIntersectionArea(state.windowRect, *monitorRect);
            if (area > state.bestArea)
            {
                state.bestArea = area;
                state.bestMonitor = monitor;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));

    return state.bestMonitor == targetMonitor;
}

std::wstring GetWindowDisplayTitle(HWND hwnd)
{
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (title[0] != L'\0')
    {
        return title;
    }

    wchar_t className[128]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (className[0] != L'\0')
    {
        std::wstringstream fallback;
        fallback << className << L" " << hwnd;
        return fallback.str();
    }

    std::wstringstream fallback;
    fallback << L"Window " << hwnd;
    return fallback.str();
}

bool IsAltTabLikeWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd))
    {
        return false;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0)
    {
        return false;
    }

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
    {
        return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect))
    {
        return false;
    }

    return rect.right - rect.left >= 80 && rect.bottom - rect.top >= 60;
}

std::vector<AppSwitcherWindowItem> EnumerateSwitcherWindows(HWND excludeHwnd, HMONITOR targetMonitor)
{
    struct EnumState
    {
        HWND exclude = nullptr;
        HMONITOR targetMonitor = nullptr;
        std::vector<AppSwitcherWindowItem> windows;
    } state{excludeHwnd, targetMonitor};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto& state = *reinterpret_cast<EnumState*>(param);
            if (hwnd == state.exclude || !IsAltTabLikeWindow(hwnd))
            {
                return TRUE;
            }

            RECT rect{};
            GetWindowRect(hwnd, &rect);
            if (!WindowBelongsToMonitor(rect, state.targetMonitor))
            {
                return TRUE;
            }

            state.windows.push_back({
                hwnd,
                static_cast<double>(std::max<LONG>(1, rect.right - rect.left)),
                static_cast<double>(std::max<LONG>(1, rect.bottom - rect.top)),
                GetWindowDisplayTitle(hwnd)});
            return state.windows.size() < 12;
        },
        reinterpret_cast<LPARAM>(&state));

    return state.windows;
}

void ApplyContentClip(winrt::Windows::UI::Xaml::FrameworkElement const& element, double width, double height)
{
    auto clip = winrt::Windows::UI::Xaml::Media::RectangleGeometry();
    clip.Rect({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
    element.Clip(clip);
}

double Distance(PointDip a, PointDip b)
{
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

bool ShouldRecreateThumbnail(
    const touchrev::thumbnail::PrivateThumbnailSlot& slot,
    HWND hwnd,
    double widthDip,
    double heightDip,
    double dpiScale)
{
    return slot.hwnd != hwnd ||
           std::abs(slot.displayWidthDip - widthDip) > 8.0 ||
           std::abs(slot.displayHeightDip - heightDip) > 8.0 ||
           std::abs(slot.dpiScale - dpiScale) > 0.01;
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
        ResetItem(item);
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

void AppSwitcherXamlView::SetTargetMonitor(HMONITOR monitor)
{
    targetMonitor_ = monitor;
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
        ApplyItemTheme(item);
    }
}

void AppSwitcherXamlView::ApplyItemTheme(ItemView& item)
{
    if (item.mainCard)
    {
        item.mainCard.Background(Brush(palette_.cardBackground));
    }

    ApplyItemInteractionState(item);

    if (item.thumbnailHost)
    {
        item.thumbnailHost.Background(Brush(palette_.contentBackground));
    }

    if (item.title)
    {
        item.title.Foreground(Brush(palette_.primaryText));
    }

    if (item.defaultIcon)
    {
        item.defaultIcon.Foreground(Brush(palette_.iconText));
    }

    if (item.closeButton)
    {
        item.closeButton.Foreground(Brush(palette_.buttonText));
        item.closeButton.Background(Brush(winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00}));

        auto resources = item.closeButton.Resources();
        resources.Insert(winrt::box_value(L"ButtonBackground"), Brush(winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00}));
        resources.Insert(winrt::box_value(L"ButtonForeground"), Brush(palette_.buttonText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"), Brush(palette_.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"), Brush(palette_.closeButtonHoverText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), Brush(palette_.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPressed"), Brush(palette_.closeButtonHoverText));
    }
}

void AppSwitcherXamlView::ApplyItemRowWeights(ItemView& item)
{
    if (!item.layoutGrid)
    {
        return;
    }

    const auto rows = item.layoutGrid.RowDefinitions();
    if (rows.Size() < 2)
    {
        return;
    }

    rows.GetAt(0).Height(winrt::Windows::UI::Xaml::GridLengthHelper::FromValueAndType(
        AppSwitcherLayoutEngine::TitleRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
    rows.GetAt(1).Height(winrt::Windows::UI::Xaml::GridLengthHelper::FromValueAndType(
        AppSwitcherLayoutEngine::ContentRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
}

void AppSwitcherXamlView::ApplyItemInteractionState(ItemView& item)
{
    if (item.titleBorder)
    {
        item.titleBorder.Background(Brush(item.hovered ? palette_.titleHoverBackground : palette_.titleBackground));
    }

    if (item.pressOverlay)
    {
        if (item.grabbed || item.pressed)
        {
            const auto overlayColor = item.grabbed
                                          ? winrt::Windows::UI::Color{0x33, 0x00, 0x00, 0x00}
                                          : winrt::Windows::UI::Color{0x22, 0x00, 0x00, 0x00};
            item.pressOverlay.Background(Brush(overlayColor));
            item.pressOverlay.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
        }
        else
        {
            item.pressOverlay.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
    }

    if (item.transform)
    {
        const double scale = item.grabbed ? 0.90 : item.pressed ? 0.985 : 1.0;
        item.transform.ScaleX(scale);
        item.transform.ScaleY(scale);
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
        ApplyItemInteractionState(item);
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
        ApplyItemInteractionState(item);
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

POINT AppSwitcherXamlView::DipPointToScreenPixel(PointDip point) const
{
    POINT clientPoint{
        static_cast<LONG>(std::lround(static_cast<double>(point.x) * currentDpiScale_)),
        static_cast<LONG>(std::lround(static_cast<double>(point.y) * currentDpiScale_))};
    ClientToScreen(hwnd_, &clientPoint);
    return clientPoint;
}

void AppSwitcherXamlView::ClearItemThumbnail(ItemView& item)
{
    if (item.thumbnailSlot)
    {
        touchrev::thumbnail::PrivateThumbnailManager::ClearSlot(item.thumbnailHost, *item.thumbnailSlot);
        item.thumbnailSlot.reset();
    }
    else if (item.thumbnailHost)
    {
        winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(item.thumbnailHost, nullptr);
    }

    item.thumbnailError = S_OK;
    item.thumbnailFailed = false;
}

void AppSwitcherXamlView::ResetItem(ItemView& item)
{
    ClearItemThumbnail(item);
    item.hwnd = nullptr;
    item.layoutPosition = {};
    item.layoutSize = {};
    item.visible = false;
    item.hovered = false;
    item.pressed = false;
    item.grabbed = false;
    ApplyItemInteractionState(item);
}

void AppSwitcherXamlView::RenderSample(UINT widthPx, UINT heightPx, double scale)
{
    clientSizeDip_ = {
        static_cast<float>(static_cast<double>(widthPx) / std::max(0.01, scale)),
        static_cast<float>(static_cast<double>(heightPx) / std::max(0.01, scale))};

    auto windows = EnumerateSwitcherWindows(hwnd_, targetMonitor_);
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

AppSwitcherXamlView::ItemView AppSwitcherXamlView::CreateItem()
{
    ItemView item;
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
        ApplyItemRowWeights(item);
        ApplyItemTheme(item);

        const size_t itemIndex = items_.size();
        item.root.PointerEntered([this, itemIndex](auto const&, auto const&) {
            if (itemIndex >= items_.size() || xamlPointerPressed_ || xamlPointerDragging_)
            {
                return;
            }

            items_[itemIndex].hovered = true;
            ApplyItemInteractionState(items_[itemIndex]);
        });

        item.root.PointerExited([this, itemIndex](auto const&, auto const&) {
            if (itemIndex >= items_.size() || xamlPointerPressed_ || xamlPointerDragging_)
            {
                return;
            }

            items_[itemIndex].hovered = false;
            ApplyItemInteractionState(items_[itemIndex]);
        });

        if (item.closeButton)
        {
            item.closeButton.PointerEntered([this, itemIndex](auto const&, auto const&) {
                if (itemIndex < items_.size() && items_[itemIndex].closeButton)
                {
                    items_[itemIndex].closeButton.Background(Brush(palette_.closeButtonHoverBackground));
                    items_[itemIndex].closeButton.Foreground(Brush(palette_.closeButtonHoverText));
                }
            });
            item.closeButton.PointerExited([this, itemIndex](auto const&, auto const&) {
                if (itemIndex < items_.size() && items_[itemIndex].closeButton)
                {
                    items_[itemIndex].closeButton.Background(Brush(winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00}));
                    items_[itemIndex].closeButton.Foreground(Brush(palette_.buttonText));
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
            pressedItemIndex_ = itemIndex;
            activeDragItemIndex_ = itemIndex;
            xamlPointerPressed_ = true;
            xamlPointerDragging_ = false;
            pressPointDip_ = logicalPoint;
            xamlDragOffsetX_ = logicalPoint.x - items_[itemIndex].layoutPosition.x;
            xamlDragOffsetY_ = logicalPoint.y - items_[itemIndex].layoutPosition.y;
            items_[itemIndex].pressed = true;
            items_[itemIndex].hovered = false;
            ApplyItemInteractionState(items_[itemIndex]);
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
        ItemView item = CreateItem();
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

    if (focusBorder_)
    {
        if (xamlPointerDragging_)
        {
            focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
            return;
        }

        auto firstVisible = std::find_if(items_.begin(), items_.end(), [](const ItemView& item) { return item.visible; });
        if (firstVisible != items_.end())
        {
            constexpr double inflationDip = 10.0;
            winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(
                focusBorder_,
                firstVisible->layoutPosition.x - inflationDip);
            winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(
                focusBorder_,
                firstVisible->layoutPosition.y - inflationDip);
            focusBorder_.Width(firstVisible->layoutSize.width + inflationDip * 2.0);
            focusBorder_.Height(firstVisible->layoutSize.height + inflationDip * 2.0);
            focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
        }
        else
        {
            focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
    }
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

        item.root.Visibility(visible
                                 ? winrt::Windows::UI::Xaml::Visibility::Visible
                                 : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        if (!visible)
        {
            ResetItem(item);
            continue;
        }

        const RECT& rect = layout.items[i].rectPx;
        const double x = static_cast<double>(rect.left) / safeScale;
        const double y = static_cast<double>(rect.top) / safeScale;
        const double w = static_cast<double>(RectWidth(rect)) / safeScale;
        const double h = static_cast<double>(RectHeight(rect)) / safeScale;

        const HWND newHwnd = windows[i].hwnd;
        if (item.hwnd != nullptr && item.hwnd != newHwnd)
        {
            ClearItemThumbnail(item);
        }
        item.hwnd = newHwnd;
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

        if (item.title)
        {
            if (!windows[i].title.empty())
            {
                item.title.Text(winrt::hstring{windows[i].title});
            }
            else
            {
                std::wstringstream title;
                title << L"Window " << (i + 1);
                item.title.Text(winrt::hstring{title.str()});
            }
        }

        if (item.closeButton)
        {
            item.closeButton.Width(std::max(28.0, h * 0.18));
        }

        if (item.thumbnailHost)
        {
            item.root.UpdateLayout();
            item.thumbnailHost.UpdateLayout();

            const double thumbnailWidth = std::max(1.0, w);
            const double thumbnailHeight = std::max(
                1.0,
                h * AppSwitcherLayoutEngine::ContentRowWeight / AppSwitcherLayoutEngine::TotalRowWeight);
            ApplyContentClip(item.thumbnailHost, thumbnailWidth, thumbnailHeight);

            const bool needsThumbnail = !item.thumbnailSlot || ShouldRecreateThumbnail(
                                                                  *item.thumbnailSlot,
                                                                  newHwnd,
                                                                  thumbnailWidth,
                                                                  thumbnailHeight,
                                                                  currentDpiScale_);
            if (needsThumbnail && !item.thumbnailFailed)
            {
                if (item.thumbnailSlot)
                {
                    ClearItemThumbnail(item);
                }

                auto slot = thumbnailManager_.CreateForWindow(
                    newHwnd,
                    item.thumbnailHost,
                    thumbnailWidth,
                    thumbnailHeight,
                    currentDpiScale_);
                if (slot)
                {
                    item.thumbnailSlot = std::make_unique<touchrev::thumbnail::PrivateThumbnailSlot>(std::move(slot));
                    item.thumbnailError = S_OK;
                    item.thumbnailFailed = false;
                }
                else
                {
                    item.thumbnailError = slot.lastError;
                    item.thumbnailFailed = true;
                    item.thumbnailSlot.reset();
                }
            }
            else if (item.thumbnailSlot)
            {
                touchrev::thumbnail::PrivateThumbnailManager::ResizeSlot(
                    *item.thumbnailSlot,
                    thumbnailWidth,
                    thumbnailHeight);
            }
        }
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
