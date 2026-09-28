#include "game/view/Camera2D.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

void projectionAndInverse() {
    game::view::Camera2D camera({100, 100}, 16, {100, 50, 800, 600});
    const auto pixel = camera.worldToScreen({105, 102});
    check(close(pixel.x, 580) && close(pixel.y, 382), "world-to-screen projection");
    const auto world = camera.screenToWorld(pixel);
    check(close(world.x, 105) && close(world.y, 102), "screen-to-world inverse");
    const auto fractional = camera.screenToWorld(camera.worldToScreen({105.125, 102.375}));
    check(close(fractional.x, 105.125) && close(fractional.y, 102.375),
        "fractional world position lost before rasterization");
    check(camera.containsScreenPoint(pixel), "projected point outside viewport");
    check(!camera.containsScreenPoint({900, 350}), "right viewport edge was included");

    camera.setPixelsPerWorldUnit(32);
    const auto zoomed = camera.worldToScreen({105, 102});
    check(close(zoomed.x, 660) && close(zoomed.y, 414), "zoom projection");
    camera.setViewport({100, 50, 1600, 900});
    const auto resized = camera.worldToScreen({105, 102});
    check(close(resized.x, 1060) && close(resized.y, 564), "resize projection");
    const auto afterResize = camera.screenToWorld(resized);
    check(close(afterResize.x, 105) && close(afterResize.y, 102),
        "inverse projection after zoom and resize");
}

void cameraBoundsAndValidation() {
    game::view::Camera2D camera({0, 0}, 16, {0, 0, 800, 600});
    camera.clampCenterToMap(1000, 1000);
    check(close(camera.center().x, 25) && close(camera.center().y, 18.75),
        "camera exposed empty space beyond map edge");
    camera.clampCenterToMap(20, 10);
    check(close(camera.center().x, 10) && close(camera.center().y, 5),
        "small map was not centered in viewport");

    bool rejected = false;
    try { camera.setPixelsPerWorldUnit(0); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && camera.pixelsPerWorldUnit() == 16, "invalid zoom changed camera");
    rejected = false;
    try { camera.screenToWorld({std::numeric_limits<double>::quiet_NaN(), 0}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "nonfinite pixel accepted");
}
} // namespace

void runCameraTests() {
    projectionAndInverse();
    cameraBoundsAndValidation();
}
