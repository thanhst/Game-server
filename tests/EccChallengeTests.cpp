#include "game/legacy/EccChallenge.h"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace game::legacy;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

Bytes hex(std::string_view text) {
    auto digit = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::runtime_error("invalid test hex digit");
    };
    if (text.size() % 2 != 0) throw std::runtime_error("odd test hex length");
    Bytes bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) bytes.push_back(static_cast<std::uint8_t>(digit(text[i]) * 16U + digit(text[i + 1])));
    return bytes;
}

Bytes bigInts(const std::vector<Bytes>& values) {
    Bytes bytes;
    for (const auto& value : values) {
        // Independent fixture assembly, not PacketWriter/production BigIntIO.
        bytes.insert(bytes.end(), {0, 0, 0, static_cast<std::uint8_t>(value.size())});
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    return bytes;
}
} // namespace

void runEccChallengeTests() {
    // All five constants are copied from Session.sendECCChallenge, not from
    // OpenSSL outputs. Scalar 1 must produce the same generator coordinates.
    const auto p = hex("FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF");
    const auto a = hex("FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC");
    const auto b = hex("5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B");
    const auto gx = hex("6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296");
    const auto gy = hex("4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5");
    EccChallenge ecc;
    const auto response = bigInts({gx, gy, gx, gy});
    expect(!ecc.issued() && !ecc.verified() && !ecc.verifyResponse(response), "ECC response accepted before challenge");
    const auto packet = ecc.issueWithSecret(Bytes{1});
    expect(packet.command == 121 && packet.payload == bigInts({p, a, b, gx, gy, gx, gy}), "P-256/BigIntIO seven-field challenge differs from Java");
    expect(ecc.issued() && !ecc.verified(), "ECC issue state invalid");
    expect(ecc.verifyResponse(response) && ecc.verified(), "S=1*R scalar-one fixture rejected");

    Bytes paddedGx = gx;
    Bytes paddedGy = gy;
    paddedGx.insert(paddedGx.begin(), 0);
    paddedGy.insert(paddedGy.begin(), 0);
    expect(ecc.verifyResponse(bigInts({paddedGx, paddedGy, paddedGx, paddedGy})), "unsigned BigIntIO leading padding rejected");
    expect(!ecc.verifyResponse(bigInts({{}, {}, {}, {}})) && !ecc.verified(), "off-curve zero coordinates accepted");
    auto trailing = response;
    trailing.push_back(0);
    expect(!ecc.verifyResponse(trailing), "ECC trailing bytes accepted");
    auto truncated = response;
    truncated.pop_back();
    expect(!ecc.verifyResponse(truncated), "truncated ECC coordinate accepted");
    expect(!ecc.verifyResponse(Bytes{0, 0, 0, 65}), "unbounded ECC coordinate length accepted");
    expect(!ecc.verifyResponse(Bytes(16, 0xff)), "negative BigIntIO lengths bypassed point validation");

    ecc.issueWithSecret(Bytes{2});
    expect(!ecc.verifyResponse(response), "ECC accepted on-curve points without checking S=secret*R");
    const auto reduced = ecc.issueWithSecret(p);
    expect(reduced.payload == bigInts({p, a, b, gx, gy, {}, {}}), "ECC secret must reduce modulo field prime, not group order");
    const auto zero = ecc.issueWithSecret({});
    expect(zero.payload == reduced.payload, "SimpleECC infinity must serialize zero-length coordinates");
    expect(!ecc.verifyResponse(response), "zero-scalar infinity accepted finite S");

    ecc.issueWithSecret(Bytes{1});
    EccChallenge moved(std::move(ecc));
    expect(!ecc.issued() && moved.issued() && moved.verifyResponse(response), "ECC move lost connection state");
    moved.reset();
    expect(!moved.issued() && !moved.verified() && !moved.verifyResponse(response), "ECC reset retained verification state");
}
