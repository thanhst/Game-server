#include "game/legacy/Packet.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace game::legacy {
namespace {

template <typename Signed, typename Unsigned>
Signed signedValue(Unsigned value) {
    static_assert(std::is_signed_v<Signed> && std::is_unsigned_v<Unsigned>);
    static_assert(sizeof(Signed) == sizeof(Unsigned));
    const auto maximum = static_cast<Unsigned>(std::numeric_limits<Signed>::max());
    if (value <= maximum) return static_cast<Signed>(value);
    // Avoid C++17 implementation-defined unsigned-to-signed narrowing.
    return static_cast<Signed>(-1 - static_cast<Signed>(static_cast<Unsigned>(~value)));
}

void validateUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[i++]);
        if (first < 0x80U) continue;
        std::size_t extra = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            extra = 1; codepoint = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            extra = 2; codepoint = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            extra = 3; codepoint = first & 0x07U; minimum = 0x10000U;
        } else {
            throw ProtocolError("invalid UTF-8 leading byte");
        }
        if (extra > text.size() - i) throw ProtocolError("truncated UTF-8 sequence");
        for (std::size_t j = 0; j < extra; ++j) {
            const auto next = static_cast<std::uint8_t>(text[i++]);
            if ((next & 0xc0U) != 0x80U) throw ProtocolError("invalid UTF-8 continuation");
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            throw ProtocolError("invalid UTF-8 code point");
        }
    }
}

} // namespace

PacketReader::PacketReader(const Bytes& bytes) : PacketReader(bytes.data(), bytes.size()) {}

PacketReader::PacketReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
    if (size != 0 && data == nullptr) throw ProtocolError("null packet storage");
}

std::size_t PacketReader::remaining() const noexcept { return size_ - position_; }

void PacketReader::require(std::size_t count) const {
    if (count > remaining()) throw ProtocolError("truncated packet payload");
}

std::uint64_t PacketReader::readUnsigned(std::size_t count) {
    require(count);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8U) | data_[position_++];
    return value;
}

std::uint8_t PacketReader::readUnsignedByte() { return static_cast<std::uint8_t>(readUnsigned(1)); }
std::int8_t PacketReader::readByte() { return signedValue<std::int8_t>(readUnsignedByte()); }
bool PacketReader::readBoolean() { return readUnsignedByte() != 0; }
std::uint16_t PacketReader::readUnsignedShort() { return static_cast<std::uint16_t>(readUnsigned(2)); }
std::int16_t PacketReader::readShort() { return signedValue<std::int16_t>(readUnsignedShort()); }
std::int32_t PacketReader::readInt() {
    return signedValue<std::int32_t>(static_cast<std::uint32_t>(readUnsigned(4)));
}
std::int64_t PacketReader::readLong() { return signedValue<std::int64_t>(readUnsigned(8)); }

float PacketReader::readFloat() {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    const auto bits = static_cast<std::uint32_t>(readUnsigned(4));
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double PacketReader::readDouble() {
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559);
    const auto bits = readUnsigned(8);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

char16_t PacketReader::readChar() { return static_cast<char16_t>(readUnsignedShort()); }

std::string PacketReader::readUTF() {
    const auto count = readUnsignedShort();
    require(count);
    if (count == 0) return {};
    std::string result(reinterpret_cast<const char*>(data_ + position_), count);
    validateUtf8(result);
    position_ += count;
    return result;
}

std::u16string PacketReader::readModifiedUTF() {
    const auto count = readUnsignedShort();
    require(count);
    const auto end = position_ + count;
    std::u16string result;
    result.reserve(count);
    while (position_ < end) {
        const auto first = data_[position_++];
        if (first <= 0x7fU) {
            result.push_back(static_cast<char16_t>(first));
            continue;
        }
        const std::size_t extra = (first & 0xe0U) == 0xc0U ? 1 : (first & 0xf0U) == 0xe0U ? 2 : 0;
        if (extra == 0 || extra > end - position_) throw ProtocolError("malformed modified UTF-8");
        std::uint16_t value = first & (extra == 1 ? 0x1fU : 0x0fU);
        for (std::size_t i = 0; i < extra; ++i) {
            const auto next = data_[position_++];
            if ((next & 0xc0U) != 0x80U) throw ProtocolError("malformed modified UTF-8 continuation");
            value = static_cast<std::uint16_t>((value << 6U) | (next & 0x3fU));
        }
        // DataInputStream preserves UTF-16 code units, including isolated
        // surrogates, and accepts overlong two/three-byte sequences.
        result.push_back(static_cast<char16_t>(value));
    }
    return result;
}

Bytes PacketReader::readBytes(std::size_t count) {
    require(count);
    if (count == 0) return {};
    Bytes result(data_ + position_, data_ + position_ + count);
    position_ += count;
    return result;
}

void PacketReader::skip(std::size_t count) { require(count); position_ += count; }
void PacketReader::requireEnd() const {
    if (remaining() != 0) throw ProtocolError("unexpected trailing packet data");
}

void PacketWriter::reserveAdditional(std::size_t count) {
    if (count > maxBytes_ - bytes_.size()) throw ProtocolError("packet payload exceeds configured limit");
    // vector's geometric growth avoids a reallocation for every primitive.
}

PacketWriter& PacketWriter::writeUnsigned(std::uint64_t value, std::size_t count) {
    reserveAdditional(count);
    for (std::size_t i = count; i > 0; --i) bytes_.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8U)));
    return *this;
}

