#include "game/net/BinaryPacketCodec.h"

#include <limits>

namespace game::net {

BinaryPacketCodec::BinaryPacketCodec(BinaryPacketLimits limits) : limits_(limits) {
    constexpr std::size_t engineMaximumMessageBytes = 16 * 1024 * 1024;
    if (limits_.maxIncomingMessageBytes == 0 || limits_.maxOutgoingMessageBytes == 0 ||
        limits_.maxIncomingMessageBytes > engineMaximumMessageBytes ||
        limits_.maxOutgoingMessageBytes > engineMaximumMessageBytes) {
        throw legacy::ProtocolError("binary packet limits must be 1..16 MiB including command byte");
    }
}

legacy::Packet BinaryPacketCodec::decode(const std::uint8_t* message, std::size_t size) const {
    if (size == 0) throw legacy::ProtocolError("binary message is missing its command byte");
    if (message == nullptr) throw legacy::ProtocolError("null binary message storage");
    if (size > limits_.maxIncomingMessageBytes) throw legacy::ProtocolError("binary message exceeds receive limit");
    const auto first = message[0];
    const auto command = first <= 127U ? static_cast<std::int8_t>(first)
                                     : static_cast<std::int8_t>(static_cast<int>(first) - 256);
    return legacy::Packet{command, legacy::Bytes(message + 1, message + size)};
}

legacy::Bytes BinaryPacketCodec::encode(const legacy::Packet& packet) const {
    if (packet.payload.size() > limits_.maxOutgoingMessageBytes - 1) {
        throw legacy::ProtocolError("binary message exceeds send limit");
    }
    legacy::Bytes message;
    message.reserve(packet.payload.size() + 1);
    message.push_back(static_cast<std::uint8_t>(packet.command));
    message.insert(message.end(), packet.payload.begin(), packet.payload.end());
    return message;
}

legacy::Packet makeServerHello(std::string_view advertisedHost, std::int32_t port,
                              bool redirect, std::int32_t voicePort) {
    if (port < 1 || port > 65535 || voicePort < 1 || voicePort > 65535) {
        throw legacy::ProtocolError("server hello port is outside 1..65535");
    }
    legacy::PacketWriter writer(2 + 4 + 2 + 65535 + 4 + 1 + 4);
    writer.writeShort(binaryProtocolVersion).writeInt(binaryProtocolFlags)
        .writeUTF(advertisedHost).writeInt(port).writeBoolean(redirect).writeInt(voicePort);
    return writer.packet(sessionHandshakeCommand);
}

} // namespace game::net
