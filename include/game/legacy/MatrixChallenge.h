#pragma once

#include "game/legacy/Packet.h"

#include <array>

namespace game::legacy {

inline constexpr std::int8_t matrixChallengeCommand = 120;
inline constexpr std::size_t matrixChallengeSize = 5;
using Matrix5 = std::array<std::array<std::uint64_t, matrixChallengeSize>, matrixChallengeSize>;

struct MatrixProfile {
    std::uint64_t modulus = 4294967291ULL;
    // ConfigStudio.SERVER_VERSION in the supplied source; production PC
    // settings can override this through SecurityRepository.
    std::string version = "0.0.7";
};

struct MatrixChallengeConfig {
    MatrixProfile pc;
    MatrixProfile mobile;
};

enum class MatrixVerification { accepted, rejected, alreadyVerified };

// Exact 48-bit recurrence used by PCRandom/MobileRandom. Java's normal Random
// constructor additionally scrambles its initial seed; secret generation does
// NOT. The two constructors are named to keep that distinction explicit.
class MatrixRandom {
public:
    static MatrixRandom rawSeed(std::uint64_t seed);
    static MatrixRandom javaSeed(std::uint64_t seed);
    std::uint32_t nextUnsignedInt();

private:
    explicit MatrixRandom(std::uint64_t state) : state_(state) {}
    std::uint64_t state_;
};

// One instance per connection, with immutable profile snapshots. Account auth,
// ECC, transport key state, and client-info acceptance belong to the host.
class MatrixChallenge {
public:
    explicit MatrixChallenge(MatrixChallengeConfig config = {});
    const MatrixProfile& profile(int device) const noexcept;
    bool acceptsVersion(int device, std::string_view version) const noexcept;
    // The host supplies entropy as a java.util.Random constructor seed. Passing
    // a fixed seed is useful for differential replay, not live session policy.
    Packet issue(int device, std::uint64_t randomSeed);
    MatrixVerification verifyResponse(const Packet& response);
    bool issued() const noexcept { return issued_; }
    bool verified() const noexcept { return verified_; }
    int device() const noexcept { return device_; }
    const Matrix5& challenge() const;
    void reset() noexcept;

    // Pure algorithms for differential tests and client-independent adapters.
    static Matrix5 generateSecret(std::uint64_t modulus);
    static Matrix5 generateChallenge(std::uint64_t modulus, std::uint64_t randomSeed);
    static Matrix5 multiply(const Matrix5& left, const Matrix5& right, std::uint64_t modulus);
    static Matrix5 computeResponse(const Matrix5& secret, const Matrix5& challenge, std::uint64_t modulus);
    static Packet challengePacket(const Matrix5& matrix);
    static Matrix5 readResponse(const Packet& packet);

private:
    MatrixChallengeConfig config_;
    Matrix5 challenge_{};
    Matrix5 expected_{};
    int device_{};
    bool issued_{};
    bool verified_{};
};

} // namespace game::legacy
