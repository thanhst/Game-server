#include "game/legacy/LegacyCodec.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace game::legacy {
namespace {

Bytes base64Encode(const Bytes& input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Bytes output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < input.size(); i += 3) {
        const auto remaining = input.size() - i;
        const auto a = input[i];
        const auto b = remaining > 1 ? input[i + 1] : 0U;
        const auto c = remaining > 2 ? input[i + 2] : 0U;
        output.push_back(static_cast<std::uint8_t>(alphabet[a >> 2U]));
        output.push_back(static_cast<std::uint8_t>(alphabet[((a & 3U) << 4U) | (b >> 4U)]));
        output.push_back(remaining > 1 ? static_cast<std::uint8_t>(alphabet[((b & 15U) << 2U) | (c >> 6U)]) : '=');
        output.push_back(remaining > 2 ? static_cast<std::uint8_t>(alphabet[c & 63U]) : '=');
    }
    return output;
}

int base64Digit(std::uint8_t byte) {
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    if (byte == '+') return 62;
    if (byte == '/') return 63;
    return -1;
}

// java.util.Base64.getDecoder() accepts missing final padding and nonzero
// unused tail bits, but rejects whitespace and the URL-safe alphabet.
std::optional<Bytes> base64Decode(const Bytes& input) {
    Bytes output;
    output.reserve(input.size() / 4 * 3 + 3);
    std::uint32_t bits = 0;
    unsigned digits = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '=') {
            if (digits == 2 && input.size() - i == 2 && input[i + 1] == '=') {
                output.push_back(static_cast<std::uint8_t>(bits >> 4U));
                return output;
            }
            if (digits == 3 && input.size() - i == 1) {
                output.push_back(static_cast<std::uint8_t>(bits >> 10U));
                output.push_back(static_cast<std::uint8_t>(bits >> 2U));
                return output;
            }
            return std::nullopt;
        }
        const auto value = base64Digit(input[i]);
        if (value < 0) return std::nullopt;
        bits = (bits << 6U) | static_cast<std::uint32_t>(value);
        if (++digits == 4) {
            output.push_back(static_cast<std::uint8_t>(bits >> 16U));
            output.push_back(static_cast<std::uint8_t>(bits >> 8U));
            output.push_back(static_cast<std::uint8_t>(bits));
            bits = 0;
            digits = 0;
        }
    }
    if (digits == 1) return std::nullopt;
    if (digits == 2) output.push_back(static_cast<std::uint8_t>(bits >> 4U));
    if (digits == 3) {
        output.push_back(static_cast<std::uint8_t>(bits >> 10U));
        output.push_back(static_cast<std::uint8_t>(bits >> 2U));
    }
    return output;
}

std::int8_t signedCommand(std::uint8_t value) {
    return value <= 127U ? static_cast<std::int8_t>(value) : static_cast<std::int8_t>(static_cast<int>(value) - 256);
}

} // namespace

LegacyCodec::LegacyCodec(CodecLimits limits) : limits_(limits) {
    if (limits_.maxInboundEncodedBytes > 0x00ffffffU ||
        limits_.maxOutboundEncodedBytes > 0x0fffffffU || limits_.maxBufferedBytes < 4 ||
        limits_.maxInboundEncodedBytes > limits_.maxBufferedBytes - 4 ||
        limits_.maxBatchMessages == 0 || limits_.maxBatchMessages > 32767) {
        throw ProtocolError("invalid legacy codec limits");
    }
}

void LegacyCodec::requireHealthy() const {
    if (failed_) throw ProtocolError("legacy stream has failed; close connection or explicitly reset");
}

void LegacyCodec::feed(const std::uint8_t* data, std::size_t size) {
    requireHealthy();
    if ((size != 0 && data == nullptr) || size > limits_.maxBufferedBytes - bufferedBytes()) {
        failed_ = true;
        throw ProtocolError("legacy receive buffer exceeds configured limit or input is null");
    }
    if (size == 0) return;
    if (inputOffset_ != 0) {
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(inputOffset_));
        inputOffset_ = 0;
    }
    input_.insert(input_.end(), data, data + size);
}

