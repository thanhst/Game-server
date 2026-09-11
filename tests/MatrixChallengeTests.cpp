#include "game/legacy/MatrixChallenge.h"

#include <limits>
#include <stdexcept>

namespace {
using namespace game::legacy;

void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Action>
void rejected(Action action, const char* message) {
    try { action(); } catch (const ProtocolError&) { return; }
    throw std::runtime_error(message);
}

Matrix5 identity() {
    Matrix5 result{};
    for (std::size_t i = 0; i < 5; ++i) result[i][i] = 1;
    return result;
}

Packet responsePacket(const Matrix5& matrix) {
    PacketWriter writer;
    for (const auto& row : matrix) {
        for (const auto value : row) writer.writeLong(static_cast<std::int64_t>(value));
    }
    return writer.packet(matrixChallengeCommand);
}
} // namespace

void runMatrixChallengeTests() {
    // Hand-derived LCG fixtures. Raw seed zero advances to 0xB, then
    // 0x40942DE6BA; JDK Random seed zero first advances to 0xBB20B4600A74.
    auto raw = MatrixRandom::rawSeed(0);
    expect(raw.nextUnsignedInt() == 0 && raw.nextUnsignedInt() == 0x0040942dU, "raw matrix LCG fixture differs");
    auto java = MatrixRandom::javaSeed(0);
    expect(java.nextUnsignedInt() == 0xbb20b460U && java.nextUnsignedInt() == 0xd4d95138U, "java.util.Random initial seed scrambling differs");
    // The hardcoded game's raw seed advances to 1E5A1490DDC1 then
    // C573B4D5D138, whose two upper 32-bit words form the first secret cell.
    auto secretRandom = MatrixRandom::rawSeed(0x5eedfaceULL);
    expect(secretRandom.nextUnsignedInt() == 0x1e5a1490U && secretRandom.nextUnsignedInt() == 0xc573b4d5U, "legacy secret PRNG must not use JDK initial scrambling");
    const auto secret = MatrixChallenge::generateSecret(4294967291ULL);
    // Since 2^32 = 5 mod (2^32-5), the first cell is
    // (5*0x1E5A1490 + 0xC573B4D5) mod 4294967291 = 0x5D361BAA.
    expect(secret[0][0] == 0x5d361baaULL, "unsigned 64-bit secret reduction fixture differs");
    const auto challenge = MatrixChallenge::generateChallenge(4294967291ULL, 0);
    expect(challenge[0][0] == 0xbb20b460ULL && challenge[0][1] == 0xd4d95138ULL, "challenge must use unsigned Random.nextInt values");

    Matrix5 diagonal{};
    for (std::size_t i = 0; i < 5; ++i) diagonal[i][i] = i + 1;
    Matrix5 knownChallenge{};
    for (auto& row : knownChallenge) row.fill(6);
    Matrix5 expected{};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t j = 0; j < 5; ++j) expected[i][j] = (i + 1) * 6 * (j + 1) % 97;
    }
    expect(MatrixChallenge::computeResponse(diagonal, knownChallenge, 97) == expected, "matrix response order must be S*C*S");
    expect(MatrixChallenge::multiply(identity(), knownChallenge, 97) == knownChallenge, "matrix identity multiplication failed");
    const auto modulus = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    Matrix5 almostModulus{};
    for (auto& row : almostModulus) row.fill(modulus - 1);
    const auto overflowResult = MatrixChallenge::multiply(almostModulus, almostModulus, modulus);
    for (const auto& row : overflowResult) {
        for (const auto value : row) expect(value == 5, "portable modular product/sum overflowed near INT64_MAX");
    }

    Matrix5 wire{};
    wire[0][0] = 0xffffffffULL;
    wire[0][1] = 0x01020304ULL;
    auto outgoing = MatrixChallenge::challengePacket(wire);
    expect(outgoing.command == 120 && outgoing.payload.size() == 104, "matrix challenge command or fixed size differs");
    const Bytes prefix = {0, 0, 0, 5, 0xff, 0xff, 0xff, 0xff, 1, 2, 3, 4};
    expect(Bytes(outgoing.payload.begin(), outgoing.payload.begin() + 12) == prefix, "matrix challenge size/int byte order differs");
    Bytes responseBytes(200);
    responseBytes[0] = 0x80;
    responseBytes[7] = 1;
    responseBytes[8] = 1;
    responseBytes[9] = 2;
    responseBytes[10] = 3;
    responseBytes[11] = 4;
    responseBytes[12] = 5;
    responseBytes[13] = 6;
    responseBytes[14] = 7;
    responseBytes[15] = 8;
    const auto incoming = MatrixChallenge::readResponse(Packet{120, responseBytes});
    expect(incoming[0][0] == 0x8000000000000001ULL && incoming[0][1] == 0x0102030405060708ULL, "matrix high/low halves not preserved");
    rejected([&] { MatrixChallenge::readResponse(Packet{120, Bytes(204)}); }, "response incorrectly accepted size prefix/trailing bytes");
    rejected([&] { MatrixChallenge::readResponse(Packet{120, Bytes(199)}); }, "truncated matrix response accepted");

    MatrixChallengeConfig config;
    config.pc.version = "pc-version";
    config.mobile.version = "mobile-version";
    config.mobile.modulus = 97;
    MatrixChallenge session(config);
    expect(session.acceptsVersion(0, "pc-version") && !session.acceptsVersion(0, "mobile-version"), "PC version profile mismatch");
    expect(session.acceptsVersion(1, "mobile-version") && session.acceptsVersion(2, "mobile-version") &&
           session.acceptsVersion(-1, "mobile-version"), "source nonzero-device mobile routing differs");
    rejected([&] { session.verifyResponse(Packet{120, Bytes(200)}); }, "matrix response accepted before issue");
    const auto pcPacket = session.issue(0, 0);
    expect(pcPacket.command == 120 && session.issued() && !session.verified(), "matrix issue state invalid");
    expect(session.verifyResponse(Packet{120, Bytes(200)}) == MatrixVerification::rejected && !session.verified(), "invalid matrix bypassed login gate");
    const auto answer = MatrixChallenge::computeResponse(secret, session.challenge(), config.pc.modulus);
    expect(session.verifyResponse(responsePacket(answer)) == MatrixVerification::accepted && session.verified(), "valid matrix answer rejected after retry");
    expect(session.verifyResponse(Packet{120, {}}) == MatrixVerification::alreadyVerified, "verified duplicate must be ignored without parsing");
    session.issue(1, 0);
    expect(!session.verified() && session.device() == 1, "reissued challenge did not reset verification");
    const auto mobileAnswer = MatrixChallenge::computeResponse(MatrixChallenge::generateSecret(97), session.challenge(), 97);
    expect(session.verifyResponse(responsePacket(mobileAnswer)) == MatrixVerification::accepted, "configured mobile matrix response rejected");
    session.reset();
    expect(!session.issued() && !session.verified(), "matrix reset leaked authentication state");
    rejected([&] { session.challenge(); }, "unissued matrix exposed");
    rejected([&] { MatrixChallenge::generateSecret(0); }, "zero matrix modulus accepted");
    rejected([&] { MatrixChallenge::generateSecret(1); }, "degenerate matrix modulus accepted");
    rejected([&] { MatrixChallenge::generateSecret(1ULL << 63U); }, "modulus outside positive Java long accepted");
}
