# Verification record — 2026-09-14

The user explicitly builds the project manually. No CMake configure, compilation,
CTest run, C++ executable, Java server, live database import/query, or network
client session was run by the agent. Source checks are not runtime proof.

Completed checks:

- Original recovery preserved 502 Java files/3,609,587 source bytes with per-file
  SHA-256 metadata. The source-only SQL retains 102 schemas, 52 static definition
  tables and 9,302 rows; no live account/player/history rows are imported.
- Python importer: nine parser fixtures pass. Its `--check` matches content.json
  against the SQL and manifest hashes and validates 191 skill levels, 1,340 item
  option references, 949 mob spawns, 171 NPC spawns and 249 waypoint references.
- All 40 CMake-listed C++ source paths and project-local quoted includes exist.
  Every LegacyTestMain entry has a test-source definition.
- CMakePresets.json, vcpkg.json, example configuration and replay JSON parse.
  Two Python files pass AST parsing; the client script was not executed.
- Git source candidate scan contains no media, archive, JAR, executable, database
  or legacy runtime/resource directories. Static content JSON/SQL are source
  dependencies. Local config and runtime storage are ignored.
- Source review checked packet boundaries, new hello bytes, signed commands,
  authentication ownership, cache ordering, Unicode encoding, identity revisions,
  callback identity/state preservation and timer arithmetic.
- Whitespace/conflict-marker checks pass. The ServerEngine submodule remains
  clean at `436f299c7bf4cfa04409a8273a3dce83910583ce`.

The live host uses existing SE_PROTOCOL_TCP. Its payload is one command byte plus
raw binary data; the engine supplies BE32 length framing. The earlier TCP_STREAM
extension was removed after the user chose to update the client. Old XOR/Base64
codec tests remain offline references. Source-only geometry loading is retained;
no deleted resource was recovered.

C++ test sources cover content/cache fixtures, byte primitives, old/new codecs,
matrix/ECC, character creation, storage uniqueness/stale revisions, session
handoff, combat ordering, cooldowns, custom-rule rollback, effect expiry, summons,
and deterministic replay. They still require the user's manual build/run.

The current host does not install a full LegacyGameplay implementation for every
Java gameplay flow. No end-to-end gameplay, load/performance, MySQL migration or
client compatibility result is claimed. See migration-status.md.
