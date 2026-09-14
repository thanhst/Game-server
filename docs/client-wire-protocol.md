# Game protocol version 1 over ServerEngine TCP

The running C++ application uses ServerEngine's ordinary `SE_PROTOCOL_TCP`.
The client must be updated to this format. `LegacyCodec` is retained for offline
Java-format comparison and replay; the running application does not use it.

## One framing layer

On a TCP socket, one message has this layout:

```text
uint32 big-endian message_byte_count
int8 command
byte[message_byte_count - 1] logical payload
```

The length includes the command byte and excludes its own four bytes. A valid
game message contains at least one byte. Every command, including resources,
matrix/ECC, and batches, uses this exact layout. There is no XOR, Base64, 24-bit
length, or special 28-bit resource length.

`se_server_send` receives only `[command][payload]`; ServerEngine adds the BE32
prefix. `SE_EVENT_MESSAGE` contains only `[command][payload]`; ServerEngine has
already removed the prefix and assembled any partial TCP reads. The application
must neither add nor parse a second prefix. The corresponding client socket
reader must accumulate four length bytes and then exactly that many message
bytes, retaining any subsequent bytes for the next message.

For example, command -109 with no payload is a one-byte message:

```text
Full TCP bytes:           00 00 00 01 93
ServerEngine MESSAGE:                93
```

`game::net::BinaryPacketCodec` converts complete engine messages to/from the
shared logical `Packet`. It is stateless. Its default incoming limit is 256 KiB
including the command; the outgoing limit is 8 MiB including the command. These
limits must fit the engine host's `max_message_bytes`. The class rejects zero
limits, empty messages, null storage, and oversized messages before reading or
allocating payload storage. TCP partial-frame handling belongs to ServerEngine,
including its connection limits and idle policy.

## Initial handshake

The first client message is command -27 with an empty logical payload:

```text
00 00 00 01 E5
```

The server's command -27 response has these payload fields:

| Field | Encoding | Version 1 value |
| --- | --- | --- |
| Protocol version | uint16, BE | 1 |
| Capability flags | uint32, BE | 0 |
| Advertised host | uint16 UTF-8 byte length, then UTF-8 | Configuration |
| Advertised port | int32, BE | Configuration |
| Redirect | one-byte boolean | Configuration |
| Voice port | int32, BE | Configuration |

There is no session key length or key material. The client checks the protocol
version and flags before continuing; unknown versions or unsupported flags must
be rejected rather than guessed. After queueing this response, the server marks
the connection ready for application commands. A nonempty handshake request or
any different command before handshake closes the connection. An empty repeated
handshake after readiness is ignored. An uncompleted handshake times out.

Example response with host `h`, port 1, redirect true, and voice port 2:

```text
Engine MESSAGE:
E5 00 01 00 00 00 00 00 01 68 00 00 00 01 01 00 00 00 02

Full TCP bytes add this prefix:
00 00 00 13
```

## Application payloads and client changes

Existing payload primitives remain big endian, with standard UTF-8 strings
prefixed by their unsigned 16-bit byte length. Preserve signed command-byte
interpretation and subcommands. The supplied Java Fast streams use standard
UTF-8, not JDK DataInput/DataOutput modified UTF-8. Payload bytes are passed
unchanged even when they resemble valid Base64 text.

Update the client's shared receive/send path once:

1. Replace Java-format 24/28-bit framing with BE32 framing for every message.
2. Remove transport Base64 encoding/decoding and rolling XOR cursors.
3. Replace the -27 key response parser with the version/flags/endpoint schema.
4. Keep command payload parsers for client info, caches, matrix, ECC, and gameplay.
5. Handle partial/coalesced TCP reads at the one BE32 framing layer.

Matrix command 120 still sends int `5` plus 25 unsigned 32-bit values; its answer
still contains 25 high/low 32-bit pairs. ECC command 121 still sends the seven
BigIntIO unsigned magnitudes. These payload rules are described in
`matrix-challenge.md` and `ecc-challenge.md`. Their old Java transport wrappers
apply only to `legacy-protocol.md`, not to the running version-one application.

Packet-format support is separate from command/gameplay completeness. The
application still reports unimplemented gameplay adapters or commands, and
deleted client resources are not reconstructed by the transport. See the
migration status for the remaining gameplay work.

## Verification boundary

`tests/BinaryPacketCodecTests.cpp` uses hardcoded message bytes for signed
commands, raw payload preservation, resource commands, the new handshake, and
bounds. The test source has not been built or run. No modified client exchange
has been performed; those checks remain for the user's manual build and client
update.
