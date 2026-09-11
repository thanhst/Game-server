#pragma once

#include "game/legacy/Packet.h"

#include <memory>

namespace game::legacy {

inline constexpr std::int8_t eccChallengeCommand = 121; // Cmd.SHOW_ADS

// Legacy ECCAuth/SimpleECC adapter using OpenSSL's P-256 implementation.
// This is independent of account authentication and the matrix login gate.
class EccChallenge {
public:
    EccChallenge();
    ~EccChallenge();
    EccChallenge(EccChallenge&&) noexcept;
    EccChallenge& operator=(EccChallenge&&) noexcept;
    EccChallenge(const EccChallenge&) = delete;
    EccChallenge& operator=(const EccChallenge&) = delete;

    // Generates 256 random bits and reduces modulo the FIELD prime, matching
    // ECCAuth's constructor; it is not a conventional ECDSA key generator.
    Packet issue();
    // Deterministic fixture/replay entry point, not live entropy generation.
    Packet issueWithSecret(const Bytes& unsignedBigEndianSecret);
    // Reads four BigIntIO unsigned magnitudes: Rx, Ry, Sx, Sy. Records only a
    // diagnostic outcome: Java finishUpdate has ECC enforcement commented out.
    // Malformed input returns false rather than granting authentication.
    bool verifyResponse(const Bytes& logicalPayload);
    bool issued() const noexcept;
    bool verified() const noexcept;
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::legacy
