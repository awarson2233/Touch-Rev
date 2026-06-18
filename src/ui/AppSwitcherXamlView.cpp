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

std::vector<AppSwitcherWindowItem> EnumerateSwitcherWindows(HWND excludeHwnd)
{
    struct EnumState
    {
        HWND exclude = nullptr;
        std::vector<AppSwitcherWindowItem> windows;
    } state{excludeHwnd};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto& state = *reinterpret_cast<EnumState*>(param);
            if (hwnd == state.exclude || !IsAltTabLikeWindow(hwnd))
            {
                return TRUE;
            }

            RECT rect{};
            GetWindowRect(hwnd, &rect);
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

double ResolveActualSize(double actualSize, double fallbackSize)
{
    return actualSize > 1.0 ? actualSize : std::max(1.0, fallbackSize);
}

void ApplyContentClip(winrt::Windows::UI::Xaml::FrameworkElement const& element, double width, double height)
{
    auto clip = winrt::Windows::UI::Xaml::Media::RectangleGeometry();
    clip.Rect({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
    element.Clip(clip);
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
    items_.clear();
    appSwitcherContainer_ = nullptr;
    layoutCanvas_ = nullptr;
    focusBorder_ = nullptr;
    emptyGrid_ = nullptr;
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
    return initialized_ && point.x >= 0.0f && point.y >= 0.0f &&
           point.x <= clientSizeDip_.width && point.y <= clientSizeDip_.height;
}

void AppSwitcherXamlView::SetDragPosition(PointDip position)
{
    dragPosition_ = position;
}

void AppSwitcherXamlView::AttachPointerHandlers()
{
    // Item-level pointer handlers are attached when SwitcherItem instances are created.
}

void AppSwitcherXamlView::RenderSample(UINT widthPx, UINT heightPx, double scale)
{
    clientSizeDip_ = {
        static_cast<float>(static_cast<double>(widthPx) / std::max(0.01, scale)),
        static_cast<float>(static_cast<double>(heightPx) / std::max(0.01, scale))};

    auto windows = EnumerateSwitcherWindows(hwnd_);
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
        appSwitcherContainer_ = root_.FindName(L"AppSwitcherContainer").as<winrt::Windows::UI::Xaml::FrameworkElement>();
        layoutCanvas_ = root_.FindName(L"LayoutCanvas").as<winrt::Windows::UI::Xaml::Controls::Canvas>();
        focusBorder_ = root_.FindName(L"FocusBorder").as<winrt::Windows::UI::Xaml::FrameworkElement>();
        emptyGrid_ = root_.FindName(L"EmptyGrid").as<winrt::Windows::UI::Xaml::FrameworkElement>();

        host_->SetRoot(root_);
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
        item.title = item.root.FindName(L"TitleText").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        item.closeButton = item.root.FindName(L"CloseButton").as<winrt::Windows::UI::Xaml::Controls::Button>();
        item.thumbnailHost = item.root.FindName(L"ContentFrame").as<winrt::Windows::UI::Xaml::FrameworkElement>();

        const size_t itemIndex = items_.size();
        item.root.PointerPressed([this, itemIndex](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!root_ || itemIndex >= items_.size())
            {
                return;
            }

            const auto point = args.GetCurrentPoint(root_).Position();
            const double left = winrt::Windows::UI::Xaml::Controls::Canvas::GetLeft(items_[itemIndex].root);
            const double top = winrt::Windows::UI::Xaml::Controls::Canvas::GetTop(items_[itemIndex].root);
            activeDragItemIndex_ = itemIndex;
            xamlPointerDragging_ = true;
            xamlDragOffsetX_ = point.X - left;
            xamlDragOffsetY_ = point.Y - top;
            sender.template as<winrt::Windows::UI::Xaml::UIElement>().CapturePointer(args.Pointer());
            args.Handled(true);
        });

        item.root.PointerMoved([this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!xamlPointerDragging_ || !root_ || activeDragItemIndex_ >= items_.size())
            {
                return;
            }

            const auto point = args.GetCurrentPoint(root_).Position();
            auto& activeItem = items_[activeDragItemIndex_];
            winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(activeItem.root, point.X - xamlDragOffsetX_);
            winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(activeItem.root, point.Y - xamlDragOffsetY_);
            args.Handled(true);
        });

        auto endDrag = [this](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!xamlPointerDragging_)
            {
                return;
            }

            xamlPointerDragging_ = false;
            activeDragItemIndex_ = static_cast<size_t>(-1);
            sender.template as<winrt::Windows::UI::Xaml::UIElement>().ReleasePointerCapture(args.Pointer());
            args.Handled(true);
        };

        item.root.PointerReleased(endDrag);
        item.root.PointerCanceled(endDrag);

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
    if (appSwitcherContainer_)
    {
        appSwitcherContainer_.Width(contentBoundsDip_.width);
        appSwitcherContainer_.Height(contentBoundsDip_.height);
    }
    layoutCanvas_.Width(contentBoundsDip_.width);
    layoutCanvas_.Height(contentBoundsDip_.height);
    if (activeDragItemIndex_ >= windows.size())
    {
        xamlPointerDragging_ = false;
        activeDragItemIndex_ = static_cast<size_t>(-1);
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
            if (item.thumbnailHost)
            {
                winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(item.thumbnailHost, nullptr);
            }
            item.hwnd = nullptr;
            item.thumbnailSlot.reset();
            continue;
        }

        const RECT& rect = layout.items[i].rectPx;
        const double x = static_cast<double>(rect.left) / safeScale;
        const double y = static_cast<double>(rect.top) / safeScale;
        const double w = static_cast<double>(RectWidth(rect)) / safeScale;
        const double h = static_cast<double>(RectHeight(rect)) / safeScale;

        item.hwnd = windows[i].hwnd;
        item.layoutPosition = {static_cast<float>(x), static_cast<float>(y)};
        if (!xamlPointerDragging_ || activeDragItemIndex_ != i)
        {
            winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(item.root, x);
            winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(item.root, y);
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

            const double fallbackThumbnailWidth = std::max(1.0, w - 2.0);
            const double fallbackThumbnailHeight = std::max(1.0, h * 0.82 - 2.0);
            const double thumbnailWidth = ResolveActualSize(item.thumbnailHost.ActualWidth(), fallbackThumbnailWidth);
            const double thumbnailHeight = ResolveActualSize(item.thumbnailHost.ActualHeight(), fallbackThumbnailHeight);
            ApplyContentClip(item.thumbnailHost, thumbnailWidth, thumbnailHeight);

            if (!item.thumbnailSlot || ShouldRecreateThumbnail(
                                           *item.thumbnailSlot,
                                           windows[i].hwnd,
                                           thumbnailWidth,
                                           thumbnailHeight,
                                           currentDpiScale_))
            {
                item.thumbnailSlot = std::make_unique<touchrev::thumbnail::PrivateThumbnailSlot>(
                    thumbnailManager_.CreateForWindow(
                        windows[i].hwnd,
                        item.thumbnailHost,
                        thumbnailWidth,
                        thumbnailHeight,
                        currentDpiScale_));
            }
            else
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

    if (focusBorder_ && !layout.items.empty())
    {
        const RECT& rect = layout.items.front().rectPx;
        constexpr double inflationDip = 10.0;
        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(
            focusBorder_,
            static_cast<double>(rect.left) / safeScale - inflationDip);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(
            focusBorder_,
            static_cast<double>(rect.top) / safeScale - inflationDip);
        focusBorder_.Width(static_cast<double>(RectWidth(rect)) / safeScale + inflationDip * 2.0);
        focusBorder_.Height(static_cast<double>(RectHeight(rect)) / safeScale + inflationDip * 2.0);
        focusBorder_.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
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
