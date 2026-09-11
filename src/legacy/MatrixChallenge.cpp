#include "game/legacy/MatrixChallenge.h"

#include <limits>
#include <utility>

namespace game::legacy {
namespace {
constexpr std::uint64_t randomMultiplier = 0x5deece66dULL;
constexpr std::uint64_t randomAddend = 0xbULL;
constexpr std::uint64_t randomMask = (1ULL << 48U) - 1;
constexpr std::uint64_t legacySecretSeed = 0x5eedfaceULL;

void validateModulus(std::uint64_t modulus) {
    if (modulus < 2 || modulus > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw ProtocolError("matrix modulus must be 2..INT64_MAX");
    }
}

void validateMatrix(const Matrix5& matrix, std::uint64_t modulus) {
    for (const auto& row : matrix) {
        for (const auto value : row) {
            if (value >= modulus) throw ProtocolError("matrix entry outside configured modulus");
        }
    }
}

// Both operands are already reduced. Avoid overflowing even if a profile
// chooses a modulus near INT64_MAX (Java uses BigInteger here).
std::uint64_t addMod(std::uint64_t left, std::uint64_t right, std::uint64_t modulus) {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

std::uint64_t multiplyMod(std::uint64_t left, std::uint64_t right, std::uint64_t modulus) {
    if (left == 0 || right == 0) return 0;
    // With the original 32-bit prime, every product fits a uint64_t.
    if (left <= std::numeric_limits<std::uint64_t>::max() / right) return left * right % modulus;
    std::uint64_t result = 0;
    while (right != 0) {
        if ((right & 1U) != 0) result = addMod(result, left, modulus);
        right >>= 1U;
        if (right != 0) left = addMod(left, left, modulus);
    }
    return result;
}

std::int32_t signedWireInt(std::uint32_t value) {
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value)
                              : -1 - static_cast<std::int32_t>(~value);
}

} // namespace

MatrixRandom MatrixRandom::rawSeed(std::uint64_t seed) { return MatrixRandom(seed & randomMask); }
MatrixRandom MatrixRandom::javaSeed(std::uint64_t seed) { return rawSeed(seed ^ randomMultiplier); }

std::uint32_t MatrixRandom::nextUnsignedInt() {
    // Unsigned overflow preserves the low 48 bits of Java's wrapped long
    // multiplication without C++ signed-overflow undefined behavior.
    state_ = (state_ * randomMultiplier + randomAddend) & randomMask;
    return static_cast<std::uint32_t>(state_ >> 16U);
}

MatrixChallenge::MatrixChallenge(MatrixChallengeConfig config) : config_(std::move(config)) {
    validateModulus(config_.pc.modulus);
    validateModulus(config_.mobile.modulus);
    if (config_.pc.version.size() > 65535 || config_.mobile.version.size() > 65535) {
        throw ProtocolError("matrix client version exceeds protocol string limit");
    }
}

const MatrixProfile& MatrixChallenge::profile(int device) const noexcept {
    // Matches MatrixChallengeManager.isPC: only zero is PC, all other device
    // bytes take the mobile route. Host policy may restrict accepted devices.
    return device == 0 ? config_.pc : config_.mobile;
}

bool MatrixChallenge::acceptsVersion(int device, std::string_view version) const noexcept {
    return profile(device).version == version;
}

Matrix5 MatrixChallenge::generateSecret(std::uint64_t modulus) {
    validateModulus(modulus);
    auto random = MatrixRandom::rawSeed(legacySecretSeed);
    Matrix5 secret{};
    for (auto& row : secret) {
        for (auto& value : row) {
            const auto high = static_cast<std::uint64_t>(random.nextUnsignedInt());
            const auto low = static_cast<std::uint64_t>(random.nextUnsignedInt());
            // Equivalent to correcting a negative Java long by adding 2^64
            // through BigInteger before remainder(modulus).
            value = ((high << 32U) | low) % modulus;
        }
    }
    return secret;
}

Matrix5 MatrixChallenge::generateChallenge(std::uint64_t modulus, std::uint64_t randomSeed) {
    validateModulus(modulus);
    auto random = MatrixRandom::javaSeed(randomSeed);
    Matrix5 challenge{};
    for (auto& row : challenge) {
        for (auto& value : row) value = static_cast<std::uint64_t>(random.nextUnsignedInt()) % modulus;
    }
    return challenge;
}

Matrix5 MatrixChallenge::multiply(const Matrix5& left, const Matrix5& right, std::uint64_t modulus) {
    validateModulus(modulus);
    validateMatrix(left, modulus);
    validateMatrix(right, modulus);
    Matrix5 result{};
    // Preserve the source i/k/j loop and reduction after every product/add.
    for (std::size_t i = 0; i < matrixChallengeSize; ++i) {
        for (std::size_t k = 0; k < matrixChallengeSize; ++k) {
            for (std::size_t j = 0; j < matrixChallengeSize; ++j) {
                const auto product = multiplyMod(left[i][k], right[k][j], modulus);
                result[i][j] = addMod(result[i][j], product, modulus);
            }
        }
    }
    return result;
}

Matrix5 MatrixChallenge::computeResponse(const Matrix5& secret, const Matrix5& challenge, std::uint64_t modulus) {
    return multiply(multiply(secret, challenge, modulus), secret, modulus);
}

Packet MatrixChallenge::challengePacket(const Matrix5& matrix) {
    PacketWriter writer(4 + matrixChallengeSize * matrixChallengeSize * 4);
    writer.writeInt(static_cast<std::int32_t>(matrixChallengeSize));
    for (const auto& row : matrix) {
        for (const auto value : row) {
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                throw ProtocolError("matrix challenge entry does not fit unsigned 32-bit wire value");
            }
            writer.writeInt(signedWireInt(static_cast<std::uint32_t>(value)));
        }
    }
    return writer.packet(matrixChallengeCommand);
}

