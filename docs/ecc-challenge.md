# Legacy ECC challenge

`EccChallenge` ports `Session.sendECCChallenge`, `ECCAuth`, `SimpleECC`, and
`BigIntIO` onto OpenSSL's P-256 point operations. It keeps a private scalar per
connection and is independent of matrix, account, and player state.

After the first successful matrix response, the Java server sends command 121
(`SHOW_ADS`). Its logical payload contains seven unsigned integers, in this order:
field prime `p`, curve coefficients `a` and `b`, generator `Gx` and `Gy`, and
challenge `Cx` and `Cy`. Each integer uses a big-endian signed 32-bit byte count
followed by minimal unsigned big-endian magnitude bytes. Zero uses count zero;
there is no Java sign-padding byte. The running C++ application passes these
logical bytes through `BinaryPacketCodec` and ordinary ServerEngine BE32 TCP
framing. The Java Base64/BE24/XOR wrapper remains only in the offline reference
`LegacyCodec`.

The scalar generation follows this game's source exactly: obtain 256 random
bits, reduce modulo the **field prime**, then calculate `C = secret * G`. This is
not ECDSA, and the modulus is not the curve's group order. `issue()` uses OpenSSL
private random generation; `issueWithSecret()` is a deterministic fixture/replay
entry point. Scalars reducing to zero produce the Java infinity representation
with `Cx = Cy = 0`, serialized as zero-length magnitudes.

`Session.finishUpdate` reads four `BigIntIO` values: `Rx`, `Ry`, `Sx`, and `Sy`.
It checks that R and S are on the curve and that `S = secret * R`. The C++
`verifyResponse` implements that check and records `verified()`. In the supplied
Java source the subsequent enforcement condition is commented out: failed ECC
verification does **not** prevent `loadChar`/`enter`. Host code must preserve that
distinction when implementing legacy behavior rather than treating ECC as the
matrix login gate. Matrix verification is separately required by `login`.

The C++ reader preserves Java's handling of nonpositive BigIntIO lengths as zero,
and permits padded positive magnitudes up to 64 bytes. It rejects larger encodings,
noncanonical coordinates outside `[0,p)`, and trailing response bytes. Java's
unbounded allocations and acceptance of arbitrarily large modulo-equivalent
coordinates are not reproduced. These are malformed-input differences, not
changes to the canonical point format emitted by `BigIntIO.writeBigInt`.

OpenSSL owns the group and point arithmetic; private BIGNUM values are cleared
on destruction/reset. The module uses BN/EC_GROUP/EC_POINT APIs, without deprecated
EC_KEY construction. Link the target to `OpenSSL::Crypto`.

`tests/EccChallengeTests.cpp` hardcodes the five curve constants from
`Session.sendECCChallenge`. Scalar-one, scalar-two mismatch, prime reduction,
infinity, BigIntIO framing, malformed packets, padding, move/reset state, and
off-curve cases are provided. These are unexecuted test sources: no build, test
run, Java execution, or real-client exchange was performed.
