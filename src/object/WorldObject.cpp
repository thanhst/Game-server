#include "game/object/WorldObject.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace game {
namespace {
void requireLocation(const std::string& mapInstanceId, Position position) {
    if (mapInstanceId.empty()) throw std::invalid_argument("map instance id must be nonempty");
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        throw std::invalid_argument("position must be finite");
}
} // namespace

WorldObject::WorldObject(ObjectId id, ObjectKind kind, std::string mapInstanceId, Position position)
    : Object(id, kind), mapInstanceId_(std::move(mapInstanceId)), position_(position) {
    requireLocation(mapInstanceId_, position_);
}

void WorldObject::moveTo(Position position) {
    requireLocation(mapInstanceId_, position);
    position_ = position;
}

void WorldObject::transferTo(std::string mapInstanceId, Position position) {
    requireLocation(mapInstanceId, position);
    mapInstanceId_.swap(mapInstanceId);
    position_ = position;
}

} // namespace game
