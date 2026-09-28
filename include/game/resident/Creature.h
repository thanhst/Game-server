#pragma once

#include "game/resident/Resident.h"

namespace game {

class Creature final : public Resident {
public:
    explicit Creature(ResidentInit init);
};

} // namespace game
