# Hunr2026 Java wire format: offline reference

The running server now uses the ordinary ServerEngine TCP protocol documented
in `client-wire-protocol.md`. `LegacyCodec` remains an offline compatibility
reference and fixture/replay tool. Its XOR/Base64 and 24/28-bit framing below
are not used by `LegacyApplication`.

This module ports the server-side framing and primitive serialization used by the
Java source in `Sample game old/HUNR_Server_Java/Hunr2026`. It is a protocol adapter:
the logical `Packet` contains a signed command byte and payload bytes, with no
socket, entity, account, map, or skill dependencies. A different game can reuse
the application systems while replacing this adapter.

This document describes source comparisons and the supplied fixture tests. No
C++ build, test execution, Java execution, or old-client connection was performed
for this implementation. Protocol framing alone does not establish gameplay,
login, persistence, resource-download, or full client compatibility.

## Source of truth

| Java source | Behavior reproduced |
| --- | --- |
| `network/Session.java:277-347` | Outbound Base64, resource exceptions, lengths, XOR ordering |
| `network/Session.java:350-371` | Batch payload format |
| `network/Session.java:374-392` | Independent rolling read/write XOR cursors |
| `network/Session.java:492-518` | Handshake key chain, host, port, redirect, voice port |
| `network/Session.java:1451-1499` | Sender batching policy used to choose default count limit |
| `network/Session.java:1506-1589` | Handshake request dispatch and inbound BE24/Base64 behavior |
| `network/Message.java` | Message uses `FastDataInputStream` and `FastDataOutputStream` |
| `network/FastDataInputStream.java:83-95` | Actual standard UTF-8 string input |
| `network/FastDataOutputStream.java:70-79` | Actual standard UTF-8 string output |
| `consts/Cmd.java` | Signed command identifiers |

The table's Java paths are relative to
`Sample game old/HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong`.

## Logical payload primitives

`PacketReader` borrows the payload storage; that storage must outlive the reader.
Reading beyond the payload throws `ProtocolError`, without reading adjacent
memory. `remaining()` supports the optional trailing fields present in several
Java handlers; `requireEnd()` is available for commands whose layout is exact.

| Primitive | Bytes |
| --- | --- |
| byte | 1, signed command interpretation in the range -128 to 127 |
| boolean | 1; output 0/1, input any nonzero value means true |
| short / char | 2, big endian |
| int | 4, big endian |
| long | 8, big endian |
| float / double | IEEE-754 bits, big endian; Java canonical NaN on output |
| `readUTF` / `writeUTF` | Unsigned 16-bit **byte** length, then standard UTF-8 |

The string distinction matters. Although `java.io.DataInputStream.readUTF` uses
modified UTF-8, this game's `Message` uses its own Fast streams. A NUL character
is one byte `00`; a supplementary character uses the normal four-byte UTF-8
sequence. `PacketReader::readModifiedUTF` and
`PacketWriter::writeModifiedUTF` exist separately for data formats actually using
the JDK Data streams. Their C++ interface uses UTF-16 code units and preserves
isolated surrogates, matching modified UTF-8's model.

Two deliberate invalid-input differences: standard UTF-8 readers reject malformed
sequences instead of the Java constructor replacing them; writers reject strings
over 65,535 bytes instead of wrapping the Java Fast writer's length prefix. These
do not change the encoding of valid, bounded strings.

## Directional framing

### Client to server

All commands, including resource requests, use:

```text
signed command: 1 byte
encoded payload length: 3 bytes, big endian
encoded payload: length bytes
```

Before the key is active, the bytes are plaintext. Afterwards, every byte in the
entire frame is XORed against the session key, including command and length. The
read cursor continues across frames.

After decryption, the Java code attempts basic Base64 decoding for every nonempty
payload. It accepts omitted final padding. If the payload is invalid Base64, the
original bytes become the logical payload. The C++ codec reproduces this fallback,
including for command -74. Valid Base64-looking raw payloads are therefore decoded
just as the Java source decodes them.

`feed` accepts arbitrary TCP chunks; `tryDecode` returns only complete packets.
Probing an incomplete frame never advances the committed XOR cursor. Decode one
packet, dispatch it, and then decode the next; do not decode the whole buffer
before processing a handshake because a coalesced subsequent packet may require
the newly activated key.

### Server to client

Payloads are Base64 encoded except command -74 (`GET_IMAGE_SOURCE`), which retains
the original payload. Empty payloads still have their command and zero length.