std::uint8_t LegacyCodec::readPlain(std::size_t offset, std::size_t& cursor) const {
    auto value = input_[inputOffset_ + offset];
    if (connected()) {
        value ^= key_[cursor];
        cursor = (cursor + 1) % key_.size();
    }
    return value;
}

std::optional<Packet> LegacyCodec::tryDecode() {
    requireHealthy();
    if (bufferedBytes() < 4) return std::nullopt;
    auto cursor = readCursor_;
    const auto command = signedCommand(readPlain(0, cursor));
    std::size_t length = 0;
    for (std::size_t i = 1; i <= 3; ++i) length = (length << 8U) | readPlain(i, cursor);
    if (length > limits_.maxInboundEncodedBytes) {
        failed_ = true;
        throw ProtocolError("legacy inbound frame exceeds configured limit");
    }
    if (length > bufferedBytes() - 4) return std::nullopt;
    Bytes payload;
    payload.reserve(length);
    for (std::size_t i = 0; i < length; ++i) payload.push_back(readPlain(i + 4, cursor));
    // The Java receive path attempts Base64 for EVERY command (including
    // GET_IMAGE_SOURCE), retaining the original bytes if decoding fails.
    if (!payload.empty()) {
        auto decoded = base64Decode(payload);
        if (decoded) payload = std::move(*decoded);
    }
    readCursor_ = cursor;
    inputOffset_ += length + 4;
    if (inputOffset_ == input_.size()) {
        input_.clear();
        inputOffset_ = 0;
    }
    return Packet{command, std::move(payload)};
}

bool LegacyCodec::isSpecialOutboundCommand(std::int8_t command) noexcept {
    switch (command) {
    case -32: // BACKGROUND_TEMPLATE
    case -66: // GET_EFFDATA
    case 11:  // REQUEST_NPCTEMPLATE
    case -67: // REQUEST_ICON
    case -87: // UPDATE_DATA
    case 66:  // GET_IMG_BY_NAME
    case 120:
    case -74: // GET_IMAGE_SOURCE
    case 59:
    case 60:
        return true;
    default:
        return false;
    }
}

void LegacyCodec::appendEncrypted(Bytes& output, std::uint8_t value, std::size_t& cursor) const {
    if (connected()) {
        value ^= key_[cursor];
        cursor = (cursor + 1) % key_.size();
    }
    output.push_back(value);
}

Bytes LegacyCodec::encode(const Packet& packet) {
    requireHealthy();
    const bool skipBase64 = packet.command == getImageSourceCommand;
    const bool specialLength = connected() && isSpecialOutboundCommand(packet.command);
    const auto maxLength = std::min(limits_.maxOutboundEncodedBytes,
                                   static_cast<std::size_t>(specialLength ? 0x0fffffffU : 0x00ffffffU));
    // Check logical length before any derived length computation/allocation.
    if (packet.payload.size() > maxLength) throw ProtocolError("legacy outbound payload exceeds configured limit");
    const auto length = skipBase64 ? packet.payload.size() : ((packet.payload.size() + 2) / 3) * 4;
    if (length > maxLength) throw ProtocolError("legacy Base64 frame exceeds configured limit");
    const auto encoded = skipBase64 ? packet.payload : base64Encode(packet.payload);
    Bytes output;
    output.reserve(length + (specialLength ? 5 : 4));
    auto cursor = writeCursor_;
    appendEncrypted(output, static_cast<std::uint8_t>(packet.command), cursor);
    if (specialLength) {
        // Session.doSendMessage: least-significant chunk first; subtract 128
        // from each byte, including the final FOUR-bit chunk, before XOR.
        for (unsigned shift = 0; shift < 28; shift += 8) {
            const auto mask = shift == 24 ? 0x0fU : 0xffU;
            const auto chunk = static_cast<int>((length >> shift) & mask);
            appendEncrypted(output, static_cast<std::uint8_t>(chunk - 128), cursor);
        }
    } else {
        appendEncrypted(output, static_cast<std::uint8_t>(length >> 16U), cursor);
        appendEncrypted(output, static_cast<std::uint8_t>(length >> 8U), cursor);
        appendEncrypted(output, static_cast<std::uint8_t>(length), cursor);
    }
    for (const auto byte : encoded) appendEncrypted(output, byte, cursor);
    writeCursor_ = cursor; // Commit only after construction succeeds.
    return output;
}

