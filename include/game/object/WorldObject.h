#pragma once

#include "game/Position.h"
#include "game/object/Object.h"

#include <string>

namespace game {

class MapInstance;

class WorldObject : public Object {
public:
    ~WorldObject() noexcept override = default;

    const std::string& mapInstanceId() const noexcept { return mapInstanceId_; }
    Position position() const noexcept { return position_; }

protected:
    WorldObject(ObjectId id, ObjectKind kind, std::string mapInstanceId, Position position);

private:
    // The map checks its bounds before committing either mutation.
    void moveTo(Position position);
    void transferTo(std::string mapInstanceId, Position position);

    std::string mapInstanceId_;
    Position position_;

    friend class MapInstance;
};

} // namespace game
