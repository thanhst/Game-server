#pragma once

#include "game/World.h"
#include <cstdint>
#include <iosfwd>

namespace game::net {
int runConsole(World& world, std::istream& input, std::ostream& output);
int runServer(World& world, std::uint16_t port);
} // namespace game::net
