#include "AppSwitcherItemView.h"

#include "AppSwitcherLayoutEngine.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

#include <cmath>
#include <sstream>
#include <utility>

namespace
{
winrt::Windows::UI::Xaml::Media::SolidColorBrush Brush(winrt::Windows::UI::Color color)
{
    return winrt::Windows::UI::Xaml::Media::SolidColorBrush(color);
}

winrt::Windows::UI::Color TransparentColor()
{
    return winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00};
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

void AppSwitcherItemView::ApplyRowWeights()
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
        AppSwitcherLayoutEngine::TitleRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
    rows.GetAt(1).Height(winrt::Windows::UI::Xaml::GridLengthHelper::FromValueAndType(
        AppSwitcherLayoutEngine::ContentRowWeight,
        winrt::Windows::UI::Xaml::GridUnitType::Star));
}

void AppSwitcherItemView::ApplyTheme(const AppSwitcherPalette& palette)
{
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
        closeButton.Background(Brush(TransparentColor()));

        auto resources = closeButton.Resources();
        resources.Insert(winrt::box_value(L"ButtonBackground"), Brush(TransparentColor()));
        resources.Insert(winrt::box_value(L"ButtonForeground"), Brush(palette.buttonText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"), Brush(palette.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"), Brush(palette.closeButtonHoverText));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), Brush(palette.closeButtonHoverBackground));
        resources.Insert(winrt::box_value(L"ButtonForegroundPressed"), Brush(palette.closeButtonHoverText));
    }
}

void AppSwitcherItemView::ApplyInteractionState(const AppSwitcherPalette& palette)
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
                                          ? winrt::Windows::UI::Color{0x33, 0x00, 0x00, 0x00}
                                          : winrt::Windows::UI::Color{0x22, 0x00, 0x00, 0x00};
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

void AppSwitcherItemView::ApplyCloseButtonHoverState(const AppSwitcherPalette& palette, bool isHovered)
{
    if (!closeButton)
    {
        return;
    }

    closeButton.Background(Brush(isHovered ? palette.closeButtonHoverBackground : TransparentColor()));
    closeButton.Foreground(Brush(isHovered ? palette.closeButtonHoverText : palette.buttonText));
}

void AppSwitcherItemView::ClearThumbnail()
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

void AppSwitcherItemView::Reset(const AppSwitcherPalette& palette)
{
    ClearThumbnail();
    hwnd = nullptr;
    layoutPosition = {};
    layoutSize = {};
    visible = false;
    hovered = false;
    pressed = false;
    grabbed = false;
    ApplyInteractionState(palette);
}

void AppSwitcherItemView::SetRootVisibility(bool isVisible)
{
    if (!root)
    {
        return;
    }

    root.Visibility(isVisible
                        ? winrt::Windows::UI::Xaml::Visibility::Visible
                        : winrt::Windows::UI::Xaml::Visibility::Collapsed);
}

void AppSwitcherItemView::AssignWindow(HWND newHwnd)
{
    if (hwnd != nullptr && hwnd != newHwnd)
    {
        ClearThumbnail();
    }
    hwnd = newHwnd;
}

void AppSwitcherItemView::ApplyTitle(const std::wstring& titleText, size_t fallbackOrdinal)
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

void AppSwitcherItemView::ApplyCloseButtonWidth(double widthDip)
{
    if (closeButton)
    {
        closeButton.Width(widthDip);
    }
}

void AppSwitcherItemView::EnsureThumbnail(
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
