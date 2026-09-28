#pragma once

#include "game/Position.h"

#include <vector>

namespace game {

// Vertices are in world units, ordered around a strictly convex boundary;
// do not repeat the first vertex at the end.
// A concave obstacle can be composed from several convex polygons.
struct ConvexPolygon {
    std::vector<Position> vertices;

    static ConvexPolygon rectangle(double x, double y, double width, double height);
};

namespace collision {
bool validWall(const ConvexPolygon& wall, double mapWidth, double mapHeight) noexcept;
bool circleOverlaps(const ConvexPolygon& wall, Position center, double radius) noexcept;
bool sweptCircleHits(const ConvexPolygon& wall, Position start,
    Position destination, double radius) noexcept;
} // namespace collision

} // namespace game