PacketWriter& PacketWriter::writeByte(std::int32_t value) { return writeUnsigned(static_cast<std::uint8_t>(value), 1); }
PacketWriter& PacketWriter::writeBoolean(bool value) { return writeByte(value ? 1 : 0); }
PacketWriter& PacketWriter::writeShort(std::int32_t value) { return writeUnsigned(static_cast<std::uint16_t>(value), 2); }
PacketWriter& PacketWriter::writeInt(std::int32_t value) { return writeUnsigned(static_cast<std::uint32_t>(value), 4); }
PacketWriter& PacketWriter::writeLong(std::int64_t value) { return writeUnsigned(static_cast<std::uint64_t>(value), 8); }

PacketWriter& PacketWriter::writeFloat(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if (std::isnan(value)) bits = 0x7fc00000U; // Float.floatToIntBits canonical NaN
    return writeUnsigned(bits, 4);
}

PacketWriter& PacketWriter::writeDouble(double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559);
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if (std::isnan(value)) bits = 0x7ff8000000000000ULL;
    return writeUnsigned(bits, 8);
}

PacketWriter& PacketWriter::writeChar(char16_t value) { return writeUnsigned(value, 2); }

PacketWriter& PacketWriter::writeUTF(std::string_view utf8) {
    if (utf8.size() > 65535U) throw ProtocolError("UTF-8 string exceeds unsigned-short byte length");
    validateUtf8(utf8);
    reserveAdditional(2 + utf8.size());
    writeShort(static_cast<std::int32_t>(utf8.size()));
    return writeBytes(reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size());
}

PacketWriter& PacketWriter::writeModifiedUTF(std::u16string_view utf16) {
    std::size_t count = 0;
    for (const auto ch : utf16) {
        count += ch != 0 && ch <= 0x7fU ? 1 : ch <= 0x7ffU ? 2 : 3;
        if (count > 65535U) throw ProtocolError("modified UTF-8 string exceeds unsigned-short byte length");
    }
    reserveAdditional(2 + count);
    writeShort(static_cast<std::int32_t>(count));
    for (const auto ch : utf16) {
        if (ch != 0 && ch <= 0x7fU) {
            writeByte(ch);
        } else if (ch <= 0x7ffU) {
            writeByte(0xc0U | ((ch >> 6U) & 0x1fU));
            writeByte(0x80U | (ch & 0x3fU));
        } else {
            writeByte(0xe0U | ((ch >> 12U) & 0x0fU));
            writeByte(0x80U | ((ch >> 6U) & 0x3fU));
            writeByte(0x80U | (ch & 0x3fU));
        }
    }
    return *this;
}

PacketWriter& PacketWriter::writeBytes(const Bytes& bytes) {
    if (&bytes == &bytes_) {
        const Bytes copy = bytes;
        return writeBytes(copy.data(), copy.size());
    }
    return writeBytes(bytes.data(), bytes.size());
}

PacketWriter& PacketWriter::writeBytes(const std::uint8_t* data, std::size_t size) {
    reserveAdditional(size);
    if (size != 0) {
        if (data == nullptr) throw ProtocolError("null packet source");
        bytes_.insert(bytes_.end(), data, data + size);
    }
    return *this;
}

Bytes PacketWriter::take() { Bytes result; result.swap(bytes_); return result; }
Packet PacketWriter::packet(std::int8_t command) const { return {command, bytes_}; }

} // namespace game::legacy
