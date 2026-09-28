#include "game/view/Camera2D.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace game::view {
namespace {
void requireFinite(double value, const char* message) {
    if (!std::isfinite(value)) throw std::invalid_argument(message);
}

void requireViewport(ViewportPixels viewport) {
    requireFinite(viewport.left, "viewport origin must be finite");
    requireFinite(viewport.top, "viewport origin must be finite");
    if (!std::isfinite(viewport.width) || viewport.width <= 0 ||
        !std::isfinite(viewport.height) || viewport.height <= 0 ||
        !std::isfinite(viewport.left + viewport.width) ||
        !std::isfinite(viewport.top + viewport.height))
        throw std::invalid_argument("viewport dimensions must be finite and positive");
}

void requirePosition(Position position) {
    requireFinite(position.x, "world position must be finite");
    requireFinite(position.y, "world position must be finite");
}
} // namespace

Camera2D::Camera2D(Position center, double pixelsPerWorldUnit, ViewportPixels viewport)
    : center_(center), pixelsPerWorldUnit_(pixelsPerWorldUnit), viewport_(viewport) {
    requirePosition(center);
    if (!std::isfinite(pixelsPerWorldUnit) || pixelsPerWorldUnit <= 0)
        throw std::invalid_argument("pixels per world unit must be finite and positive");
    requireViewport(viewport);
}

void Camera2D::setCenter(Position center) {
    requirePosition(center);
    center_ = center;
}

void Camera2D::setPixelsPerWorldUnit(double scale) {
    if (!std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("pixels per world unit must be finite and positive");
    pixelsPerWorldUnit_ = scale;
}

void Camera2D::setViewport(ViewportPixels viewport) {
    requireViewport(viewport);
    viewport_ = viewport;
}

void Camera2D::clampCenterToMap(double mapWidth, double mapHeight) {
    if (!std::isfinite(mapWidth) || mapWidth <= 0 ||
        !std::isfinite(mapHeight) || mapHeight <= 0)
        throw std::invalid_argument("map dimensions must be finite and positive");

    const double halfWidth = viewport_.width / pixelsPerWorldUnit_ / 2;
    const double halfHeight = viewport_.height / pixelsPerWorldUnit_ / 2;
    center_.x = halfWidth >= mapWidth / 2 ? mapWidth / 2
        : std::clamp(center_.x, halfWidth, mapWidth - halfWidth);
    center_.y = halfHeight >= mapHeight / 2 ? mapHeight / 2
        : std::clamp(center_.y, halfHeight, mapHeight - halfHeight);
}

ScreenPoint Camera2D::worldToScreen(Position world) const {
    requirePosition(world);
    const ScreenPoint screen{
        viewport_.left + viewport_.width / 2 + (world.x - center_.x) * pixelsPerWorldUnit_,
        viewport_.top + viewport_.height / 2 + (world.y - center_.y) * pixelsPerWorldUnit_
    };
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
        throw std::overflow_error("world position cannot be projected to finite pixels");
    return screen;
}

Position Camera2D::screenToWorld(ScreenPoint screen) const {
    requireFinite(screen.x, "screen position must be finite");
    requireFinite(screen.y, "screen position must be finite");
    const Position world{
        center_.x + (screen.x - viewport_.left - viewport_.width / 2) / pixelsPerWorldUnit_,
        center_.y + (screen.y - viewport_.top - viewport_.height / 2) / pixelsPerWorldUnit_
    };
    if (!std::isfinite(world.x) || !std::isfinite(world.y))
        throw std::overflow_error("screen position cannot be projected to finite world units");
    return world;
}

bool Camera2D::containsScreenPoint(ScreenPoint screen) const noexcept {
    return std::isfinite(screen.x) && std::isfinite(screen.y) &&
        screen.x >= viewport_.left && screen.x < viewport_.left + viewport_.width &&
        screen.y >= viewport_.top && screen.y < viewport_.top + viewport_.height;
}

} // namespace game::view
