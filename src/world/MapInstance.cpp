#include "game/world/MapInstance.h"

#include "game/resident/Resident.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace game {

MapInstance::MapInstance(std::string instanceId, MapDefinition definition)
    : instanceId_(std::move(instanceId)), definition_(std::move(definition)) {
    if (instanceId_.empty() || definition_.id.empty())
        throw std::invalid_argument("map IDs must be nonempty");
    if (!std::isfinite(definition_.width) || definition_.width <= 0 ||
        !std::isfinite(definition_.height) || definition_.height <= 0)
        throw std::invalid_argument("map dimensions must be finite and positive");
    for (const auto& wall : definition_.walls)
        if (!collision::validWall(wall, definition_.width, definition_.height))
            throw std::invalid_argument("wall must be a strictly convex polygon inside the map");
}

bool MapInstance::contains(Position position) const noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) &&
        position.x >= 0 && position.x <= definition_.width &&
        position.y >= 0 && position.y <= definition_.height;
}

bool MapInstance::has(ObjectId id) const {
    return objects_.find(id) != objects_.end();
}

bool MapInstance::bodyFits(Position center, double radius) const noexcept {
    return std::isfinite(radius) && radius > 0 && contains(center) &&
        center.x >= radius && center.x <= definition_.width - radius &&
        center.y >= radius && center.y <= definition_.height - radius;
}

bool MapInstance::overlapsWall(Position center, double radius) const noexcept {
    for (const auto& wall : definition_.walls)
        if (collision::circleOverlaps(wall, center, radius)) return true;
    return false;
}

bool MapInstance::pathHitsWall(Position start, Position destination, double radius) const noexcept {
    if (start.x == destination.x && start.y == destination.y) return false;
    for (const auto& wall : definition_.walls)
        if (collision::sweptCircleHits(wall, start, destination, radius)) return true;
    return false;
}

MapResult MapInstance::enter(const Resident& resident) {
    if (resident.mapInstanceId() != instanceId_) return MapResult::WrongMap;
    if (!bodyFits(resident.position(), resident.bodyRadius())) return MapResult::OutOfBounds;
    if (overlapsWall(resident.position(), resident.bodyRadius())) return MapResult::Blocked;
    if (!objects_.insert(resident.id()).second) return MapResult::AlreadyPresent;
    return MapResult::Ok;
}

MapResult MapInstance::leave(const Resident& resident) {
    if (resident.mapInstanceId() != instanceId_) return MapResult::WrongMap;
    if (objects_.erase(resident.id()) == 0) return MapResult::NotPresent;
    return MapResult::Ok;
}

MapResult MapInstance::move(Resident& resident, Position destination) {
    if (resident.mapInstanceId() != instanceId_) return MapResult::WrongMap;
    if (!has(resident.id())) return MapResult::NotPresent;
    if (!bodyFits(destination, resident.bodyRadius())) return MapResult::OutOfBounds;
    if (pathHitsWall(resident.position(), destination, resident.bodyRadius()))
        return MapResult::Blocked;

    static_cast<WorldObject&>(resident).moveTo(destination);
    return MapResult::Ok;
}

MapResult MapInstance::transferTo(Resident& resident, MapInstance& destinationMap,
    Position destination) {
    if (&destinationMap == this) return move(resident, destination);
    if (destinationMap.instanceId_ == instanceId_)
        return MapResult::InstanceIdConflict;
    if (resident.mapInstanceId() != instanceId_) return MapResult::WrongMap;
    if (!has(resident.id())) return MapResult::NotPresent;
    if (!destinationMap.bodyFits(destination, resident.bodyRadius()))
        return MapResult::OutOfBounds;
    if (destinationMap.overlapsWall(destination, resident.bodyRadius()))
        return MapResult::Blocked;
    if (!destinationMap.objects_.insert(resident.id()).second)
        return MapResult::AlreadyPresent;

    try {
        static_cast<WorldObject&>(resident).transferTo(destinationMap.instanceId_, destination);
    } catch (...) {
        destinationMap.objects_.erase(resident.id());
        throw;
    }
    objects_.erase(resident.id());
    return MapResult::Ok;
}

} // namespace game