Before the handshake, all messages use the same big-endian 24-bit length as above.
After the handshake, these commands instead use a four-byte 28-bit length:

| Commands | Java names |
| --- | --- |
| -32, -66, 11, -67 | BACKGROUND_TEMPLATE, GET_EFFDATA, REQUEST_NPCTEMPLATE, REQUEST_ICON |
| -87, 66, -74 | UPDATE_DATA, GET_IMG_BY_NAME, GET_IMAGE_SOURCE |
| 120, 59, 60 | Numeric exceptions in `isSpecialMessage` |

The four length chunks are least significant first. For shifts 0, 8, and 16,
take eight bits; for shift 24, take four bits. Subtract 128 from **each** chunk,
cast to a byte, then apply XOR. This is neither ordinary little endian nor a
varint. For example a resource payload length `0x010203` yields length bytes
`83 82 81 80` before XOR.

The write cursor is independent from the read cursor. It continues across all
outbound headers and payloads. `encode` commits it only after constructing the
whole frame. Once encoded frames exist, the host must transmit them in that
exact order. If a frame cannot be queued, close the session; dropping it and
continuing would desynchronize the client's cipher cursor.

## Handshake

The client sends command -27 (`GET_SESSION_ID`), ordinarily with empty payload.
The Java collector creates a one-byte key. `encodeHandshake` accepts a caller
supplied key so deterministic fixtures can check the bytes; the host chooses the
live key and connection policy.

The response's logical payload is:

```text
byte key length
byte key[0]
for i = 1..length-1: byte (key[i] XOR key[i-1])
UTF host
int port
boolean redirect
int voicePort
```

The response itself is Base64 encoded, uses the plaintext BE24 frame, and does
not consume either XOR cursor. Only afterwards does `encodeHandshake` activate
the key. `activateKey` is also exposed for deterministic replay, without creating
a handshake frame. It rejects duplicate activation and keys outside 1..127 bytes:
Java's signed-byte cursor overflows for longer keys, and its actual collector
uses a length of one.

XOR here reproduces the legacy transport format. Matrix/ECC authentication,
account authentication, and their state transitions belong to separate modules.

## Batches

Command 115 (`BATCH_MESSAGE`) has this logical payload:

```text
short message count
repeat count times:
    byte command
    int logical payload length
    byte[length] original logical payload
```

Children are not individually Base64 encoded or framed. The complete batch is
then encoded through the ordinary outbound path once. `makeBatch` and
`unpackBatch` enforce bounded counts and lengths. The default count limit is 50,
matching the Java sender. `unpackBatch` is a structural/replay helper: Java's
inbound `MessageHandler` does not automatically accept batches, so the host must
not turn arbitrary inbound command 115 into privileged nested dispatches.

The codec does not schedule Java's two-millisecond sender collection window,
its handling of command -30 during collection, or its 2,048-message sending
queue. These are host scheduling/backpressure concerns, independent of the
serialized batch format.

## Limits and extension boundary

`CodecLimits` limits encoded inbound length, encoded outbound length, total
buffered input, and batch count. Default per-frame encoded payload limits are
16,777,215 bytes, matching inbound BE24 capacity. A host can choose smaller
budgets, or explicitly raise outbound resource limits up to 28-bit capacity.
The aggregate input limit is enforced before appending a chunk; callers must
drain complete packets between chunks.

A framing limit failure marks the codec failed; subsequent input/output throws
until an explicit `reset`. Reuse the session only by creating a new connection
and codec, or applying a deliberate reconnect policy. Truncated TCP frames wait
for further bytes; a disconnect with buffered bytes is the host's truncation
event. The codec does not own sockets or perform I/O.

Keep game-specific command dispatch outside the codec. New gameplay effects,
entities, persistence adapters, and game packs should communicate in domain
types, with this adapter translating only at the connection boundary.

## Fixtures and verification boundary

`tests/LegacyCodecTests.cpp` defines `runLegacyCodecTests`. Its expected frames
are hardcoded from the Java layouts instead of using the production encoder to
manufacture decoder input. It includes signed primitive boundaries, IEEE-754,
standard and modified UTF, one-byte and chained-key handshake responses,
independent rolling cursors, split/coalesced input, the resource length format,
Base64 success/fallback/padding, batch structure, and allocation/length limits.

These are **unexecuted test sources** until the user builds and runs them. An
additional old-client exchange and Java/C++ differential replay are needed to
establish runtime compatibility beyond the inspected serialization contracts.
