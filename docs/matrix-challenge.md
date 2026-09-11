# Legacy matrix challenge

`MatrixChallenge` ports the supplied `MatrixChallengePC`,
`MatrixChallengeMobile`, `MatrixChallengeManager`, and the matrix portions of
`Session.sendMatrixChallenge`, `handleMatrixChallengeResponse`, and `login`.
It has no repository, socket, or player dependency. A connection owns its own
challenge and verification state; PC/mobile version and modulus profiles are
configuration values rather than database globals.

Both Java implementations use the same algorithm and seed. Their generated
secrets are identical when their moduli are identical, despite comments claiming
they are different. Device zero selects PC; every other device selects mobile,
as in `MatrixChallengeManager.isPC`. The supplied source defaults to modulus
4,294,967,291 and version `0.0.7` in `ConfigStudio`; Java's PC repository may
override both. Set the C++ profiles to the values actually used by the intended
client; no account or deployment database settings were imported.

Secret generation starts directly at `0x5eedface & ((1<<48)-1)`, without the
initial XOR performed by `java.util.Random`. Each draw updates the state as
`(state * 0x5deece66d + 11) & ((1<<48)-1)`, returning the top 32 bits. Pair two
draws into an unsigned 64-bit integer and reduce modulo the selected modulus.
This fills the 5x5 matrix in row order. Challenge generation instead uses the
normal JDK seed XOR and one unsigned 32-bit draw per cell, again reduced modulo
the profile. The host supplies a fresh seed for each live issue.

The answer is `S * C * S` modulo the profile modulus. The C++ implementation
uses unsigned arithmetic and a bounded modular multiplication fallback when the
product would overflow 64 bits. It supports moduli from 2 to `INT64_MAX` and
requires canonical matrix entries. This reproduces Java's positive `BigInteger`
arithmetic for valid generated matrices. Invalid/degenerate moduli are rejected.

Command 120 carries a challenge with BE int `5` followed by 25 BE ints (104
logical bytes). Its response has 25 pairs of BE high/low ints (200 logical bytes)
and **no size prefix**. The normal `LegacyCodec` applies Base64 and the special
outbound 28-bit framing to the challenge. Responses use inbound BE24 framing.

`verifyResponse` distinguishes first acceptance, a wrong response that can be
retried, and a duplicate after acceptance. A newly issued challenge resets the
verification state. Login must remain blocked until `verified()` is true. In
the Java flow, the first matrix success sends a separate ECC challenge; this
module does not conflate the matrix result with account or ECC authentication.

Malformed response lengths and trailing bytes are rejected. Java reads the 25
pairs but does not check for trailing bytes; that invalid-input permissiveness
is deliberately not reproduced. Unknown device routing is reproduced, although
the host may choose a stricter client-info policy.

`tests/MatrixChallengeTests.cpp` includes hardcoded LCG/secret/challenge values,
matrix identities, a near-`INT64_MAX` product case, wire-layout fixtures,
version/device profiles, retries, duplicates, and session resets. These tests
have not been built or run; the user requested that builds be done manually.
