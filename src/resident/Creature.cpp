#include "game/resident/Creature.h"

#include <utility>

namespace game {

Creature::Creature(ResidentInit init) : Resident(ObjectKind::Creature, std::move(init)) {}

} // namespace game
