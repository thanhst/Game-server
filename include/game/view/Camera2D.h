#pragma once

#include "game/Position.h"

namespace game::view {

struct ScreenPoint {
    double x = 0;
    double y = 0;
};

struct ViewportPixels {
    double left = 0;
    double top = 0;
    double width = 0;
    double height = 0;
};

// World and screen both use a right/down coordinate system. Pixel coordinates
// must be measured in the same render-target space as the viewport.
class Camera2D final {
public:
    Camera2D(Position center, double pixelsPerWorldUnit, ViewportPixels viewport);

    Position center() const noexcept { return center_; }
    double pixelsPerWorldUnit() const noexcept { return pixelsPerWorldUnit_; }
    ViewportPixels viewport() const noexcept { return viewport_; }

    void setCenter(Position center);
    void setPixelsPerWorldUnit(double scale);
    void setViewport(ViewportPixels viewport);
    void clampCenterToMap(double mapWidth, double mapHeight);

    ScreenPoint worldToScreen(Position world) const;
    Position screenToWorld(ScreenPoint screen) const;
    bool containsScreenPoint(ScreenPoint screen) const noexcept;

private:
    Position center_;
    double pixelsPerWorldUnit_;
    ViewportPixels viewport_;
};

} // namespace game::view
