#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game::application {
using SessionId = std::uint64_t;
struct Delivery {
    SessionId session = 0;
    std::vector<std::uint8_t> bytes;
    bool close = false;
    std::string reason; // Operator diagnostic, never a credential or packet dump.
};
// One serialized owner calls these methods. Modules own connection codecs,
// identities, worlds and persistence; the engine owns sockets and byte queues.
// A new game implements this contract without depending on any HUNR type/ID.
class GameModule {
public:
    virtual ~GameModule() = default;
    virtual void connected(SessionId, std::int64_t nowMs) = 0;
    virtual void received(SessionId, const std::uint8_t*, std::size_t, std::int64_t nowMs) = 0;
    virtual void disconnected(SessionId, std::int64_t nowMs) = 0;
    virtual void tick(std::int64_t nowMs) = 0;
    virtual std::vector<Delivery> takeDeliveries() = 0;
};
struct StreamHostOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 14445;
    std::uint32_t maxConnections = 128;
};
int runStreamHost(GameModule&, const StreamHostOptions&);
} // namespace game::application
