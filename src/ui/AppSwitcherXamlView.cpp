#include "AppSwitcherXamlView.h"

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
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <fstream>
#include <sstream>

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
    containerTransform_ = nullptr;
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
    const float minX = std::min(0.0f, clientSizeDip_.width - contentBoundsDip_.width);
    const float minY = std::min(0.0f, clientSizeDip_.height - contentBoundsDip_.height);
    dragPosition_.x = std::clamp(dragPosition_.x, minX, 0.0f);
    dragPosition_.y = std::clamp(dragPosition_.y, minY, 0.0f);

    if (containerTransform_)
    {
        containerTransform_.X(dragPosition_.x);
        containerTransform_.Y(dragPosition_.y);
    }
}

void AppSwitcherXamlView::AttachPointerHandlers()
{
    if (!root_)
    {
        return;
    }

    root_.PointerPressed([this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (!root_)
        {
            return;
        }

        const auto point = args.GetCurrentPoint(root_).Position();
        xamlPointerDragging_ = true;
        xamlDragOffsetX_ = point.X - dragPosition_.x;
        xamlDragOffsetY_ = point.Y - dragPosition_.y;
        root_.CapturePointer(args.Pointer());
        args.Handled(true);
    });

    root_.PointerMoved([this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (!xamlPointerDragging_ || !root_)
        {
            return;
        }

        const auto point = args.GetCurrentPoint(root_).Position();
        SetDragPosition({
            static_cast<float>(point.X - xamlDragOffsetX_),
            static_cast<float>(point.Y - xamlDragOffsetY_)});
        args.Handled(true);
    });

    auto endDrag = [this](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (!xamlPointerDragging_ || !root_)
        {
            return;
        }

        xamlPointerDragging_ = false;
        root_.ReleasePointerCapture(args.Pointer());
        args.Handled(true);
    };

    root_.PointerReleased(endDrag);
    root_.PointerCanceled(endDrag);
}

void AppSwitcherXamlView::RenderSample(UINT widthPx, UINT heightPx, double scale)
{
    clientSizeDip_ = {
        static_cast<float>(static_cast<double>(widthPx) / std::max(0.01, scale)),
        static_cast<float>(static_cast<double>(heightPx) / std::max(0.01, scale))};

    std::vector<AppSwitcherWindowItem> sample{
        {nullptr, 1920.0, 1080.0},
        {nullptr, 1366.0, 768.0},
        {nullptr, 900.0, 1400.0},
        {nullptr, 1280.0, 1024.0},
        {nullptr, 1600.0, 900.0},
        {nullptr, 720.0, 1280.0},
    };
    ApplyLayout(sample, widthPx, heightPx, scale);
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
        containerTransform_ = winrt::Windows::UI::Xaml::Media::TranslateTransform();
        appSwitcherContainer_.RenderTransform(containerTransform_);
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
        items_.push_back(item);
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
    SetDragPosition(dragPosition_);

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
            continue;
        }

        const RECT& rect = layout.items[i].rectPx;
        const double x = static_cast<double>(rect.left) / safeScale;
        const double y = static_cast<double>(rect.top) / safeScale;
        const double w = static_cast<double>(RectWidth(rect)) / safeScale;
        const double h = static_cast<double>(RectHeight(rect)) / safeScale;

        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(item.root, x);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(item.root, y);
        item.root.Width(w);
        item.root.Height(h);

        if (item.title)
        {
            std::wstringstream title;
            title << L"Sample Window " << (i + 1) << L"  " << static_cast<int>(windows[i].widthPx)
                  << L"x" << static_cast<int>(windows[i].heightPx);
            item.title.Text(winrt::hstring{title.str()});
        }

        if (item.closeButton)
        {
            item.closeButton.Width(std::max(28.0, h * 0.18));
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