Matrix5 MatrixChallenge::readResponse(const Packet& packet) {
    if (packet.command != matrixChallengeCommand || packet.payload.size() != matrixChallengeSize * matrixChallengeSize * 8) {
        throw ProtocolError("matrix response must be command 120 with 25 high/low int pairs");
    }
    PacketReader reader(packet.payload);
    Matrix5 response{};
    for (auto& row : response) {
        for (auto& value : row) {
            // readLong has the same BE bit layout as the Java reader's two
            // unsigned int halves. Preserve the high sign bit for comparison.
            value = static_cast<std::uint64_t>(reader.readLong());
        }
    }
    reader.requireEnd();
    return response;
}

Packet MatrixChallenge::issue(int device, std::uint64_t randomSeed) {
    const auto& selected = profile(device);
    auto challenge = generateChallenge(selected.modulus, randomSeed);
    auto expected = computeResponse(generateSecret(selected.modulus), challenge, selected.modulus);
    auto packet = challengePacket(challenge);
    challenge_ = challenge;
    expected_ = expected;
    device_ = device;
    issued_ = true;
    verified_ = false;
    return packet;
}

MatrixVerification MatrixChallenge::verifyResponse(const Packet& packet) {
    if (verified_) return MatrixVerification::alreadyVerified;
    if (!issued_) throw ProtocolError("matrix response arrived before a challenge");
    const auto response = readResponse(packet);
    std::uint64_t difference = 0;
    // Compare every entry as the Java implementation does, without stopping
    // at the first mismatch. Out-of-range or negative Java longs cannot match.
    for (std::size_t i = 0; i < matrixChallengeSize; ++i) {
        for (std::size_t j = 0; j < matrixChallengeSize; ++j) difference |= expected_[i][j] ^ response[i][j];
    }
    verified_ = difference == 0;
    return verified_ ? MatrixVerification::accepted : MatrixVerification::rejected;
}

const Matrix5& MatrixChallenge::challenge() const {
    if (!issued_) throw ProtocolError("matrix challenge has not been issued");
    return challenge_;
}

void MatrixChallenge::reset() noexcept {
    challenge_ = {};
    expected_ = {};
    device_ = 0;
    issued_ = false;
    verified_ = false;
}

} // namespace game::legacy
