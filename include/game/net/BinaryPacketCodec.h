#pragma once

#include "game/legacy/Packet.h"

namespace game::net {

inline constexpr std::uint16_t binaryProtocolVersion = 1;
inline constexpr std::uint32_t binaryProtocolFlags = 0;
inline constexpr std::int8_t sessionHandshakeCommand = -27;

struct BinaryPacketLimits {
    // Includes the command byte; ServerEngine owns its separate BE32 prefix.
    std::size_t maxIncomingMessageBytes = 256 * 1024;
    std::size_t maxOutgoingMessageBytes = 8 * 1024 * 1024;
};

// One ServerEngine SE_EVENT_MESSAGE is exactly one command plus logical
// payload. This stateless codec never accumulates TCP chunks or adds a length
// prefix, Base64, XOR, compression, or special resource-command framing.
// Packet's shared primitive serialization lives in legacy/Packet.h; it carries
// no player, account, skill, or game-content dependencies.
class BinaryPacketCodec {
public:
    explicit BinaryPacketCodec(BinaryPacketLimits limits = {});
    legacy::Packet decode(const std::uint8_t* message, std::size_t size) const;
    legacy::Packet decode(const legacy::Bytes& message) const { return decode(message.data(), message.size()); }
    legacy::Bytes encode(const legacy::Packet& packet) const;
    const BinaryPacketLimits& limits() const noexcept { return limits_; }

private:
    BinaryPacketLimits limits_;
};

// Response to an empty -27 request. Version/flags are explicit because the
// revised client uses the engine's ordinary TCP transport instead of Java XOR.
legacy::Packet makeServerHello(std::string_view advertisedHost, std::int32_t port,
                              bool redirect, std::int32_t voicePort);

} // namespace game::net
