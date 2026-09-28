#include "game/resident/Resident.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace game {

Resident::Resident(ObjectKind kind, ResidentInit init)
    : WorldObject(init.id, kind, std::move(init.mapInstanceId), init.position),
      definition_(std::move(init.definition)),
      vitality_(std::move(init.vitality)),
      mana_(std::move(init.mana)),
      bodyRadius_(init.bodyRadius) {
    if (definition_.empty())
        throw std::invalid_argument("resident definition must be nonempty");
    if (!std::isfinite(bodyRadius_) || bodyRadius_ <= 0)
        throw std::invalid_argument("resident body radius must be finite and positive");
}

} // namespace game
