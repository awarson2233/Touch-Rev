#pragma once

#include "RectangleModel.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

struct SizePx
{
    UINT width = 0;
    UINT height = 0;
};

class CoordinateSpace
{
public:
    void Update(float dpi, UINT clientWidth, UINT clientHeight)
    {
        SetDpi(dpi);
        SetClientSize(clientWidth, clientHeight);
    }

    void SetDpi(float dpi)
    {
        dpi_ = std::max(1.0f, dpi);
    }

    void SetClientSize(UINT clientWidth, UINT clientHeight)
    {
        clientSizePx_ = {std::max(1u, clientWidth), std::max(1u, clientHeight)};
        clientSizeDip_ = {
            PixelsToDips(static_cast<float>(clientSizePx_.width)),
            PixelsToDips(static_cast<float>(clientSizePx_.height))};
    }

    float Dpi() const { return dpi_; }
    float Scale() const { return dpi_ / 96.0f; }
    SizePx ClientSizePixels() const { return clientSizePx_; }
    SizeDip ClientSizeDips() const { return clientSizeDip_; }

    float PixelsToDips(float pixels) const
    {
        return pixels * 96.0f / dpi_;
    }

    float DipsToPixels(float dips) const
    {
        return dips * dpi_ / 96.0f;
    }

    PointDip ClientPixelsToDips(POINT clientPoint) const
    {
        return {
            PixelsToDips(static_cast<float>(clientPoint.x)),
            PixelsToDips(static_cast<float>(clientPoint.y))};
    }

    POINT DipsToClientPixels(PointDip point) const
    {
        return {
            static_cast<LONG>(std::lround(DipsToPixels(point.x))),
            static_cast<LONG>(std::lround(DipsToPixels(point.y)))};
    }

    PointDip ScreenPixelsToDips(HWND hwnd, POINT screenPoint) const
    {
        ScreenToClient(hwnd, &screenPoint);
        return ClientPixelsToDips(screenPoint);
    }

    float SnapDipToPixel(float dip) const
    {
        return PixelsToDips(std::round(DipsToPixels(dip)));
    }

    PointDip SnapDipToPixel(PointDip point) const
    {
        return {SnapDipToPixel(point.x), SnapDipToPixel(point.y)};
    }

private:
    float dpi_ = 96.0f;
    SizePx clientSizePx_ = {1, 1};
    SizeDip clientSizeDip_ = {1.0f, 1.0f};
};
