#pragma once

#include "game/resident/Resident.h"

namespace game {

class Player final : public Resident {
public:
    explicit Player(ResidentInit init);
};

} // namespace game
