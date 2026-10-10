#include "AppIcon.h"

#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace touchrev::ui
{
namespace
{
// 每像素 4x4 超采样，够把 16px 托盘图标的圆弧边缘做平。
constexpr int kSubSamples = 4;

struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

Rgb AccentColor()
{
    try
    {
        const auto color = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
            winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        return {color.R / 255.0, color.G / 255.0, color.B / 255.0};
    }
    catch (...)
    {
        return {0x00 / 255.0, 0x78 / 255.0, 0xD4 / 255.0};
    }
}

bool InCircle(double x, double y, double cx, double cy, double radius)
{
    const double dx = x - cx;
    const double dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

// 单位坐标系（0..1）内的图形定义：底盘圆 + 三个触控点 + 下方手势弧。
bool InPlate(double x, double y)
{
    return InCircle(x, y, 0.5, 0.5, 0.47);
}

bool InGlyph(double x, double y)
{
    constexpr double kDotRadius = 0.075;
    if (InCircle(x, y, 0.305, 0.395, kDotRadius) ||
        InCircle(x, y, 0.5, 0.335, kDotRadius) ||
        InCircle(x, y, 0.695, 0.395, kDotRadius))
    {
        return true;
    }

    // 弧线：以 (0.5, 0.40) 为心的圆环，只保留下半段。
    const double dx = x - 0.5;
    const double dy = y - 0.40;
    const double distance = std::sqrt(dx * dx + dy * dy);
    return distance >= 0.245 && distance <= 0.315 && y >= 0.565;
}
}

HICON CreateAppIcon(int sizePx)
{
    const int size = std::clamp(sizePx, 8, 512);
    const Rgb accent = AccentColor();

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;  // 自上而下
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP colorBitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!colorBitmap || !pixels)
    {
        if (colorBitmap)
        {
            DeleteObject(colorBitmap);
        }
        return nullptr;
    }

    auto* rgba = static_cast<BYTE*>(pixels);
    const double step = 1.0 / (size * kSubSamples);
    const double sampleWeight = 1.0 / (kSubSamples * kSubSamples);

    for (int py = 0; py < size; ++py)
    {
        for (int px = 0; px < size; ++px)
        {
            double plateCoverage = 0.0;
            double glyphCoverage = 0.0;

            for (int sy = 0; sy < kSubSamples; ++sy)
            {
                for (int sx = 0; sx < kSubSamples; ++sx)
                {
                    const double x = (px * kSubSamples + sx + 0.5) * step;
                    const double y = (py * kSubSamples + sy + 0.5) * step;
                    if (InPlate(x, y))
                    {
                        plateCoverage += sampleWeight;
                        if (InGlyph(x, y))
                        {
                            glyphCoverage += sampleWeight;
                        }
                    }
                }
            }

            // 先在底盘内混出前景，再整体乘底盘覆盖率得到预乘 alpha 结果。
            const double r = accent.r * (1.0 - glyphCoverage) + glyphCoverage;
            const double g = accent.g * (1.0 - glyphCoverage) + glyphCoverage;
            const double b = accent.b * (1.0 - glyphCoverage) + glyphCoverage;

            BYTE* pixel = rgba + (static_cast<size_t>(py) * size + px) * 4;
            pixel[0] = static_cast<BYTE>(std::lround(b * plateCoverage * 255.0));
            pixel[1] = static_cast<BYTE>(std::lround(g * plateCoverage * 255.0));
            pixel[2] = static_cast<BYTE>(std::lround(r * plateCoverage * 255.0));
            pixel[3] = static_cast<BYTE>(std::lround(plateCoverage * 255.0));
        }
    }

    // 32bpp 图标不使用 AND 掩码，但 ICONINFO 仍要求提供一张同尺寸单色位图。
    std::vector<BYTE> maskBits((static_cast<size_t>(size) + 31) / 32 * 4 * size, 0);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!maskBitmap)
    {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;

    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}
}
