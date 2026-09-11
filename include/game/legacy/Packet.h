#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace game::legacy {

using Bytes = std::vector<std::uint8_t>;

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A logical message, before Base64, framing and per-session XOR.
struct Packet {
    std::int8_t command{};
    Bytes payload;
};

// Matches the actual Hunr2026 FastDataInputStream wire primitives. In this
// source tree readUTF means standard UTF-8, not DataInputStream modified UTF-8.
class PacketReader {
public:
    explicit PacketReader(const Bytes& bytes);
    PacketReader(const std::uint8_t* data, std::size_t size);
    PacketReader(Bytes&&) = delete; // readers borrow storage

    std::size_t remaining() const noexcept;
    std::size_t position() const noexcept { return position_; }
    std::uint8_t readUnsignedByte();
    std::int8_t readByte();
    bool readBoolean();
    std::uint16_t readUnsignedShort();
    std::int16_t readShort();
    std::int32_t readInt();
    std::int64_t readLong();
    float readFloat();
    double readDouble();
    char16_t readChar();
    std::string readUTF();
    // Only for formats actually written with java.io.DataOutputStream.
    std::u16string readModifiedUTF();
    Bytes readBytes(std::size_t count);
    void skip(std::size_t count);
    void requireEnd() const;

private:
    void require(std::size_t count) const;
    std::uint64_t readUnsigned(std::size_t count);
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t position_{};
};

class PacketWriter {
public:
    explicit PacketWriter(std::size_t maxBytes = 0x0fffffffU) : maxBytes_(maxBytes) {}
    PacketWriter& writeByte(std::int32_t value);
    PacketWriter& writeBoolean(bool value);
    PacketWriter& writeShort(std::int32_t value);
    PacketWriter& writeInt(std::int32_t value);
    PacketWriter& writeLong(std::int64_t value);
    PacketWriter& writeFloat(float value);
    PacketWriter& writeDouble(double value);
    PacketWriter& writeChar(char16_t value);
    PacketWriter& writeUTF(std::string_view utf8);
    PacketWriter& writeModifiedUTF(std::u16string_view utf16);
    PacketWriter& writeBytes(const Bytes& bytes);
    PacketWriter& writeBytes(const std::uint8_t* data, std::size_t size);
    const Bytes& data() const noexcept { return bytes_; }
    Bytes take();
    Packet packet(std::int8_t command) const;

private:
    void reserveAdditional(std::size_t count);
    PacketWriter& writeUnsigned(std::uint64_t value, std::size_t count);
    std::size_t maxBytes_;
    Bytes bytes_;
};

} // namespace game::legacy
