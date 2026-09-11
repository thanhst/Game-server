#pragma once

#include "game/legacy/Packet.h"

#include <optional>

namespace game::legacy {

inline constexpr std::int8_t getSessionIdCommand = -27;
inline constexpr std::int8_t getImageSourceCommand = -74;
inline constexpr std::int8_t batchMessageCommand = 115;

struct CodecLimits {
    std::size_t maxInboundEncodedBytes = 0x00ffffffU;
    std::size_t maxOutboundEncodedBytes = 0x00ffffffU;
    std::size_t maxBufferedBytes = 0x01010004U;
    std::size_t maxBatchMessages = 50;
};

// Server-side directionality is deliberate: Session.java receives 24-bit
// lengths for ALL commands, but transmits special resource commands with its
// unusual four-byte 28-bit encoding after the handshake.
// Each instance belongs to one connection and one serialized execution lane.
class LegacyCodec {
public:
    explicit LegacyCodec(CodecLimits limits = {});
    void feed(const std::uint8_t* data, std::size_t size);
    void feed(const Bytes& bytes) { feed(bytes.data(), bytes.size()); }
    // Decode one packet at a time. Process/answer GET_SESSION_ID before calling
    // again, because the next buffered packet may already be encrypted.
    std::optional<Packet> tryDecode();
    Bytes encode(const Packet& packet);
    Bytes encodeHandshake(const Bytes& key, std::string_view host, std::int32_t port,
                          bool redirect, std::int32_t voicePort);
    // For replay/testing or restoring an explicitly agreed key. Handshake is
    // preferable on live sessions. Cursors start at zero independently.
    void activateKey(const Bytes& key);
    bool connected() const noexcept { return !key_.empty(); }
    bool failed() const noexcept { return failed_; }
    std::size_t bufferedBytes() const noexcept { return input_.size() - inputOffset_; }
    void reset();

    static bool isSpecialOutboundCommand(std::int8_t command) noexcept;
    Packet makeBatch(const std::vector<Packet>& packets) const;
    // Structural helper for outbound/replay batches. Inbound dispatch must not
    // automatically unpack: Java's MessageHandler does not accept batch input.
    std::vector<Packet> unpackBatch(const Packet& packet) const;

private:
    void requireHealthy() const;
    static void validateKey(const Bytes& key);
    std::uint8_t readPlain(std::size_t offset, std::size_t& cursor) const;
    void appendEncrypted(Bytes& output, std::uint8_t value, std::size_t& cursor) const;
    CodecLimits limits_;
    Bytes input_;
    std::size_t inputOffset_{};
    Bytes key_;
    std::size_t readCursor_{};
    std::size_t writeCursor_{};
    bool failed_{};
};

} // namespace game::legacy
