#include "game/legacy/EccChallenge.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <limits>
#include <utility>

namespace game::legacy {
namespace {
using BigNumber = std::unique_ptr<BIGNUM, decltype(&BN_clear_free)>;
using Context = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
using Group = std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)>;
using Point = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;

BigNumber newNumber() {
    BigNumber number(BN_new(), BN_clear_free);
    if (!number) throw ProtocolError("could not allocate ECC number");
    return number;
}

Point newPoint(const EC_GROUP* group) {
    Point point(EC_POINT_new(group), EC_POINT_free);
    if (!point) throw ProtocolError("could not allocate ECC point");
    return point;
}

void checked(int success, const char* message) {
    if (success != 1) throw ProtocolError(message);
}

void writeBigInt(PacketWriter& writer, const BIGNUM* number) {
    if (BN_is_negative(number)) throw ProtocolError("negative outbound ECC magnitude");
    const auto count = BN_num_bytes(number);
    writer.writeInt(count);
    if (count != 0) {
        Bytes bytes(static_cast<std::size_t>(count));
        if (BN_bn2bin(number, bytes.data()) != count) throw ProtocolError("could not encode ECC magnitude");
        writer.writeBytes(bytes);
    }
}

BigNumber readBigInt(PacketReader& reader) {
    auto result = newNumber();
    const auto count = reader.readInt();
    // BigIntIO.readBigInt returns zero for every nonpositive signed length.
    if (count <= 0) return result;
    // A P-256 coordinate uses <=32 magnitude bytes. Permit leading padding,
    // but do not copy an unbounded BigInteger allocation from Java input.
    if (count > 64) throw ProtocolError("ECC coordinate encoding exceeds 64 bytes");
    const auto bytes = reader.readBytes(static_cast<std::size_t>(count));
    if (!BN_bin2bn(bytes.data(), count, result.get())) throw ProtocolError("could not decode ECC magnitude");
    return result;
}

bool setCanonicalPoint(const EC_GROUP* group, EC_POINT* point, const BIGNUM* x, const BIGNUM* y,
                       const BIGNUM* prime, BN_CTX* context) {
    if (BN_is_negative(x) || BN_is_negative(y) || BN_cmp(x, prime) >= 0 || BN_cmp(y, prime) >= 0) return false;
    if (EC_POINT_set_affine_coordinates(group, point, x, y, context) != 1) return false;
    return EC_POINT_is_at_infinity(group, point) == 0 && EC_POINT_is_on_curve(group, point, context) == 1;
}
} // namespace

struct EccChallenge::Impl {
    Group group{EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1), EC_GROUP_free};
    Context context{BN_CTX_new(), BN_CTX_free};
    BigNumber prime = newNumber();
    BigNumber a = newNumber();
    BigNumber b = newNumber();
    BigNumber secret = newNumber();
    bool active = false;
    bool valid = false;

    Impl() {
        if (!group || !context) throw ProtocolError("could not initialize legacy ECC P-256");
        checked(EC_GROUP_get_curve(group.get(), prime.get(), a.get(), b.get(), context.get()), "could not read P-256 curve parameters");
    }

    Packet issue(const BIGNUM* randomValue) {
        auto reduced = newNumber();
        checked(BN_nnmod(reduced.get(), randomValue, prime.get(), context.get()), "could not reduce legacy ECC secret modulo field prime");
        auto challenge = newPoint(group.get());
        checked(EC_POINT_mul(group.get(), challenge.get(), reduced.get(), nullptr, nullptr, context.get()), "could not generate legacy ECC challenge point");
        auto gx = newNumber();
        auto gy = newNumber();
        auto cx = newNumber();
        auto cy = newNumber();
        checked(EC_POINT_get_affine_coordinates(group.get(), EC_GROUP_get0_generator(group.get()), gx.get(), gy.get(), context.get()), "could not read P-256 generator point");
        if (EC_POINT_is_at_infinity(group.get(), challenge.get()) != 1) {
            checked(EC_POINT_get_affine_coordinates(group.get(), challenge.get(), cx.get(), cy.get(), context.get()), "could not read ECC challenge point");
        }
        // SimpleECC.Point() represents infinity with x=y=0. A zero scalar or
        // group-order multiple therefore emits zero-length Cx and Cy fields.
        PacketWriter writer(7 * (4 + 32));
        writeBigInt(writer, prime.get());
        writeBigInt(writer, a.get());
        writeBigInt(writer, b.get());
        writeBigInt(writer, gx.get());
        writeBigInt(writer, gy.get());
        writeBigInt(writer, cx.get());
        writeBigInt(writer, cy.get());
        auto packet = writer.packet(eccChallengeCommand);
        if (!BN_copy(secret.get(), reduced.get())) throw ProtocolError("could not retain ECC session secret");
        BN_set_flags(secret.get(), BN_FLG_CONSTTIME);
        active = true;
        valid = false;
        return packet;
    }

    bool verify(const Bytes& bytes) {
        valid = false;
        if (!active) return false;
        PacketReader reader(bytes);
        auto rx = readBigInt(reader);
        auto ry = readBigInt(reader);
        auto sx = readBigInt(reader);
        auto sy = readBigInt(reader);
        reader.requireEnd();
        auto r = newPoint(group.get());
        auto s = newPoint(group.get());
        if (!setCanonicalPoint(group.get(), r.get(), rx.get(), ry.get(), prime.get(), context.get()) ||
            !setCanonicalPoint(group.get(), s.get(), sx.get(), sy.get(), prime.get(), context.get())) return false;
        auto expected = newPoint(group.get());
        checked(EC_POINT_mul(group.get(), expected.get(), nullptr, r.get(), secret.get(), context.get()), "could not verify legacy ECC response");
        valid = EC_POINT_cmp(group.get(), expected.get(), s.get(), context.get()) == 0;
        return valid;
    }
};

EccChallenge::EccChallenge() : impl_(std::make_unique<Impl>()) {}
EccChallenge::~EccChallenge() = default;
EccChallenge::EccChallenge(EccChallenge&&) noexcept = default;
EccChallenge& EccChallenge::operator=(EccChallenge&&) noexcept = default;

Packet EccChallenge::issue() {
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto random = newNumber();
    checked(BN_priv_rand(random.get(), 256, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY), "could not obtain ECC challenge entropy");
    return impl_->issue(random.get());
}

Packet EccChallenge::issueWithSecret(const Bytes& unsignedBigEndianSecret) {
    if (unsignedBigEndianSecret.size() > 32) throw ProtocolError("ECC fixture secret exceeds Java 256-bit source");
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto random = newNumber();
    if (!unsignedBigEndianSecret.empty() &&
        !BN_bin2bn(unsignedBigEndianSecret.data(), static_cast<int>(unsignedBigEndianSecret.size()), random.get())) {
        throw ProtocolError("could not read ECC fixture secret");
    }
    return impl_->issue(random.get());
}

bool EccChallenge::verifyResponse(const Bytes& logicalPayload) {
    if (!impl_) return false;
    try {
        return impl_->verify(logicalPayload);
    } catch (const ProtocolError&) {
        impl_->valid = false;
        return false;
    }
}

bool EccChallenge::issued() const noexcept { return impl_ && impl_->active; }
bool EccChallenge::verified() const noexcept { return impl_ && impl_->valid; }

void EccChallenge::reset() noexcept {
    if (!impl_) return;
    BN_clear(impl_->secret.get());
    impl_->active = false;
    impl_->valid = false;
}

} // namespace game::legacy
