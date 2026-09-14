# Development protocol GAME/1

This executable hosts the C++ gameplay prototype. It binds only to `127.0.0.1`, has no authentication, keeps characters only in memory, and runs one world on one owner thread. The `mob` training creature is a stationary target; this slice does not include AI or respawn.

## Start modes

After you build the project yourself, run the executable from the repository root (or its output directory, which receives `content/demo.game`):

```powershell
.\path\to\GameServer.exe --validate-content content/demo.game
.\path\to\GameServer.exe --console content/demo.game
.\path\to\GameServer.exe --serve content/demo.game 7777
```

No arguments selects `--console`. Content validation loads definitions, validates references and registered effect handlers, and exits without starting ServerEngine or sockets. Console mode also does not initialize the engine or open sockets. The executable is linked to ServerEngine, so Windows still needs its DLL beside the executable. `--serve` advances the world in 50 ms steps; Ctrl+C/SIGTERM stops it. Ports accept only a decimal integer from 1 through 65535.

The host seeds the `mob` archetype, when present, on team 2 at `(140,100)` and prints its ID. New playable characters use team 1 and spawn at `(100,100)`. Coordinates are capped at the map dimensions. The development host requires the `arena` map. There is no protocol command to choose a team or impersonate another session.

## Frames and messages

Each TCP frame is a **4-byte unsigned big-endian byte count**, then exactly that many payload bytes. ServerEngine adds/removes that header; the game passes only the payload to the C ABI. Payloads are bounded to 4096 bytes. Requests are one printable ASCII line, with spaces between tokens. There is no terminating NUL or newline on the wire. Responses may contain newline-separated records.

Every request starts with `GAME/1`. Every response also starts with `GAME/1`. The server sends an initial `GAME/1 HELLO ...` greeting. The development client adds the prefix automatically when you enter just a command. IDs and names accept letters, digits, `_`, `-`, and `.`. Names are at most 32 characters and content IDs at most 64. Leading/trailing spaces are tolerated; extra arguments, control bytes, nonfinite coordinates, and partially parsed numbers are rejected.

| Request after `GAME/1` | Behavior |
| --- | --- |
| `PING` | Replies `OK PONG`; allowed before character creation. |
| `CREATE <archetype> <name>` | Creates exactly one character for this connection. Archetype must have the `player` tag. Replies `OK CREATED id=N`. |
| `MOVE <x> <y>` | Moves the connection's character after authoritative bounds, speed, and status checks. |
| `CAST <skill> <target-id>` | Uses a learned skill, checking team, map, range, mana, cooldown, and statuses. For a self skill, pass your own entity ID. |
| `STATE` | Returns your current and maximum HP/mana, position, map/team, and active effects. |
| `ENTITIES` | Returns entities visible in your current map. Oversized lists report `MORE entities_omitted=N`. |
| `TICK <milliseconds>` | Console only: advances 1–60000 ms. Network clients receive an error. |
| `QUIT` | Requests disconnect and despawns the owned character. |

Failures return `GAME/1 ERROR <reason>`. Unexpected internal exceptions, event overflow, or an oversized transport event stop the host, allowing RAII to stop/destroy the engine instead of continuing with lost authoritative state. Rejected gameplay commands do not grant movement, ownership, or resources. Normal skill failures return their specific reason, such as `cooldown`, `out_of_range`, or `insufficient_mana`.

`GAME/1 EVENTS` frames contain records like:

```text
EVENT kind=damage source=2 target=1 content=energy_hit value=28
```

The network host broadcasts events to created characters in the same map. Before `CREATE`, a connection receives only direct protocol responses. Event frames can arrive between command replies; clients must distinguish `EVENTS` from `OK`/`ERROR`. Separate frames keep each payload within 4096 bytes. Event records are notifications; use `STATE`/`ENTITIES` for snapshots. Console prints the same events after commands. Slow peers whose send queues reject a write are disconnected and lose character ownership. A successful engine send means local queueing, so `BYE` is not guaranteed to arrive before disconnect.

Transport configuration limits this host to 32 connections, 4096-byte messages, 64 KiB send queues per peer, 2048 queued transport events, and 1 MiB queued event bytes. Idle connections time out after 120 seconds. At most 64 transport events are processed before checking the simulation clock. Fixed ticks catch up in batches of at most 20 steps; each step stays 50 ms, and elapsed simulation time is not silently skipped.

## Manual console example

With the bundled definitions and a fresh process, the printed dummy ID is 1 and the first created character ID is 2. Use the actual returned IDs if these differ. The skill `energy` has range 24; movement accrues at most one second of speed credit.

```text
GAME/1 CREATE guardian Thanh
GAME/1 ENTITIES
GAME/1 TICK 1000
GAME/1 MOVE 108 100
GAME/1 TICK 1000
GAME/1 MOVE 116 100
GAME/1 CAST energy 1
GAME/1 CAST shield 2
GAME/1 STATE
GAME/1 TICK 5000
GAME/1 STATE
GAME/1 QUIT
```

This shows movement, direct damage, a self shield, and timed expiry. Console time advances only on `TICK`; network time advances automatically. New connections spawn at the same point, so this prototype has no collision resolution.

## Python client

The client uses only Python's standard library, connects only to localhost, and validates frame sizes:

```powershell
python scripts/debug_client.py --port 7777
python scripts/debug_client.py --port 7777 --command "CREATE guardian Thanh" --command STATE --command ENTITIES
```

Interactive input can omit `GAME/1`, for example `CREATE medic Healer`. The receiver prints asynchronous events. Scripted mode waits for each direct reply and prints event frames encountered while waiting. It returns a nonzero exit code for protocol errors. Closing the client discards its in-memory character. Use multiple terminals for separate sessions; all playable clients use the same team.

Protocol tests are in `tests/ProtocolTests.cpp`. They exercise parser bounds, versioning, ownership, non-player creation rejection, numeric edge cases, gameplay dispatch, disconnect cleanup, and console-only time advancement without sockets. They are provided for your manual build/test workflow; no build or runtime verification was performed while creating these files.
