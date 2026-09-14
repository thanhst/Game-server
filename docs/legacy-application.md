# HUNR application boundary

`application::GameModule` owns application sessions and receives complete TCP
payloads. `TcpHost` owns the ServerEngine C ABI handle, poll loop and transport
queues. Neither requires a HUNR skill, item, class, map or database column.

`LegacyApplication` implements the HUNR connection and account lifecycle:

1. GET_SESSION_ID (-27): new version-one hello with protocol version, capability
   flags, host, port, redirect and voice port. No XOR key or Base64 is used;
   ServerEngine provides the four-byte length prefix for every message.
2. NOT_LOGIN/CLIENT_INFO (-29/2): zoom, dimensions, device, version; server-list,
   resource version, DLL list, matrix challenge. Source supports zoom2..4.
3. MATRIX_CHALLENGE (120): source matrix calculation and retry; successful
   verification sends the P-256 ECC challenge (121). Matrix success gates login.
4. ANDROID_PACK (126): device identity, required before attaching a character.
5. NOT_LOGIN/LOGIN (-29/0): version/username/password parsing, guest password
   rule, maintenance/role/lock state, duplicate-account handling, login cooldown,
   image version arrays and template version packet.
6. Authenticated cache requests: map, skill, item0/1/2/100 and UPDATE_DATA caches
   serialized from the original definitions. No image file is embedded in Git.
7. FINISH_UPDATE (-38): ECC is diagnostic, as the Java enforcement is commented
   out; an account without a character gets the global CREATE_PLAYER packet.
8. NOT_MAP/CREATE_PLAYER (-28/2): original name rules, hair/gender defaults,
   items, stats, money, task, slots and tree. Account storage claims ownership
   atomically. Gameplay handoff requires account and device state.

## Interfaces for another game

- Implement `GameModule` for a different wire protocol or account/world flow.
- Implement `LegacyGameplay` to keep the migrated command payloads and replace game
  systems. It receives authenticated character ownership, not a client-selected
  account ID. Its enter/packet/tick methods produce explicitly addressed packets.
- Implement `IdentityStore` for MySQL or an account service. It provides atomic
  character uniqueness and revision-checked save, with no socket knowledge.
- Use immutable `ContentSnapshot` instances per world; load and validate a new
  snapshot before using it for new worlds. Actor state stays outside definitions.
- Add a `RuleProfile` handler or effect implementation; new-game definitions can
  use the string IDs/attribute registry of `GameDomain` instead of HUNR byte IDs.

## Deployment configuration

Copy `config/hunr.example.json` to `config/hunr.local.json`. Paths resolve relative
to the configuration file, including Unicode paths on Windows. Local files and
the runtime directory are ignored by Git. The example is a local configuration,
not recovered Java deployment settings: cache versions, checksums, DLL lists and
resource versions must be supplied for an actual deployment with the updated client.

The included SQLite adapter has a namespaced schema and salted PBKDF2 password
verification. It does not import live account rows, connect to the old MySQL
database, or expose an unauthenticated registration endpoint. Create local test
accounts with `--create-local-account <config> <username>`; the password is read
from stdin rather than command-line arguments. Use a private local console.

The adapter stores a versioned character JSON document using Java SQL column
names, with embedded JSON decoded once; unknown component fields survive saves.
It prevents duplicate characters per account and stale-save overwrites. These
are deliberate storage improvements, not an assertion of MySQL schema parity.

## Current integration limits

The default `--legacy` host installs connection/authentication/content modules.
It does **not** install a full `LegacyGameplay` implementation for Player.enter,
every gameplay opcode, guilds, trade, quests or boss subclasses. An unsupported
world handoff emits a client dialog and an operator diagnostic, without claiming
that a character entered a map. `LegacyWorld` is independently usable through
the replay entry point and its public API; this is not yet an end-to-end old
client game server replacement. See `migration-status.md` for the boundary.

Parser limits, handshake deadlines, malformed UTF rejection, normalized cooldown
keys, device limits, bounded queues and character uniqueness are explicit
differences from malformed/racy Java paths. No compile or network execution was
performed by the agent, as requested by the user.
