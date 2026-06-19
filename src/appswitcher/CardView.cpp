#include "CardView.h"

#include "LayoutEngine.h"
#include "common/FileUtils.h"
#include "common/PathUtils.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>



#include <cmath>
#include <sstream>
#include <utility>

namespace
{
winrt::Windows::UI::Xaml::Media::SolidColorBrush Brush(winrt::Windows::UI::Color color)
{
    return winrt::Windows::UI::Xaml::Media::SolidColorBrush(color);
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
}

namespace touchrev::appswitcher
{
std::optional<CardView> CardView::Create(
    const AppSwitcherPalette& palette,
    size_t index,
    CardCallbacks callbacks)
{
    CardView item;
    try
    {
        constexpr wchar_t kItemXamlPath[] = L"xaml/SwitcherItem.xaml";
        const std::wstring xaml = touchrev::common::LoadTextFileUtf8(touchrev::common::ModuleRelativePath(kItemXamlPath));
        auto object = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring{xaml});
        auto rootElement = object.as<winrt::Windows::UI::Xaml::FrameworkElement>();

        if (item.Initialize(rootElement, palette, index, callbacks))
        {
            return item;
        }
    }
    catch (const winrt::hresult_error&)
    {
    }
    return std::nullopt;
}

bool CardView::Initialize(
    winrt::Windows::UI::Xaml::FrameworkElement rootElement,
    const AppSwitcherPalette& palette,
    size_t index,
    CardCallbacks callbacks)
{
    root = rootElement;
    if (!root)
    {
        return false;
    }

    try
    {
        transform = root.FindName(L"ItemTransform").as<winrt::Windows::UI::Xaml::Media::CompositeTransform>();
        layoutGrid = root.FindName(L"ItemLayoutGrid").as<winrt::Windows::UI::Xaml::Controls::Grid>();
        mainCard = root.FindName(L"MainCard").as<winrt::Windows::UI::Xaml::Controls::Border>();
        titleBorder = root.FindName(L"TitleBorder").as<winrt::Windows::UI::Xaml::Controls::Border>();
        title = root.FindName(L"TitleText").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        defaultIcon = root.FindName(L"DefaultIcon").as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
        closeButton = root.FindName(L"CloseButton").as<winrt::Windows::UI::Xaml::Controls::Button>();
        thumbnailHost = root.FindName(L"ContentFrame").as<winrt::Windows::UI::Xaml::Controls::Border>();
        pressOverlay = root.FindName(L"PressOverlay").as<winrt::Windows::UI::Xaml::Controls::Border>();

        ApplyRowWeights();
        ApplyTheme(palette);
        palette_ = palette;

        root.PointerEntered([this](auto const&, auto const&) {
            hovered = true;
            ApplyInteractionState(palette_);
        });

        root.PointerExited([this](auto const&, auto const&) {
            hovered = false;
            ApplyInteractionState(palette_);
        });


        if (closeButton)
        {
            closeButton.Click([callbacks, index](auto const&, auto const&) {
                if (callbacks.onCloseClicked)
                {
                    callbacks.onCloseClicked(index);
                }
            });
            closeButton.PointerEntered([this](auto const&, auto const&) {
                ApplyCloseButtonHoverState(palette_, true);
            });
            closeButton.PointerExited([this](auto const&, auto const&) {
                ApplyCloseButtonHoverState(palette_, false);
            });
        }

        root.PointerPressed([callbacks, index](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (callbacks.onPointerPressed)
            {
                callbacks.onPointerPressed(index, args);
            }
        });

        root.PointerMoved([callbacks, index](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (callbacks.onPointerMoved)
            {
                callbacks.onPointerMoved(index, args);
            }
        });

        root.PointerReleased([callbacks, index](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (callbacks.onPointerReleased)
            {
                callbacks.onPointerReleased(index, args);
            }
        });

        root.PointerCanceled([callbacks, index](auto const&, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (callbacks.onPointerCanceled)
            {
                callbacks.onPointerCanceled(index, args);
            }
        });

        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}

void CardView::ApplyRowWeights()
{
    if (!layoutGrid)
    {
        return;
    }

    const auto rows = layoutGrid.RowDefinitions();
    if (rows.Size() < 2)
    {
        return;
    }

    rows.GetAt(0).Height(winrt::Windows::UI::Xaml::GridLengthHelper::FromValueAndType(
        LayoutEngine::TitleRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
    rows.GetAt(1).Height(winrt::Windows::UI::Xaml::GridLengthHelper::FromValueAndType(
        LayoutEngine::ContentRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
}

void CardView::ApplyTheme(const AppSwitcherPalette& palette)
{
    palette_ = palette;
    if (mainCard)
    {
        mainCard.Background(Brush(palette.cardBackground));
    }

    ApplyInteractionState(palette);

    if (thumbnailHost)
    {
        thumbnailHost.Background(Brush(palette.contentBackground));
    }

    if (title)
    {
        title.Foreground(Brush(palette.primaryText));
    }

    if (defaultIcon)
    {
        defaultIcon.Foreground(Brush(palette.iconText));
    }

    if (closeButton)
    {
        closeButton.Foreground(Brush(palette.buttonText));
        closeButton.Background(Brush(winrt::Windows::UI::Colors::Transparent()));

        auto resources = closeButton.Resources();
        resources.Insert(winrt::box_value(L"ButtonBackground"), Brush(winrt::Windows::UI::Colors::Transparent()));
        resources.Insert(winrt::box_value(L"ButtonForeground"), Brush(palette.buttonText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"), Brush(palette.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"), Brush(palette.closeButtonHoverText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), Brush(palette.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPressed"), Brush(palette.closeButtonHoverText));
    }
}

void CardView::ApplyInteractionState(const AppSwitcherPalette& palette)
{
    if (titleBorder)
    {
        titleBorder.Background(Brush(hovered ? palette.titleHoverBackground : palette.titleBackground));
    }

    if (pressOverlay)
    {
        if (grabbed || pressed)
        {
            const auto overlayColor = grabbed
                                          ? palette.cardGrabbedOverlay
                                          : palette.cardPressedOverlay;
            pressOverlay.Background(Brush(overlayColor));
            pressOverlay.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
        }
        else
        {
            pressOverlay.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
    }

    if (transform)
    {
        const double scale = grabbed ? 0.90 : pressed ? 0.985 : 1.0;
        transform.ScaleX(scale);
        transform.ScaleY(scale);
    }
}

void CardView::ApplyCloseButtonHoverState(const AppSwitcherPalette& palette, bool isHovered)
{
    if (!closeButton)
    {
        return;
    }

    closeButton.Background(Brush(isHovered ? palette.closeButtonHoverBackground : winrt::Windows::UI::Colors::Transparent()));
    closeButton.Foreground(Brush(isHovered ? palette.closeButtonHoverText : palette.buttonText));
}

void CardView::ClearThumbnail()
{
    if (thumbnailSlot)
    {
        touchrev::thumbnail::PrivateThumbnailManager::ClearSlot(thumbnailHost, *thumbnailSlot);
        thumbnailSlot.reset();
    }
    else if (thumbnailHost)
    {
        winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(thumbnailHost, nullptr);
    }

    thumbnailError = S_OK;
    thumbnailFailed = false;
}

void CardView::Reset(const AppSwitcherPalette& palette)
{
    palette_ = palette;
    ClearThumbnail();
    hwnd = nullptr;
    layoutPosition = {};
    layoutSize = {};
    visible = false;
    hovered = false;
    pressed = false;
    grabbed = false;
    ApplyInteractionState(palette);
    ApplyCloseButtonHoverState(palette, false);
}

void CardView::SetRootVisibility(bool isVisible)
{
    if (!root)
    {
        return;
    }

    root.Visibility(isVisible
                        ? winrt::Windows::UI::Xaml::Visibility::Visible
                        : winrt::Windows::UI::Xaml::Visibility::Collapsed);
}

void CardView::AssignWindow(HWND newHwnd)
{
    if (hwnd != nullptr && hwnd != newHwnd)
    {
        ClearThumbnail();
    }
    hwnd = newHwnd;
}

void CardView::ApplyTitle(const std::wstring& titleText, size_t fallbackOrdinal)
{
    if (!title)
    {
        return;
    }

    if (!titleText.empty())
    {
        title.Text(winrt::hstring{titleText});
        return;
    }

    std::wstringstream fallback;
    fallback << L"Window " << fallbackOrdinal;
    title.Text(winrt::hstring{fallback.str()});
}

void CardView::ApplyCloseButtonWidth(double widthDip)
{
    if (closeButton)
    {
        closeButton.Width(widthDip);
    }
}

void CardView::EnsureThumbnail(
    touchrev::thumbnail::PrivateThumbnailManager& thumbnailManager,
    double widthDip,
    double heightDip,
    double dpiScale)
{
    if (!root || !thumbnailHost || !hwnd)
    {
        return;
    }

    root.UpdateLayout();
    thumbnailHost.UpdateLayout();
    ApplyContentClip(thumbnailHost, widthDip, heightDip);

    const bool needsThumbnail = !thumbnailSlot || ShouldRecreateThumbnail(
                                                   *thumbnailSlot,
                                                   hwnd,
                                                   widthDip,
                                                   heightDip,
                                                   dpiScale);
    if (needsThumbnail && !thumbnailFailed)
    {
        if (thumbnailSlot)
        {
            ClearThumbnail();
        }

        auto slot = thumbnailManager.CreateForWindow(
            hwnd,
            thumbnailHost,
            widthDip,
            heightDip,
            dpiScale);
        if (slot)
        {
            thumbnailSlot = std::make_unique<touchrev::thumbnail::PrivateThumbnailSlot>(std::move(slot));
            thumbnailError = S_OK;
            thumbnailFailed = false;
        }
        else
        {
            thumbnailError = slot.lastError;
            thumbnailFailed = true;
            thumbnailSlot.reset();
        }
    }
    else if (thumbnailSlot)
    {
        touchrev::thumbnail::PrivateThumbnailManager::ResizeSlot(
            *thumbnailSlot,
            widthDip,
            heightDip);
    }
}

void CardView::Destroy()
{
    ClearThumbnail();
    root = nullptr;
    transform = nullptr;
    layoutGrid = nullptr;
    mainCard = nullptr;
    titleBorder = nullptr;
    title = nullptr;
    defaultIcon = nullptr;
    closeButton = nullptr;
    thumbnailHost = nullptr;
    pressOverlay = nullptr;
}

void CardView::UpdateState(
    HWND newHwnd,
    const std::wstring& titleText,
    size_t index,
    double x,
    double y,
    double w,
    double h,
    bool isDragging,
    touchrev::thumbnail::PrivateThumbnailManager& thumbnailManager,
    double dpiScale)
{
    AssignWindow(newHwnd);
    visible = true;
    layoutSize = {static_cast<float>(w), static_cast<float>(h)};
    if (!isDragging)
    {
        layoutPosition = {static_cast<float>(x), static_cast<float>(y)};
    }
    if (root)
    {
        root.Width(w);
        root.Height(h);
    }
    ApplyTitle(titleText, index);
    ApplyCloseButtonWidth(std::max(28.0, h * 0.18));

    const double thumbnailWidth = std::max(1.0, w);
    const double thumbnailHeight = std::max(
        1.0,
        h * LayoutEngine::ContentRowWeight / LayoutEngine::TotalRowWeight);
    EnsureThumbnail(thumbnailManager, thumbnailWidth, thumbnailHeight, dpiScale);
}
}

