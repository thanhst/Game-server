#include "game/world/CollisionGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace game {
namespace {
double cross(Position a, Position b, Position c) noexcept {
    return (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
}

double distanceToSegment(Position point, Position a, Position b) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length == 0) return std::hypot(point.x - a.x, point.y - a.y);
    if (!std::isfinite(length)) return 0; // Fail closed on unrepresentable geometry.

    const double unitX = dx / length;
    const double unitY = dy / length;
    const double projection = std::clamp(
        (point.x - a.x) * unitX + (point.y - a.y) * unitY, 0.0, length);
    return std::hypot(point.x - a.x - projection * unitX,
        point.y - a.y - projection * unitY);
}

bool onSegment(Position p, Position a, Position b) noexcept {
    return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
        p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

bool segmentsIntersect(Position a, Position b, Position c, Position d) noexcept {
    const double abC = cross(a, b, c);
    const double abD = cross(a, b, d);
    const double cdA = cross(c, d, a);
    const double cdB = cross(c, d, b);
    if (!std::isfinite(abC) || !std::isfinite(abD) ||
        !std::isfinite(cdA) || !std::isfinite(cdB)) return true;
    if (abC == 0 && onSegment(c, a, b)) return true;
    if (abD == 0 && onSegment(d, a, b)) return true;
    if (cdA == 0 && onSegment(a, c, d)) return true;
    if (cdB == 0 && onSegment(b, c, d)) return true;
    if (abC == 0 || abD == 0 || cdA == 0 || cdB == 0) return false;
    return ((abC > 0) != (abD > 0)) && ((cdA > 0) != (cdB > 0));
}

bool pointInside(const ConvexPolygon& wall, Position point) noexcept {
    int side = 0;
    for (std::size_t i = 0; i < wall.vertices.size(); ++i) {
        const double turn = cross(wall.vertices[i],
            wall.vertices[(i + 1) % wall.vertices.size()], point);
        if (!std::isfinite(turn)) return true; // Fail closed.
        if (turn == 0) continue;
        const int currentSide = turn > 0 ? 1 : -1;
        if (side != 0 && side != currentSide) return false;
        side = currentSide;
    }
    return true;
}
} // namespace

ConvexPolygon ConvexPolygon::rectangle(double x, double y, double width, double height) {
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0 ||
        !std::isfinite(x + width) || !std::isfinite(y + height))
        throw std::invalid_argument("rectangle dimensions must be finite and positive");
    return {{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}};
}

namespace collision {
bool validWall(const ConvexPolygon& wall, double mapWidth, double mapHeight) noexcept {
    const auto& points = wall.vertices;
    if (points.size() < 3) return false;
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            point.x < 0 || point.y < 0 ||
            point.x > mapWidth || point.y > mapHeight) return false;
    }

    // Every other vertex must be strictly on the same side of each edge.
    // This rejects degenerate, self-intersecting and concave input.
    int winding = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        int edgeSide = 0;
        for (std::size_t j = 0; j < points.size(); ++j) {
            if (j == i || j == (i + 1) % points.size()) continue;
            const double turn = cross(points[i], points[(i + 1) % points.size()], points[j]);
            if (!std::isfinite(turn) || turn == 0) return false;
            const int currentSide = turn > 0 ? 1 : -1;
            if (edgeSide != 0 && edgeSide != currentSide) return false;
            edgeSide = currentSide;
        }
        if (winding != 0 && winding != edgeSide) return false;
        winding = edgeSide;
    }
    return true;
}

bool circleOverlaps(const ConvexPolygon& wall, Position center, double radius) noexcept {
    if (pointInside(wall, center)) return true;
    for (std::size_t i = 0; i < wall.vertices.size(); ++i) {
        if (distanceToSegment(center, wall.vertices[i],
                wall.vertices[(i + 1) % wall.vertices.size()]) <= radius)
            return true;
    }
    return false;
}

bool sweptCircleHits(const ConvexPolygon& wall, Position start,
    Position destination, double radius) noexcept {
    if (circleOverlaps(wall, start, radius) ||
        circleOverlaps(wall, destination, radius)) return true;

    for (std::size_t i = 0; i < wall.vertices.size(); ++i) {
        const Position a = wall.vertices[i];
        const Position b = wall.vertices[(i + 1) % wall.vertices.size()];
        if (segmentsIntersect(start, destination, a, b) ||
            distanceToSegment(a, start, destination) <= radius ||
            distanceToSegment(b, start, destination) <= radius ||
            distanceToSegment(start, a, b) <= radius ||
            distanceToSegment(destination, a, b) <= radius)
            return true;
    }
    return false;
}
} // namespace collision
} // namespace game
