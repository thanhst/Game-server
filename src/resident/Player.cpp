#include "game/resident/Player.h"

#include <utility>

namespace game {

Player::Player(ResidentInit init) : Resident(ObjectKind::Player, std::move(init)) {}

} // namespace game
