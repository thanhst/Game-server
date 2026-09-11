#pragma once

#include "game/legacy/LegacyContent.h"
#include "game/legacy/Packet.h"

#include <array>
#include <cstdint>

namespace game::legacy {

// Versions come from the deployment configuration, just as Config.java reads
// game.{data,map,skill,item}.version. The SQL snapshot does not define them.
struct CacheVersions {
    std::int8_t data = 0;
    std::int8_t map = 0;
    std::int8_t skill = 0;
    std::int8_t itemBase = 0;
};

class LegacyCaches final {
public:
    static LegacyCaches build(const ContentSnapshot& content, CacheVersions versions);
    Packet versionPacket() const;
    Packet dataPacket() const; // UPDATE_DATA: six source animation/paint caches
    Packet mapPacket() const;
    Packet skillPacket() const;
    Packet itemPacket(std::int32_t type) const; // 0,1,2 and 100, as Service.updateItem

    const Bytes& mapBytes() const noexcept { return map_; }
    const Bytes& skillBytes() const noexcept { return skill_; }
    std::uint8_t effectiveItemVersion() const noexcept { return itemVersion_; }
    CacheVersions versions() const noexcept { return versions_; }

private:
    CacheVersions versions_;
    std::uint8_t itemVersion_ = 0;
    Bytes version_, data_, map_, skill_, heads_;
    std::array<Bytes, 3> items_;
};

} // namespace game::legacy
