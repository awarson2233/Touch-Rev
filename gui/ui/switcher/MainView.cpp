#include "MainView.h"

#include "WindowController.h"
#include "common/Win32Error.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"
#include "common/GeometryUtils.h"

#include <win32acrylic/composition_acrylic.h>

#include <winrt/base.h>
#include <winrt/Windows.Devices.Input.h>
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
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <utility>

namespace touchrev::appswitcher
{
namespace
{
constexpr wchar_t kRootXamlPath[] = L"xaml/AppSwitcherRoot.xaml";
constexpr wchar_t kThemeXamlPath[] = L"xaml/ThemeResources.xaml";

using touchrev::common::xaml::Brush;

bool AppendAppSwitcherThemeResources(winrt::Windows::UI::Xaml::ResourceDictionary const& resources)
{
    try
    {
        const std::wstring themeXaml = touchrev::common::LoadTextFileUtf8(touchrev::common::ModuleRelativePath(kThemeXamlPath));
        if (themeXaml.empty())
        {
            DebugLog(L"ThemeResources.xaml is empty or missing.");
            return false;
        }

        auto themeRes = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{themeXaml})
                            .as<winrt::Windows::UI::Xaml::ResourceDictionary>();
        resources.MergedDictionaries().Append(themeRes);
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Load ThemeResources.xaml in MainView", error.code());
        return false;
    }
}

win32acrylic::Color ToAcrylicColor(winrt::Windows::UI::Color color, double opacity)
{
    const float alpha = static_cast<float>(std::clamp(opacity, 0.0, 1.0)) * (static_cast<float>(color.A) / 255.0f);
    return {
        static_cast<float>(color.R) / 255.0f,
        static_cast<float>(color.G) / 255.0f,
        static_cast<float>(color.B) / 255.0f,
        alpha};
}

using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::UI::Composition::CompositionPropertySet;

constexpr std::chrono::milliseconds kFocusFadeInDuration{120};
constexpr std::chrono::milliseconds kFocusFadeOutDuration{100};

void SetVector2Now(CompositionPropertySet const& props, wchar_t const* name, float2 value)
{
    // 属性上若有弹簧在跑，直接 Insert 会被动画覆盖，必须先停掉。
    props.StopAnimation(name);
    props.InsertVector2(name, value);
}

void SpringVector2(CompositionPropertySet const& props, wchar_t const* name, float2 value)
{
    // NaturalMotion 动画在运行中被重新启动时会继承当前速度，连续切换能自然衔接。
    auto spring = props.Compositor().CreateSpringVector2Animation();
    spring.DampingRatio(0.85f);
    spring.Period(std::chrono::milliseconds(28));
    spring.FinalValue(value);
    props.StartAnimation(name, spring);
}

void FadeOpacity(winrt::Windows::UI::Composition::Visual const& visual, float to, std::chrono::milliseconds duration)
{
    auto fade = visual.Compositor().CreateScalarKeyFrameAnimation();
    fade.InsertKeyFrame(1.0f, to);
    fade.Duration(duration);
    visual.StartAnimation(L"Opacity", fade);
}

winrt::Windows::UI::Color SystemAccentColor(winrt::Windows::UI::ViewManagement::UIColorType type)
{
    try
    {
        return winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(type);
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Query system accent color", error.code());
        return {0xFF, 0x00, 0x78, 0xD4};
    }
}

bool SystemAnimationsEnabled()
{
    try
    {
        return winrt::Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
    }
    catch (const winrt::hresult_error&)
    {
        return true;
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
        item->Reset();
    }
    cards_.clear();
    if (appSwitcherContainer_)
    {
        winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(appSwitcherContainer_, nullptr);
    }
    if (focusBorder_)
    {
        winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(focusBorder_, nullptr);
    }
    focusVisual_ = nullptr;
    focusProps_ = nullptr;
    focusOuterGeometry_ = nullptr;
    focusInnerGeometry_ = nullptr;
    focusClipGeometry_ = nullptr;
    focusAccentBrush_ = nullptr;
    focusInnerBrush_ = nullptr;
    focusRingShown_ = false;
    appSwitcherContainer_ = nullptr;
    appSwitcherStroke_ = nullptr;
    layoutCanvas_ = nullptr;
    focusBorder_ = nullptr;
    emptyGrid_ = nullptr;
    emptyIcon_ = nullptr;
    emptyText_ = nullptr;
    root_ = nullptr;
    host_ = nullptr;
    hwnd_ = nullptr;
    containerAcrylicVisual_ = nullptr;
    containerAcrylicClip_ = nullptr;
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

void MainView::SetItemDragOutCallback(std::function<bool(const ItemDragOut&)> callback)
{
    itemDragOutCallback_ = std::move(callback);
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
    UpdateSelectionVisual(true);
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

void MainView::ApplyTheme(AppThemeMode mode, bool active)
{
    const bool themeChanged = (themeMode_ != mode);
    const bool activeChanged = (isWindowActive_ != active);
    const bool brushExists = (containerAcrylicVisual_ && containerAcrylicVisual_.Brush());

    themeMode_ = mode;
    isWindowActive_ = active;
    // 系统强调色与动画开关可能在两次呼出之间被修改，不受下面的提前返回影响。
    UpdateFocusRingStyle();

    if (!themeChanged && !activeChanged && brushExists)
    {
        return;
    }

    if (appSwitcherContainer_)
    {
        UpdateContainerAcrylicBrush();
    }

    for (auto& item : cards_)
    {
        item->ApplyTheme(active);
    }
}

void MainView::UpdateContainerAcrylicBrush()
{
    if (!containerAcrylicVisual_)
    {
        return;
    }

    auto appResources = winrt::Windows::UI::Xaml::Application::Current().Resources();
    auto GetThemeDict = [&](bool dark) -> winrt::Windows::UI::Xaml::ResourceDictionary
    {
        auto themeKey = winrt::box_value(dark ? L"Dark" : L"Light");
        try
        {
            auto themeDictionaries = appResources.ThemeDictionaries();
            if (themeDictionaries.HasKey(themeKey))
            {
                return themeDictionaries.Lookup(themeKey).as<winrt::Windows::UI::Xaml::ResourceDictionary>();
            }
        }
        catch (...) {}

        try
        {
            auto merged = appResources.MergedDictionaries();
            for (uint32_t i = 0; i < merged.Size(); ++i)
            {
                auto subDict = merged.GetAt(i);
                auto themeDictionaries = subDict.ThemeDictionaries();
                if (themeDictionaries.HasKey(themeKey))
                {
                    return themeDictionaries.Lookup(themeKey).as<winrt::Windows::UI::Xaml::ResourceDictionary>();
                }
            }
        }
        catch (...) {}
        return nullptr;
    };

    try
    {
        double blurAmount = 30.0;
        double noiseOpacity = 0.02;
        double tintOpacity = 0.0;
        double luminosityOpacity = 0.64;
        winrt::Windows::UI::Color tintColor = {0xFF, 0x54, 0x54, 0x54};
        winrt::Windows::UI::Color luminosityColor = {0xFF, 0x54, 0x54, 0x54};

        bool isDark = (themeMode_ == AppThemeMode::Dark);
        winrt::Windows::UI::Xaml::ResourceDictionary themeDict = GetThemeDict(isDark);

        auto GetThemeResource = [&](const wchar_t* key) -> winrt::Windows::Foundation::IInspectable
        {
            if (themeDict)
            {
                auto boxedKey = winrt::box_value(key);
                if (themeDict.HasKey(boxedKey))
                {
                    return themeDict.Lookup(boxedKey);
                }
            }
            return nullptr;
        };

        if (appSwitcherContainer_)
        {
            if (auto val = GetThemeResource(L"AcrylicBlurAmount"))
            {
                blurAmount = winrt::unbox_value<double>(val);
            }
            if (auto val = GetThemeResource(L"AcrylicNoiseOpacity"))
            {
                noiseOpacity = winrt::unbox_value<double>(val);
            }
            if (auto val = GetThemeResource(L"AcrylicTintOpacity"))
            {
                tintOpacity = winrt::unbox_value<double>(val);
            }

            if (auto val = GetThemeResource(L"AcrylicTintColor"))
            {
                tintColor = winrt::unbox_value<winrt::Windows::UI::Color>(val);
                luminosityColor = tintColor;
            }

            if (auto val = GetThemeResource(L"AcrylicLuminosityColor"))
            {
                luminosityColor = winrt::unbox_value<winrt::Windows::UI::Color>(val);
            }

            if (auto val = GetThemeResource(L"AcrylicLuminosityOpacity"))
            {
                luminosityOpacity = winrt::unbox_value<double>(val);
            }
        }

        // 与 XAML AcrylicBrush 同一套 luminosity 配方（模糊 → luminosity 色 → tint 色 → 噪点），
        // 设置了 TintLuminosityOpacity 时 luminosity 色就是 tint 色按该不透明度取用。
        auto compositor = containerAcrylicVisual_.Compositor();
        win32acrylic::AcrylicBrushOptions options{};
        options.material = win32acrylic::AcrylicMaterial::Acrylic;
        options.backdropSource = win32acrylic::BackdropSource::HostBackdrop;
        options.tintColor = ToAcrylicColor(tintColor, tintOpacity);
        options.luminosityColor = ToAcrylicColor(luminosityColor, luminosityOpacity);
        options.blurAmount = static_cast<float>(blurAmount);
        options.noiseOpacity = static_cast<float>(noiseOpacity);
        containerAcrylicVisual_.Brush(win32acrylic::CreateAcrylicBrush(compositor, options));

        if (appSwitcherContainer_)
        {
            winrt::Windows::UI::Xaml::Media::SolidColorBrush transparentBrush{winrt::Windows::UI::Color{0, 0, 0, 0}};
            appSwitcherContainer_.Background(transparentBrush);
        }
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Create AppSwitcherContainer acrylic brush", error.code());

        if (appSwitcherContainer_)
        {
            winrt::Windows::UI::Color fallbackColor = {0xFF, 0x20, 0x20, 0x20};
            
            bool isDark = (themeMode_ == AppThemeMode::Dark);
            winrt::Windows::UI::Xaml::ResourceDictionary themeDict = GetThemeDict(isDark);

            if (themeDict)
            {
                auto boxedKey = winrt::box_value(L"AcrylicFallbackColor");
                if (themeDict.HasKey(boxedKey))
                {
                    fallbackColor = winrt::unbox_value<winrt::Windows::UI::Color>(themeDict.Lookup(boxedKey));
                }
            }

            winrt::Windows::UI::Xaml::Media::SolidColorBrush fallbackBrush{fallbackColor};
            appSwitcherContainer_.Background(fallbackBrush);
        }
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
    if (appSwitcherStroke_)
    {
        appSwitcherStroke_.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
    }

    for (auto& item : cards_)
    {
        item->pressed = false;
        item->grabbed = false;
        if (item->root)
        {
            item->root.Visibility(item->visible
                                     ? winrt::Windows::UI::Xaml::Visibility::Visible
                                     : winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        item->ApplyInteractionState();
    }
    UpdateSelectionVisual();
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
    if (appSwitcherStroke_)
    {
        appSwitcherStroke_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
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
        item->ApplyInteractionState();
    }
    UpdateSelectionVisual();
}

bool MainView::TryHandOffDrag(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args, PointDip pointerDip)
{
    if (!itemDragOutCallback_ || activeDragItemIndex_ >= cards_.size())
    {
        return false;
    }

    auto& item = *cards_[activeDragItemIndex_];
    if (!item.hwnd)
    {
        return false;
    }

    // 抓起阈值之前卡片还停在布局位置上；代理按最初按下的点对齐到指针下，保持手指在卡片上的相对位置。
    ItemDragOut dragOut;
    dragOut.hwnd = item.hwnd;
    const POINT topLeft = DipPointToScreenPixel(item.layoutPosition);
    const POINT bottomRight = DipPointToScreenPixel({
        item.layoutPosition.x + item.layoutSize.width,
        item.layoutPosition.y + item.layoutSize.height});
    dragOut.cardRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    dragOut.grabPoint = DipPointToScreenPixel(pressPointDip_);
    dragOut.pointer = DipPointToScreenPixel(pointerDip);
    dragOut.fromMouse =
        args.Pointer().PointerDeviceType() == winrt::Windows::Devices::Input::PointerDeviceType::Mouse;

    // 回调在指针仍被本线程持有时执行，触摸/笔才能被转成鼠标拖拽交给移动循环。
    if (!itemDragOutCallback_(dragOut))
    {
        return false;
    }

    if (item.root)
    {
        item.root.ReleasePointerCapture(args.Pointer());
    }
    ResetInteractionState();
    return true;
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

void MainView::EnsureLayoutCommitted()
{
    if (!root_)
    {
        return;
    }

    try
    {
        root_.UpdateLayout();
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"MainView::EnsureLayoutCommitted", error.code());
    }
}

bool MainView::LoadRoot()
{
    try
    {
        auto app = winrt::Windows::UI::Xaml::Application::Current();
        if (!app)
        {
            DebugLog(L"Xaml Application is not available.");
            return false;
        }

        AppendAppSwitcherThemeResources(app.Resources());

        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(touchrev::common::ModuleRelativePath(kRootXamlPath));
        if (xaml.empty())
        {
            DebugLog(L"AppSwitcherRoot.xaml is empty or missing.");
            return false;
        }

        auto object = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{xaml});
        root_ = object.as<winrt::Windows::UI::Xaml::Controls::Grid>();

        appSwitcherContainer_ = root_.FindName(L"AppSwitcherContainer").as<winrt::Windows::UI::Xaml::Controls::Border>();
        if (appSwitcherContainer_)
        {
            auto hostVisual = winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(appSwitcherContainer_);
            auto compositor = hostVisual.Compositor();
            containerAcrylicVisual_ = compositor.CreateSpriteVisual();
            try
            {
                containerAcrylicClip_ = compositor.CreateRoundedRectangleGeometry();
                const auto xamlRadius = appSwitcherContainer_.CornerRadius();
                const float radius = static_cast<float>(xamlRadius.TopLeft);
                containerAcrylicClip_.CornerRadius({radius, radius});
                auto clip = compositor.CreateGeometricClip(containerAcrylicClip_);
                containerAcrylicVisual_.Clip(clip);
            }
            catch (const winrt::hresult_error& error)
            {
                DebugLogHResult(L"Create AppSwitcherContainer acrylic clip", error.code());
            }
            winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(appSwitcherContainer_, containerAcrylicVisual_);
        }
        appSwitcherStroke_ = root_.FindName(L"AppSwitcherStroke").as<winrt::Windows::UI::Xaml::Controls::Border>();
        layoutCanvas_ = root_.FindName(L"LayoutCanvas").as<winrt::Windows::UI::Xaml::Controls::Canvas>();
        focusBorder_ = root_.FindName(L"FocusBorder").as<winrt::Windows::UI::Xaml::Controls::Border>();
        CreateFocusRing();
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
        ApplyTheme(themeMode_, isWindowActive_);
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

    auto item = CardView::Create(itemIndex, callbacks);
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
    cards_[index]->ApplyInteractionState();
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
        if (TryHandOffDrag(args, logicalPoint))
        {
            args.Handled(true);
            return;
        }
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

    if (focusBorder_)
    {
        focusBorder_.Width(visibleBoundsDip_.width);
        focusBorder_.Height(visibleBoundsDip_.height);
    }

    if (focusVisual_)
    {
        focusVisual_.Size({visibleBoundsDip_.width, visibleBoundsDip_.height});
    }

    const float widthDip = contentBoundsDip_.width;
    const float heightDip = contentBoundsDip_.height;

    for (const auto& container : {appSwitcherContainer_, appSwitcherStroke_})
    {
        if (!container)
        {
            continue;
        }
        winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(container, contentOriginDip_.x);
        winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(container, contentOriginDip_.y);
        container.Width(widthDip);
        container.Height(heightDip);
    }

    if (containerAcrylicVisual_)
    {
        containerAcrylicVisual_.Size({widthDip, heightDip});
    }

    if (containerAcrylicClip_)
    {
        containerAcrylicClip_.Size({widthDip, heightDip});
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

double MainView::FocusResource(const wchar_t* key, double fallback) const
{
    if (!focusBorder_)
    {
        return fallback;
    }

    auto res = focusBorder_.Resources();
    const auto boxedKey = winrt::box_value(key);
    return res.HasKey(boxedKey) ? winrt::unbox_value<double>(res.Lookup(boxedKey)) : fallback;
}

void MainView::CreateFocusRing()
{
    if (!focusBorder_)
    {
        return;
    }

    try
    {
        auto compositor = winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(focusBorder_).Compositor();
        focusProps_ = compositor.CreatePropertySet();
        focusProps_.InsertVector2(L"Offset", {});
        focusProps_.InsertVector2(L"Size", {});
        focusProps_.InsertVector2(L"Growth", {});

        // props.Offset/Size 是强调色外沿（静止时）的矩形。强调色内沿必须固定贴住白色内圈、只让外沿生长，
        // 而单条描边做不到各边不等宽，所以把强调色画成一条足够宽（覆盖最大生长）的描边，内沿对齐，
        // 再用随 Growth 伸出的圆角矩形裁剪出外沿。
        const float stroke = static_cast<float>(FocusResource(L"StrokeDip", 4.0));
        const float innerStroke = static_cast<float>(FocusResource(L"InnerStrokeDip", 2.0));
        const float accentWidth = stroke + static_cast<float>(FocusResource(L"GrowMaxDip", 5.0));
        // 宽描边中线 = 强调色内沿外扩半个宽描边。
        const float accentInset = stroke - accentWidth * 0.5f;
        // 白色内圈中线 = 强调色内沿内缩半个内圈宽度。
        const float innerInset = stroke + innerStroke * 0.5f;

        auto bind = [&](auto const& target, wchar_t const* property, wchar_t const* expression) {
            auto animation = compositor.CreateExpressionAnimation(expression);
            animation.SetReferenceParameter(L"props", focusProps_);
            animation.SetScalarParameter(L"accentInset", accentInset);
            animation.SetScalarParameter(L"innerInset", innerInset);
            target.StartAnimation(property, animation);
        };

        focusClipGeometry_ = compositor.CreateRoundedRectangleGeometry();
        // 外沿只在 Growth 的符号方向上伸出：负值向左/上扩（移原点），正值向右/下扩（只加尺寸）。
        bind(focusClipGeometry_, L"Offset", L"props.Offset + Vector2(Min(props.Growth.X, 0), Min(props.Growth.Y, 0))");
        bind(focusClipGeometry_, L"Size", L"props.Size + Vector2(Abs(props.Growth.X), Abs(props.Growth.Y))");

        focusOuterGeometry_ = compositor.CreateRoundedRectangleGeometry();
        bind(focusOuterGeometry_, L"Offset", L"props.Offset + Vector2(accentInset, accentInset)");
        bind(focusOuterGeometry_, L"Size", L"props.Size - Vector2(accentInset * 2, accentInset * 2)");

        focusInnerGeometry_ = compositor.CreateRoundedRectangleGeometry();
        bind(focusInnerGeometry_, L"Offset", L"props.Offset + Vector2(innerInset, innerInset)");
        bind(focusInnerGeometry_, L"Size", L"props.Size - Vector2(innerInset * 2, innerInset * 2)");

        focusAccentBrush_ = compositor.CreateColorBrush();
        focusInnerBrush_ = compositor.CreateColorBrush();

        auto outerShape = compositor.CreateSpriteShape(focusOuterGeometry_);
        outerShape.StrokeBrush(focusAccentBrush_);
        outerShape.StrokeThickness(accentWidth);
        auto innerShape = compositor.CreateSpriteShape(focusInnerGeometry_);
        innerShape.StrokeBrush(focusInnerBrush_);
        innerShape.StrokeThickness(innerStroke);

        focusVisual_ = compositor.CreateShapeVisual();
        focusVisual_.Shapes().Append(innerShape);
        focusVisual_.Shapes().Append(outerShape);
        focusVisual_.Clip(compositor.CreateGeometricClip(focusClipGeometry_));
        focusVisual_.Opacity(0.0f);
        focusRingShown_ = false;
        winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(focusBorder_, focusVisual_);

        UpdateFocusRingStyle();
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"Create focus ring visual", error.code());
        focusVisual_ = nullptr;
        focusProps_ = nullptr;
    }
}

void MainView::UpdateFocusRingStyle()
{
    animationsEnabled_ = SystemAnimationsEnabled();
    if (!focusAccentBrush_ || !focusInnerBrush_)
    {
        return;
    }

    // 取自系统 Alt+Tab 的焦点视觉：外层 FocusVisualPrimaryBrushThemed（暗色 AccentLight2、亮色 AccentDark1），
    // 内层为系统焦点副描边 FocusStrokeColorInner（暗色 #B3000000、亮色 #B3FFFFFF）。
    using winrt::Windows::UI::ViewManagement::UIColorType;
    const bool dark = themeMode_ == AppThemeMode::Dark;
    focusAccentBrush_.Color(SystemAccentColor(dark ? UIColorType::AccentLight2 : UIColorType::AccentDark1));
    focusInnerBrush_.Color(dark ? winrt::Windows::UI::Color{0xB3, 0x00, 0x00, 0x00}
                                : winrt::Windows::UI::Color{0xB3, 0xFF, 0xFF, 0xFF});
}

void MainView::HideFocusRing(bool animate)
{
    if (!focusVisual_ || !focusRingShown_)
    {
        return;
    }

    focusRingShown_ = false;
    if (animate && animationsEnabled_)
    {
        FadeOpacity(focusVisual_, 0.0f, kFocusFadeOutDuration);
        return;
    }
    focusVisual_.StopAnimation(L"Opacity");
    focusVisual_.Opacity(0.0f);
}

void MainView::SetFocusGrowth(float x, float y, bool animate)
{
    if (!focusProps_)
    {
        return;
    }

    if (animate && animationsEnabled_)
    {
        SpringVector2(focusProps_, L"Growth", {x, y});
        return;
    }
    SetVector2Now(focusProps_, L"Growth", {x, y});
}

float MainView::GrowthForAxis(double accumulated, double threshold, int stepX, int stepY) const
{
    if (accumulated == 0.0 || threshold <= 0.0)
    {
        return 0.0f;
    }

    const double progress = std::clamp(accumulated / threshold, -1.0, 1.0);
    double growth = progress * FocusResource(L"GrowMaxDip", 5.0);
    const int sign = accumulated > 0.0 ? 1 : -1;
    const bool hasNext = LayoutEngine::CalculateNextSelection(
                             GetItemGeometries(), selectedItemIndex_, stepX * sign, stepY * sign) != static_cast<size_t>(-1);
    if (!hasNext)
    {
        growth *= FocusResource(L"GrowEdgeResistance", 0.3);
    }
    return static_cast<float>(growth);
}

void MainView::UpdateSelectionVisual(bool animate)
{
    if (!focusVisual_ || !focusProps_)
    {
        return;
    }

    if (xamlPointerDragging_ || selectedItemIndex_ == static_cast<size_t>(-1) ||
        selectedItemIndex_ >= cards_.size() || !cards_[selectedItemIndex_]->visible)
    {
        HideFocusRing(true);
        return;
    }

    const auto& selectedItem = *cards_[selectedItemIndex_];
    const float gap = static_cast<float>(FocusResource(L"InflationDip", 6.0));
    const float stroke = static_cast<float>(FocusResource(L"StrokeDip", 4.0));
    const float innerStroke = static_cast<float>(FocusResource(L"InnerStrokeDip", 2.0));
    const float accentWidth = stroke + static_cast<float>(FocusResource(L"GrowMaxDip", 5.0));
    // 从卡片边缘向外：留空 → 内圈 → 强调色；outerInflation 为强调色外沿（静止时）到卡片边缘的距离。
    // 各圆角 = 卡片圆角 + 该线到卡片边缘的距离，保证与卡片同心；描边几何取中线。
    const float outerInflation = gap + innerStroke + stroke;
    const float accentCenterInflation = gap + innerStroke + accentWidth * 0.5f;
    const float innerInflation = gap + innerStroke * 0.5f;
    const float cardRadius = selectedItem.mainCard
                                 ? static_cast<float>(selectedItem.mainCard.CornerRadius().TopLeft)
                                 : 10.0f;
    focusClipGeometry_.CornerRadius({cardRadius + outerInflation, cardRadius + outerInflation});
    focusOuterGeometry_.CornerRadius({cardRadius + accentCenterInflation, cardRadius + accentCenterInflation});
    focusInnerGeometry_.CornerRadius({cardRadius + innerInflation, cardRadius + innerInflation});

    const float2 offset{
        selectedItem.layoutPosition.x - outerInflation,
        selectedItem.layoutPosition.y - outerInflation};
    const float2 size{
        selectedItem.layoutSize.width + outerInflation * 2.0f,
        selectedItem.layoutSize.height + outerInflation * 2.0f};
    const bool targetUnchanged =
        std::abs(offset.x - focusTargetOffset_.x) < 0.5f && std::abs(offset.y - focusTargetOffset_.y) < 0.5f &&
        std::abs(size.x - focusTargetSize_.x) < 0.5f && std::abs(size.y - focusTargetSize_.y) < 0.5f;
    focusTargetOffset_ = offset;
    focusTargetSize_ = size;

    if (focusRingShown_ && targetUnchanged)
    {
        return;
    }

    if (!focusRingShown_)
    {
        // 首次出现（呼出、拖拽结束）没有上一个位置可滑，改为从外扩处淡入并收缩到位。
        focusRingShown_ = true;
        SetFocusGrowth(0.0f, 0.0f, false);
        focusVisual_.StopAnimation(L"Opacity");
        if (animationsEnabled_)
        {
            const float entrance = static_cast<float>(FocusResource(L"EntranceInflationDip", 8.0));
            SetVector2Now(focusProps_, L"Offset", offset - float2{entrance, entrance});
            SetVector2Now(focusProps_, L"Size", size + float2{entrance * 2.0f, entrance * 2.0f});
            focusVisual_.Opacity(0.0f);
            FadeOpacity(focusVisual_, 1.0f, kFocusFadeInDuration);
            SpringVector2(focusProps_, L"Offset", offset);
            SpringVector2(focusProps_, L"Size", size);
        }
        else
        {
            SetVector2Now(focusProps_, L"Offset", offset);
            SetVector2Now(focusProps_, L"Size", size);
            focusVisual_.Opacity(1.0f);
        }
        return;
    }

    if (animate && animationsEnabled_)
    {
        SpringVector2(focusProps_, L"Offset", offset);
        SpringVector2(focusProps_, L"Size", size);
        return;
    }
    SetVector2Now(focusProps_, L"Offset", offset);
    SetVector2Now(focusProps_, L"Size", size);
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

    double paddingDip = 48.0;
    double itemGapDip = 32.0;
    double minAspect = 0.4;
    double maxAspect = 2.5;
    double titleHeightDip = 40.0;
    double cardBorderDip = 1.0;

    if (root_)
    {
        auto res = root_.Resources();
        if (res.HasKey(winrt::box_value(L"PaddingDip")))
        {
            paddingDip = winrt::unbox_value<double>(res.Lookup(winrt::box_value(L"PaddingDip")));
        }
        if (res.HasKey(winrt::box_value(L"ItemGapDip")))
        {
            itemGapDip = winrt::unbox_value<double>(res.Lookup(winrt::box_value(L"ItemGapDip")));
        }
        if (res.HasKey(winrt::box_value(L"MinAspect")))
        {
            minAspect = winrt::unbox_value<double>(res.Lookup(winrt::box_value(L"MinAspect")));
        }
        if (res.HasKey(winrt::box_value(L"MaxAspect")))
        {
            maxAspect = winrt::unbox_value<double>(res.Lookup(winrt::box_value(L"MaxAspect")));
        }
    }

    EnsureItemCount(windows.size());
    if (!cards_.empty() && cards_[0])
    {
        titleHeightDip = cards_[0]->TitleHeightDip();
        cardBorderDip = cards_[0]->BorderDip();
    }

    const double safeScale = std::max(0.01, scale);
    currentDpiScale_ = safeScale;
    // 扣除任务栏占用的边，只在工作区内排版；每边内缩最多四分之一客户区，防御异常的工作区数据。
    const LONG clientWidthPx = static_cast<LONG>(std::max(1u, widthPx));
    const LONG clientHeightPx = static_cast<LONG>(std::max(1u, heightPx));
    const LONG insetLeft = std::clamp<LONG>(workAreaInsetsPx_.left, 0, clientWidthPx / 4);
    const LONG insetRight = std::clamp<LONG>(workAreaInsetsPx_.right, 0, clientWidthPx / 4);
    const LONG insetTop = std::clamp<LONG>(workAreaInsetsPx_.top, 0, clientHeightPx / 4);
    const LONG insetBottom = std::clamp<LONG>(workAreaInsetsPx_.bottom, 0, clientHeightPx / 4);
    RECT workArea{insetLeft, insetTop, clientWidthPx - insetRight, clientHeightPx - insetBottom};
    const LayoutResult layout = LayoutEngine::Calculate(
        windows,
        workArea,
        safeScale,
        paddingDip,
        itemGapDip,
        titleHeightDip,
        cardBorderDip,
        minAspect,
        maxAspect);
    contentBoundsDip_ = {
        static_cast<float>(static_cast<double>(layout.totalSizePx.cx) / safeScale),
        static_cast<float>(static_cast<double>(layout.totalSizePx.cy) / safeScale)};
    const double workLeftDip = static_cast<double>(workArea.left) / safeScale;
    const double workTopDip = static_cast<double>(workArea.top) / safeScale;
    const double workWidthDip = static_cast<double>(workArea.right - workArea.left) / safeScale;
    const double workHeightDip = static_cast<double>(workArea.bottom - workArea.top) / safeScale;
    contentOriginDip_ = {
        static_cast<float>(workLeftDip + std::max(0.0, (workWidthDip - contentBoundsDip_.width) * 0.5)),
        static_cast<float>(workTopDip + std::max(0.0, (workHeightDip - contentBoundsDip_.height) * 0.5))};
    if (emptyGrid_)
    {
        emptyGrid_.Margin({static_cast<double>(insetLeft) / safeScale,
                           static_cast<double>(insetTop) / safeScale,
                           static_cast<double>(insetRight) / safeScale,
                           static_cast<double>(insetBottom) / safeScale});
    }
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
            item.Reset();
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
                // 整框已移到下一张，生长清零，下一次切换要重新走满一个阈值。
                selectedChanged = true;
                gestureAccX_ = 0.0;
            }
            else
            {
                gestureAccX_ = std::fmod(gestureAccX_, currentSnapThresholdX);
            }
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
                gestureAccY_ = 0.0;
            }
            else
            {
                gestureAccY_ = std::fmod(gestureAccY_, currentSnapThresholdY);
            }
        }
    }

    // 外层描边朝手指方向生长，到阈值时恰好长满；刚切换时生长量骤降，用弹簧收回避免跳变。
    SetFocusGrowth(
        GrowthForAxis(gestureAccX_, currentSnapThresholdX, 1, 0),
        GrowthForAxis(gestureAccY_, currentSnapThresholdY, 0, 1),
        selectedChanged);

    return selectedChanged;
}

void MainView::ClearGestureAccumulator()
{
    gestureAccX_ = 0.0;
    gestureAccY_ = 0.0;
    SetFocusGrowth(0.0f, 0.0f, true);
}

void MainView::ResetSelection()
{
    selectedItemIndex_ = static_cast<size_t>(-1);
    // 下次呼出从隐藏态重新出现，播放入场动画而不是从上次的位置滑过去。
    HideFocusRing(false);
}
}



