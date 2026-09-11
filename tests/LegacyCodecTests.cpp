#include "game/legacy/LegacyCodec.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace game::legacy;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void expectProtocolError(Action action, const char* message) {
    try { action(); } catch (const ProtocolError&) { return; }
    throw std::runtime_error(message);
}

void primitiveFixtures() {
    // Independently written big-endian fixture, not a reader/writer round trip.
    const Bytes fixture = {
        0x80, 0xff, 0x01, 0x00, 0x80, 0x00, 0xff, 0xff,
        0x80, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x3f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0xc0, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    PacketWriter writer;
    writer.writeByte(-128).writeByte(255).writeBoolean(true).writeBoolean(false)
        .writeShort(-32768).writeShort(65535).writeInt(std::numeric_limits<std::int32_t>::min())
        .writeInt(0x01020304).writeLong(std::numeric_limits<std::int64_t>::min()).writeLong(-1)
        .writeFloat(1.0F).writeFloat(-0.0F).writeDouble(-2.5);
    expect(writer.data() == fixture, "big-endian primitive encoding differs from Java fixture");
    PacketReader reader(fixture);
    expect(reader.readByte() == -128 && reader.readUnsignedByte() == 255, "signed byte decoding failed");
    expect(reader.readBoolean() && !reader.readBoolean(), "boolean decoding failed");
    expect(reader.readShort() == -32768 && reader.readUnsignedShort() == 65535, "short decoding failed");
    expect(reader.readInt() == std::numeric_limits<std::int32_t>::min(), "int minimum decoding failed");
    expect(reader.readInt() == 0x01020304, "int byte order failed");
    expect(reader.readLong() == std::numeric_limits<std::int64_t>::min() && reader.readLong() == -1, "long signed decoding failed");
    expect(reader.readFloat() == 1.0F, "float decoding failed");
    const auto negativeZero = reader.readFloat();
    expect(negativeZero == 0.0F && std::signbit(negativeZero), "float negative zero lost");
    expect(reader.readDouble() == -2.5, "double decoding failed");
    reader.requireEnd();
    expectProtocolError([&] { reader.readByte(); }, "EOF accepted");

    PacketWriter nans;
    nans.writeFloat(std::numeric_limits<float>::quiet_NaN()).writeDouble(std::numeric_limits<double>::quiet_NaN());
    expect(nans.data() == Bytes({0x7f, 0xc0, 0, 0, 0x7f, 0xf8, 0, 0, 0, 0, 0, 0}), "NaNs not Java canonical bits");
    const Bytes truth = {0xff};
    PacketReader booleanReader(truth);
    expect(booleanReader.readBoolean(), "Java boolean must accept any nonzero byte");
    PacketWriter bounded(1);
    expectProtocolError([&] { bounded.writeInt(5); }, "primitive writer exceeded bound");
    expect(bounded.data().empty(), "failed primitive write changed output");
}

void stringFixtures() {
    // A, NUL, e-acute, GRINNING FACE: standard UTF-8 payload is eight bytes.
    const std::string utf8("A\0\xc3\xa9\xf0\x9f\x98\x80", 8);
    const Bytes standard = {0x00, 0x08, 0x41, 0x00, 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80};
    PacketWriter standardWriter;
    standardWriter.writeUTF(utf8);
    expect(standardWriter.data() == standard, "FastDataOutputStream UTF must be standard UTF-8");
    PacketReader standardReader(standard);
    expect(standardReader.readUTF() == utf8, "standard UTF-8 input changed NUL or supplementary character");
    standardReader.requireEnd();

    const std::u16string utf16 = {u'A', 0, 0x00e9, 0xd83d, 0xde00};
    const Bytes modified = {0x00, 0x0b, 0x41, 0xc0, 0x80, 0xc3, 0xa9, 0xed, 0xa0, 0xbd, 0xed, 0xb8, 0x80};
    PacketWriter modifiedWriter;
    modifiedWriter.writeModifiedUTF(utf16);
    expect(modifiedWriter.data() == modified, "DataOutputStream modified UTF fixture differs");
    PacketReader modifiedReader(modified);
    expect(modifiedReader.readModifiedUTF() == utf16, "modified UTF-8 UTF-16 code units changed");
    modifiedReader.requireEnd();
    const Bytes isolatedSurrogate = {0, 3, 0xed, 0xa0, 0x80};
    PacketReader isolatedReader(isolatedSurrogate);
    expect(isolatedReader.readModifiedUTF() == std::u16string(1, 0xd800), "modified UTF discarded isolated surrogate");
    const Bytes invalidUtf = {0, 2, 0xc0, 0x80};
    PacketReader invalidReader(invalidUtf);
    expectProtocolError([&] { invalidReader.readUTF(); }, "malformed standard UTF-8 accepted");
    expectProtocolError([&] { standardWriter.writeUTF(std::string(65536, 'x')); }, "oversized UTF length wrapped");
    const Bytes truncated = {0, 2, 0x41};
    PacketReader truncatedReader(truncated);
    expectProtocolError([&] { truncatedReader.readUTF(); }, "truncated UTF accepted");
}

void framingFixtures() {
    LegacyCodec plain;
    expect(plain.encode(Packet{-5, {0}}) == Bytes({0xfb, 0, 0, 4, 'A', 'A', '=', '='}), "plaintext BE24 Base64 frame differs");
    expect(plain.encode(Packet{-67, {0}}) == Bytes({0xbd, 0, 0, 4, 'A', 'A', '=', '='}), "pre-key resource frame must use BE24");
    expect(plain.encode(Packet{-74, {0, 0xff}}) == Bytes({0xb6, 0, 0, 2, 0, 0xff}), "GET_IMAGE_SOURCE must bypass outbound Base64");

    const Bytes key = {0x10, 0x20, 0x30};
    const Bytes first = {0xeb, 0x20, 0x30, 0x14, 0x61, 0x71, 0x2d, 0x1d};
    const Bytes second = {0x35, 0x10, 0x20, 0x30};
    LegacyCodec encrypted;
    encrypted.activateKey(key);
    expect(encrypted.encode(Packet{-5, {0}}) == first, "encrypted ordinary packet fixture differs");
    expect(encrypted.encode(Packet{5, {}}) == second, "outbound key cursor did not continue across packets");
    for (std::size_t i = 0; i < first.size(); ++i) {
        encrypted.feed(&first[i], 1);
        const auto decoded = encrypted.tryDecode();
        if (i + 1 < first.size()) {
            expect(!decoded, "partial packet returned early or cipher cursor advanced");
        } else {
            expect(decoded && decoded->command == -5 && decoded->payload == Bytes({0}), "inbound cursor must be independent of writes");
        }
    }
    encrypted.feed(second);
    const auto next = encrypted.tryDecode();
    expect(next && next->command == 5 && next->payload.empty(), "inbound cursor did not continue across packets");
    expect(!encrypted.tryDecode() && encrypted.bufferedBytes() == 0, "fully consumed receive buffer not empty");

    LegacyCodec resource;
    resource.activateKey(key);
    expect(resource.encode(Packet{-74, {0, 0x7f, 0xff}}) == Bytes({0xa6, 0xa3, 0xb0, 0x90, 0xa0, 0x30, 0x6f, 0xdf}), "resource 28-bit offset length or Base64 exception differs");
    LegacyCodec icon;
    icon.activateKey(key);
    expect(icon.encode(Packet{-67, {0}}) == Bytes({0xad, 0xa4, 0xb0, 0x90, 0xa0, 0x71, 0x51, 0x1d, 0x0d}), "icon must combine Base64 with special 28-bit framing");
    LegacyCodec resourceLength;
    resourceLength.activateKey(Bytes{0});
    const auto large = resourceLength.encode(Packet{-74, Bytes(0x010203, 0)});
    expect(large.size() == 0x010208 && Bytes(large.begin(), large.begin() + 5) == Bytes({0xb6, 0x83, 0x82, 0x81, 0x80}), "28-bit byte order differs");
    for (const auto command : {-32, -66, 11, -67, -87, 66, 120, -74, 59, 60}) {
        expect(LegacyCodec::isSpecialOutboundCommand(static_cast<std::int8_t>(command)), "special resource command missing");
    }
    expect(!LegacyCodec::isSpecialOutboundCommand(-27) && !LegacyCodec::isSpecialOutboundCommand(115), "ordinary command marked as resource");
}

void inboundBase64Fixtures() {
    LegacyCodec decoder;
    // Missing padding is accepted by the Java basic decoder.
    decoder.feed(Bytes{1, 0, 0, 2, 'Z', 'g', 2, 0, 0, 3, 'Z', 'm', '8'});
    auto packet = decoder.tryDecode();
    expect(packet && packet->payload == Bytes({'f'}), "unpadded two-digit Base64 rejected");
    packet = decoder.tryDecode();
    expect(packet && packet->command == 2 && packet->payload == Bytes({'f', 'o'}), "unpadded three-digit Base64 rejected");
    decoder.feed(Bytes{3, 0, 0, 3, 0, 0x7f, 0xff});
    packet = decoder.tryDecode();
    expect(packet && packet->payload == Bytes({0, 0x7f, 0xff}), "invalid Base64 did not preserve original payload");
    decoder.feed(Bytes{0xb6, 0, 0, 4, 'A', 'A', '=', '='});
    packet = decoder.tryDecode();
    expect(packet && packet->command == -74 && packet->payload == Bytes({0}), "inbound resource request incorrectly bypassed Base64 or used 28-bit length");
    decoder.feed(Bytes{4, 0, 0, 4, 'A', 'A', '=', 'X'});
    packet = decoder.tryDecode();
    expect(packet && packet->payload == Bytes({'A', 'A', '=', 'X'}), "bad Base64 padding not preserved");
    decoder.feed(Bytes{4, 0, 0, 4, 'Z', 'h', '=', '='});
    packet = decoder.tryDecode();
    expect(packet && packet->payload == Bytes({'f'}), "Java decoder accepts nonzero unused tail bits");
}

void handshakeAndBatchFixtures() {
    LegacyCodec session;
    const Bytes requestAndNext = {0xe5, 0, 0, 0, 0xeb, 0x20, 0x30, 0x14, 0x61, 0x71, 0x2d, 0x1d};
    session.feed(requestAndNext);
    const auto request = session.tryDecode();
    expect(request && request->command == -27 && request->payload.empty() && !session.connected(), "handshake request parsing failed");
    session.activateKey(Bytes{0x10, 0x20, 0x30});
    const auto next = session.tryDecode();
    expect(next && next->command == -5 && next->payload == Bytes({0}), "coalesced packet lost key transition");

    LegacyCodec server;
    const auto handshake = server.encodeHandshake(Bytes{0x2a}, "", 1, false, 2);
    // Decoded payload: 01 2a 0000 00000001 00 00000002.
    const Bytes handshakeFixture = {0xe5, 0, 0, 20,
        'A', 'S', 'o', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'E', 'A', 'A', 'A', 'A', 'A', 'A', 'g', '=', '='};
    expect(handshake == handshakeFixture && server.connected(), "handshake fields/Base64/plaintext frame differ");
    expect(server.encode(Packet{5, {}}) == Bytes({0x2f, 0x2a, 0x2a, 0x2a}), "handshake incorrectly consumed XOR cursor");
    expectProtocolError([&] { server.encodeHandshake(Bytes{0x2a}, "", 1, false, 2); }, "duplicate handshake accepted");
    LegacyCodec multi;
    const auto multiHandshake = multi.encodeHandshake(Bytes{1, 2, 3}, "", 1, true, 2);
    const Bytes multiFixture = {0xe5, 0, 0, 20,
        'A', 'w', 'E', 'D', 'A', 'Q', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'Q', 'E', 'A', 'A', 'A', 'A', 'C'};
    expect(multiHandshake == multiFixture, "handshake adjacent key XOR chain differs");

    LegacyCodec batchCodec;
    const auto batch = batchCodec.makeBatch({Packet{-5, {0, 0xff}}, Packet{-74, {'A'}}});
    const Bytes batchFixture = {0, 2, 0xfb, 0, 0, 0, 2, 0, 0xff, 0xb6, 0, 0, 0, 1, 'A'};
    expect(batch.command == 115 && batch.payload == batchFixture, "batch must contain logical child payloads");
    const Bytes batchFrame = {0x73, 0, 0, 20,
        'A', 'A', 'L', '7', 'A', 'A', 'A', 'A', 'A', 'g', 'D', '/', 't', 'g', 'A', 'A', 'A', 'A', 'F', 'B'};
    expect(batchCodec.encode(batch) == batchFrame, "batch must apply Base64 once around complete batch");
    const auto children = batchCodec.unpackBatch(Packet{115, batchFixture});
    expect(children.size() == 2 && children[0].command == -5 && children[0].payload == Bytes({0, 0xff}) &&
           children[1].command == -74 && children[1].payload == Bytes({'A'}), "batch child decoding failed");
    expectProtocolError([&] { batchCodec.unpackBatch(Packet{115, {0, 1, 0, 0xff, 0xff, 0xff, 0xff}}); }, "negative batch length accepted");
    expectProtocolError([&] { batchCodec.unpackBatch(Packet{115, {0, 1, 0, 0, 0, 0, 4, 1}}); }, "truncated batch child accepted");
    expectProtocolError([&] { batchCodec.makeBatch(std::vector<Packet>(51)); }, "batch exceeded Java sender count");
}

void limitsAndFailureFixtures() {
    CodecLimits limits;
    limits.maxInboundEncodedBytes = 4;
    limits.maxOutboundEncodedBytes = 4;
    limits.maxBufferedBytes = 8;
    LegacyCodec bounded(limits);
    bounded.feed(Bytes{1, 0, 0, 5});
    expectProtocolError([&] { bounded.tryDecode(); }, "oversized length accepted before allocation");
    expect(bounded.failed(), "invalid stream not marked failed");
    expectProtocolError([&] { bounded.feed(Bytes{}); }, "failed stream remained usable");
    bounded.reset();
    expect(!bounded.failed() && !bounded.connected() && bounded.bufferedBytes() == 0, "reset failed to clear session state");
    bounded.feed(Bytes{1, 0, 0, 4, 'A', 'A', '=', '='});
    const auto maximumPacket = bounded.tryDecode();
    expect(maximumPacket && maximumPacket->payload == Bytes({0}), "maximum valid bounded packet rejected");
    expectProtocolError([&] { bounded.encode(Packet{1, {1, 2, 3, 4}}); }, "Base64 expansion exceeded outbound bound");
    expect(!bounded.failed(), "local outbound error poisoned inbound stream");
    expectProtocolError([&] { bounded.feed(Bytes(9)); }, "aggregate receive buffer exceeded limit");
    LegacyCodec key;
    expectProtocolError([&] { key.activateKey(Bytes{}); }, "zero-length key accepted");
    expectProtocolError([&] { key.activateKey(Bytes(128)); }, "Java signed-cursor overflow key accepted");
    const Bytes shortInt = {1, 2, 3};
    PacketReader reader(shortInt);
    expectProtocolError([&] { reader.readInt(); }, "truncated primitive accepted");
    expect(reader.position() == 0, "failed primitive consumed bytes");
    expectProtocolError([&] { reader.readBytes(std::numeric_limits<std::size_t>::max()); }, "byte count overflow accepted");
}
} // namespace

void runLegacyCodecTests() {
    primitiveFixtures();
    stringFixtures();
    framingFixtures();
    inboundBase64Fixtures();
    handshakeAndBatchFixtures();
    limitsAndFailureFixtures();
}
