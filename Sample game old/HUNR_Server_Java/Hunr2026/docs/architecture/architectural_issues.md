# Architectural Issues

## Over-Scoped Files

The following files carry too much unrelated responsibility for a safe C++ port:

- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/user/Player.java` is about 978 KB. It contains character state, combat, inventory, shop/economy, events, map movement, persistence, network response triggers, and feature-specific logic.
- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/network/Service.java` is about 126 KB. It is both a protocol serializer and a gameplay-facing response API for map, inventory, clan, combat, resource download, and UI flows.
- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/server/Server.java` is about 93 KB. It owns config, DB connection, startup, content loading, cache generation, socket accept loop, event startup, and shutdown.
- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/map/tzone/Zone.java` is about 67 KB. It owns zone state, starts its own thread, updates players/mobs/items, handles combat, map entry/leave, drops, respawn, and some special map behavior.
- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/network/Session.java` is about 62 KB. It owns socket IO, protocol crypto, message framing, login, character loading, disciple loading, heartbeat, sender batching, and session cleanup.
- `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/mob/Mob.java` and `Info.java` are also large enough to hide mixed combat/stat/runtime behavior.
- `_HunrProvision/MainUpdate.java` mixes event scheduling, resets, rewards, auto-registration, map kickout logic, and DB updates.
- `_HunrProvision/HoangAnhDz.java` is a mixed utility/service class with drop logic, bot creation, logging, checksums, and raw SQL execution.

## Unclear Ownership

- `DragonBall`, `GameRepository`, `MapManager`, `SessionManager`, `Top`, `Lucky`, and many services are global singletons or static registries. Object lifetime is process-wide and not explicit.
- `Server` creates maps and zones, but `Zone` starts its thread from the constructor. This makes ownership and shutdown order unclear.
- `Session` owns the socket and threads, but `Player` points back into `Session`, `Service`, and `Zone`. `Zone` also stores players. The effective owner of an online player is not explicit.
- `Service` may be constructed around either a `Session` or a disciple `Player`, so its lifetime and valid use cases are not obvious from the type.
- `MySQLConnect` exposes one static JDBC connection used across startup, runtime loops, repositories-adjacent code, and event/top/shop features. Spring Data repositories likely have their own connection management, creating two persistence paths.
- `AutoBackup.start()` runs inside server initialization and writes backups of source and SQL as a startup side effect. That is operational ownership inside gameplay startup.

## Wrong Or Leaky Abstractions

- Spring Boot is used to construct repositories and REST controllers, but most runtime code bypasses dependency injection through static singletons.
- `SQLStatement` centralizes some SQL strings, while many other SQL statements are embedded directly in feature classes. There is no single data-access boundary.
- DB rows contain large JSON fields for map, task, item, player, and event state. The code parses those structures in many places without a central schema contract.
- `Zone extends Thread`; a world object is also an execution primitive. This makes testing, restart, and ownership hard.
- `Service` is named generically but is effectively a legacy protocol writer plus gameplay presentation layer.
- `Player` is both an entity and an application service. It validates commands, mutates state, writes persistence, triggers events, and serializes responses through `Service`.
- `ConfigStudio` uses compile-time constants for runtime feature switches. Changing events or modes requires code changes and redeploys.
- Security/authentication concerns are mixed into `Session` and `MessageHandler` instead of an authentication/session state machine.

## Duplicated Responsibilities

- Persistence exists through both Spring Data repositories and raw JDBC. Some features use repositories, some use `MySQLConnect`, and some use helper methods that accept raw SQL strings.
- Config exists in at least four forms:
  - `application.properties`
  - `Config/config.ini`
  - `_HunrProvision.ConfigStudio` constants
  - DB-backed config tables such as `nr_others`, `nr_drop_rate`, `nr_security`, and Bo Mong tables
- Event scheduling exists in `Server`, `MainUpdate`, `MapManager`, and individual event/top classes.
- Command handling is split between `MessageHandler`, `Session`, `Player`, and `Service`.
- Runtime data loading is split between `Server.init*`, repository-backed service loaders, raw SQL feature loaders, and resource-file loading.
- Shop/top/reward features repeatedly perform manual SQL and reward construction patterns.

## Weak Extension Points

- Adding a new item option/stat/attribute likely requires edits across item template parsing, `Item`, `Info`, `Player`, `Service`, and multiple feature-specific command handlers.
- Attribute effects are largely magic numeric option IDs and array indexes. The semantic owner for an option is not centralized.
- Adding a new command usually means touching `Cmd`, `MessageHandler`, `Player`, `Service`, and sometimes persistence and map/runtime classes.
- Adding a new event often means adding a new package/class and inserting scheduler calls into `MainUpdate`, `Server`, or `MapManager`.
- Adding a new map type currently requires editing `TMap.init()` to instantiate a specific `Zone` subclass based on map ID.
- Adding new DB-backed content requires both SQL/schema updates and hand-written Java load code. The requested `sql` folder currently covers only Bo Mong, not the whole Java runtime schema.
- The current C++ `WorldService` and `GatewayServer` are already large command/protocol hubs. Without further decomposition, they can recreate the Java monolith in C++.

## Migration Risks

- Missing Maven and `application.properties` sources mean the Java build/runtime environment cannot be reconstructed fully from checked-in source alone.
- The Java runtime depends on many DB tables not defined in the requested SQL folder. Porting must validate actual schema before assuming fields.
- Java side effects are often hidden in static initializers, constructors, or startup methods. Examples include `Server` config loading, `Zone` thread start, `AutoBackup.start()`, and matrix challenge static setup.
- Threading behavior is distributed across session threads, every zone thread, map manager, event update, autosave, voice server, scheduled tasks, and REST-triggered maintenance. A direct thread-for-thread port would be fragile.
- Many features rely on live online player lookup through `SessionManager`, so offline/online consistency boundaries are weak.

