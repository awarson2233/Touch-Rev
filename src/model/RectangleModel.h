#pragma once

#include <algorithm>
#include <cmath>

struct PointDip
{
    float x = 0.0f;
    float y = 0.0f;
};

struct SizeDip
{
    float width = 0.0f;
    float height = 0.0f;
};

class RectangleModel
{
public:
    void Initialize(float clientWidth, float clientHeight)
    {
        clientWidth_ = std::max(0.0f, clientWidth);
        clientHeight_ = std::max(0.0f, clientHeight);
        width_ = 240.0f;
        height_ = 128.0f;
        x_ = (clientWidth_ - width_) * 0.5f;
        y_ = (clientHeight_ - height_) * 0.5f;
        ClampToClient();
    }

    void SetClientSize(float clientWidth, float clientHeight)
    {
        clientWidth_ = std::max(0.0f, clientWidth);
        clientHeight_ = std::max(0.0f, clientHeight);
        ClampToClient();
    }

    bool HitTest(PointDip point) const
    {
        return point.x >= x_ && point.x <= x_ + width_ && point.y >= y_ &&
               point.y <= y_ + height_;
    }

    void MoveTo(PointDip point)
    {
        const PointDip clamped = ClampPosition(point);
        x_ = clamped.x;
        y_ = clamped.y;
    }

    void ClampToClient()
    {
        MoveTo({x_, y_});
    }

    PointDip ClampPosition(PointDip point) const
    {
        const float maxX = std::max(0.0f, clientWidth_ - width_);
        const float maxY = std::max(0.0f, clientHeight_ - height_);
        return {std::clamp(point.x, 0.0f, maxX), std::clamp(point.y, 0.0f, maxY)};
    }

    PointDip Position() const
    {
        return {x_, y_};
    }

    SizeDip Size() const
    {
        return {width_, height_};
    }

    SizeDip ClientSize() const
    {
        return {clientWidth_, clientHeight_};
    }

    float Left() const { return x_; }
    float Top() const { return y_; }
    float Right() const { return x_ + width_; }
    float Bottom() const { return y_ + height_; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    float width_ = 240.0f;
    float height_ = 128.0f;
    float clientWidth_ = 0.0f;
    float clientHeight_ = 0.0f;
};
