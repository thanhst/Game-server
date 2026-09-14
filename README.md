# GameServer - C++17 on ServerEngine

This repository contains the C++ gameplay prototype that runs on ServerEngine.
Compatibility modules, import tools, cache builders, diagnostic tools, and
account storage experiments have been removed from this project.

The current server uses ServerEngine's normal TCP framing and the simple
`GAME/1` development protocol. It keeps state in memory, exposes a console mode,
and can host local TCP clients on `127.0.0.1`.

## What Remains

- `GameDomain`: data-driven maps, characters, skills, effects, entity ownership,
  cooldowns, mana, movement, timed effects, shields, control tags, and event
  budgets.
- `GameProtocol`: the text `GAME/1` protocol used by console mode, the debug
  client, and the local TCP host.
- `GameServer`: executable entry point for `--console`, `--validate-content`,
  and `--serve`.
- `content/demo.game`: source-only demo definitions. It contains no images,
  sounds, map binaries, account data, or client resources.

## Read Next

1. [Architecture](docs/architecture.md) explains ownership, ticks, effects, and
   extension points.
2. [Protocol](docs/protocol.md) describes the current TCP and console commands.
3. [Content format](docs/content-format.md) explains how to add characters,
   skills, effects, and maps through data.

## Manual Build

The user builds this project manually. A typical flow is:

```powershell
git submodule update --init --recursive
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

After building, run from the repository root or from the output directory where
`content/demo.game` is copied:

```powershell
.\out\build\ninja-debug\GameServer.exe --validate-content content/demo.game
.\out\build\ninja-debug\GameServer.exe --console content/demo.game
.\out\build\ninja-debug\GameServer.exe --serve content/demo.game 7777
python scripts/debug_client.py --port 7777
```

No build, test binary, live server, or socket client was run by the agent while
making this cleanup.
