#pragma once

#include "game/Content.h"
#include "game/object/Object.h"

#include <cstddef>
#include <set>
#include <string>

namespace game {

class Resident;

enum class MapResult {
    Ok,
    WrongMap,
    InstanceIdConflict,
    NotPresent,
    AlreadyPresent,
    OutOfBounds,
    Blocked
};

// A runtime map with rectangular bounds and convex static walls for residents.
// The world owns residents; this class tracks their membership and location.
class MapInstance final {
public:
    MapInstance(std::string instanceId, MapDefinition definition);

    const std::string& id() const noexcept { return instanceId_; }
    const MapDefinition& definition() const noexcept { return definition_; }
    bool contains(Position position) const noexcept;
    bool has(ObjectId id) const;
    std::size_t objectCount() const noexcept { return objects_.size(); }

    MapResult enter(const Resident& resident);
    MapResult leave(const Resident& resident);
    // Low-level commits: the caller validates speed, status and authority.
    MapResult move(Resident& resident, Position destination);
    MapResult transferTo(Resident& resident, MapInstance& destinationMap,
        Position destination);

private:
    bool bodyFits(Position center, double radius) const noexcept;
    bool overlapsWall(Position center, double radius) const noexcept;
    bool pathHitsWall(Position start, Position destination, double radius) const noexcept;

    std::string instanceId_;
    MapDefinition definition_;
    std::set<ObjectId> objects_;
};

} // namespace game
