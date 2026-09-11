#pragma once

#include "game/World.h"
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game::net {
constexpr std::size_t maxDebugMessageBytes = 4096;
struct ProtocolReply { std::string text; bool close = false; };
struct EventFrame { std::string map; std::string text; };

// Development protocol only. Session identity comes from the host, never a packet.
// All methods, including disconnect, run on the World owner thread.
class DebugProtocol {
public:
    explicit DebugProtocol(World& world, bool console = false);
    ProtocolReply connect(std::uint64_t session);
    void disconnect(std::uint64_t session);
    ProtocolReply dispatch(std::uint64_t session, std::string_view message);
    EntityId entityFor(std::uint64_t session) const;
    bool canSee(std::uint64_t session, const std::string& map) const;
    std::vector<EventFrame> takeEventFrames();
private:
    World& world_;
    bool console_;
    std::map<std::uint64_t, EntityId> sessions_;
    std::map<EntityId, std::string> knownMaps_;
};
} // namespace game::net