void LegacyCodec::validateKey(const Bytes& key) {
    // Java stores cursors in signed bytes: length >= 128 overflows into a
    // negative index. The live Java collector generates exactly ONE byte.
    if (key.empty() || key.size() > 127) throw ProtocolError("legacy key must contain 1..127 bytes");
}

void LegacyCodec::activateKey(const Bytes& key) {
    requireHealthy();
    if (connected()) throw ProtocolError("legacy key is already active");
    validateKey(key);
    key_ = key;
    readCursor_ = 0;
    writeCursor_ = 0;
}

Bytes LegacyCodec::encodeHandshake(const Bytes& key, std::string_view host, std::int32_t port,
                                 bool redirect, std::int32_t voicePort) {
    requireHealthy();
    if (connected()) throw ProtocolError("duplicate legacy handshake");
    validateKey(key);
    PacketWriter writer(limits_.maxOutboundEncodedBytes);
    writer.writeByte(static_cast<std::int32_t>(key.size())).writeByte(key.front());
    for (std::size_t i = 1; i < key.size(); ++i) writer.writeByte(key[i] ^ key[i - 1]);
    writer.writeUTF(host).writeInt(port).writeBoolean(redirect).writeInt(voicePort);
    auto output = encode(writer.packet(getSessionIdCommand));
    activateKey(key);
    return output;
}

Packet LegacyCodec::makeBatch(const std::vector<Packet>& packets) const {
    requireHealthy();
    if (packets.empty() || packets.size() > limits_.maxBatchMessages) throw ProtocolError("invalid legacy batch count");
    PacketWriter writer(limits_.maxOutboundEncodedBytes);
    writer.writeShort(static_cast<std::int32_t>(packets.size()));
    for (const auto& packet : packets) {
        if (packet.payload.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw ProtocolError("legacy batch child length exceeds signed int");
        }
        writer.writeByte(packet.command).writeInt(static_cast<std::int32_t>(packet.payload.size())).writeBytes(packet.payload);
    }
    return writer.packet(batchMessageCommand);
}

std::vector<Packet> LegacyCodec::unpackBatch(const Packet& packet) const {
    requireHealthy();
    if (packet.command != batchMessageCommand || packet.payload.size() > limits_.maxOutboundEncodedBytes) {
        throw ProtocolError("invalid legacy batch payload");
    }
    PacketReader reader(packet.payload);
    const auto count = reader.readUnsignedShort();
    if (count == 0 || count > limits_.maxBatchMessages || count > reader.remaining() / 5) {
        throw ProtocolError("invalid legacy batch count");
    }
    std::vector<Packet> packets;
    packets.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto command = reader.readByte();
        const auto length = reader.readInt();
        if (length < 0) throw ProtocolError("negative legacy batch child length");
        packets.push_back(Packet{command, reader.readBytes(static_cast<std::size_t>(length))});
    }
    reader.requireEnd();
    return packets;
}

void LegacyCodec::reset() {
    input_.clear();
    inputOffset_ = 0;
    key_.clear();
    readCursor_ = 0;
    writeCursor_ = 0;
    failed_ = false;
}

} // namespace game::legacy
