#pragma once

#include "RectangleModel.h"

#include <windows.h>
#include <objbase.h>
#include <d2d1_1.h>
#include <wrl/client.h>

class CompositionHost;
class GraphicsDevice;

class Renderer
{
public:
    enum class BackgroundMode
    {
        TransparentForMica,
        SolidWhiteFallback,
    };

    HRESULT Initialize(GraphicsDevice& graphicsDevice, CompositionHost& compositionHost, float dpi);
    HRESULT Resize(float dpi);
    HRESULT Render(const RectangleModel& rectangle, const PointDip* visualPosition = nullptr);
    void ReleaseTarget();
    void Reset();
    void SetDpi(float dpi) { dpi_ = dpi; }
    void SetBackgroundMode(BackgroundMode mode) { backgroundMode_ = mode; }
    void SetFallbackBackgroundColor(D2D1_COLOR_F color) { fallbackBackgroundColor_ = color; }

private:
    HRESULT CreateTargetBitmap();
    HRESULT EnsureBrushes();
    float SnapDipToPixel(float dip) const;

    GraphicsDevice* graphicsDevice_ = nullptr;
    CompositionHost* compositionHost_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> strokeBrush_;
    float dpi_ = 96.0f;
    BackgroundMode backgroundMode_ = BackgroundMode::TransparentForMica;
    D2D1_COLOR_F fallbackBackgroundColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
};
