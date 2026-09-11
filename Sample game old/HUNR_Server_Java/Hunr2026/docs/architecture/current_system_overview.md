# Current System Overview

## Scope

Sources inspected for this document:

- Java source of truth: `HUNR_Server_Java/Hunr2026`
- SQL/schema source requested: `HUNR_Server_Java/Hunr2026/sql`
- C++ target: `AH_BE_NRO_SERVER_C++`

Unrelated modules were not expanded. The `_website`, client, UI, backup, log, and generated C++ full-port trees were not used as source of truth. Static assets under `resources` and `data` were considered only where the Java startup/runtime code directly references them.

## Project Entry Points

### Java Server

- Main application: `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/Run.java`
- Runtime bootstrap: `com.ngocrong.Run.main()` calls `SpringApplication.run(...)`.
- Startup hook: `Run` implements `CommandLineRunner.run(...)`.
- Game server bootstrap: `Run.run(...)` wires Spring repositories into `GameRepository`, loads several DB-backed static configs, then calls `DragonBall.getInstance().start()`.
- Socket game server: `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/server/DragonBall.java` creates `Server`, registers a shutdown hook, then calls `server.init()` and `server.start()`.
- Main TCP listener: `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/server/Server.java`, `start()`, opens the game port from config.
- Voice TCP listener: `HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/server/voice/VoiceServer.java`, started from `Server.start()`.
- REST/API entry points under Spring component scan:
  - `GET /api/server/bao-tri` starts maintenance countdown.
  - `GET /api/server/get-item` grants an item to an online player.
  - `POST /api/server/cms` registers a user through raw JDBC.
  - `DropRateController` exists but its mappings are commented out.

### C++ Target

- Main executable: `AH_BE_NRO_SERVER_C++/src/main.cpp`
- Main CMake target: `nro_server`
- Core library target: `nro_core`
- Entry flow: load `.env`, connect to MySQL content, load `ContentRegistry`, choose persistence backend, start `WorldService`, then start `GatewayServer`.

## Build And Config Summary

### Java Build

- `HUNR_Server_Java/Hunr2026/build.bat` invokes `mvn clean package -DskipTests`, falling back to `mvnw.cmd` if `mvn` is unavailable.
- `HUNR_Server_Java/Hunr2026/run.bat` expects `target/HunrProvision-0.0.1-SNAPSHOT.jar` and starts it with:
  - `-Xms1G -Xmx24G`
  - G1 GC
  - heap dump on OOM
  - UTF-8 JVM/system properties
- `autoRun.bat` watches port `14445`; if the port is not listening, it builds and starts the server.
- Uncertainty: no `pom.xml`, `build.gradle`, `application.properties`, or `application.yml` was found under the git repository during this scan. The Java code and scripts clearly expect Maven and a classpath `application.properties`, but the source files that define dependency versions and runtime properties were not present.

### Java Runtime Config

- `com.ngocrong.server.Config` loads `application.properties` from the Java classloader and expects keys including:
  - `server.id`, `server.name`, `server.port_game`, `server.host`, `server.redirect`, `server.port_voice`
  - `server.autosave.delay`
  - `database.host`, `database.port`, `database.name`, `database.user`, `database.password`
  - `game.data.version`, `game.item.version`, `game.map.version`, `game.skill.version`
  - `game.exp`, `game.item.quantity.max`, `game.servers`
- `_HunrProvision.MainConfig` separately loads `Config/config.ini` for rates and combine/cold reward tuning.
- `_HunrProvision.ConfigStudio` contains hardcoded feature flags, version strings, public messages, and event toggles.
- Several additional runtime configs are loaded from DB repositories or raw JDBC, including drop rate, Bo Mong, security/matrix challenge, and `nr_others`.

### SQL Source

- The requested SQL folder contains one schema file: `HUNR_Server_Java/Hunr2026/sql/bo_mong_setup.sql`.
- That file defines the Bo Mong tables:
  - `nr_bo_mong_nhiem_vu_config`
  - `nr_bo_mong_boss_config`
  - `nr_bo_mong_moc_diem`
  - `nr_bo_mong_config`
  - `nr_bo_mong_nhiem_vu`
  - `nr_bo_mong_history`
- It also alters `nr_player` with Bo Mong-related columns and index.
- Java code references many other tables through JPA annotations, `SQLStatement`, and direct JDBC. Those tables are not defined by the requested `sql` folder source.

### C++ Build And Config

- `AH_BE_NRO_SERVER_C++/CMakeLists.txt` uses C++20.
- Main targets:
  - `nro_core`
  - `nro_server`
  - `nro_tests`
  - optional `nro_db_bench`
  - `nro_combat_bench`
- Optional generated skeleton sources are included from `generated/full_port_sources.cmake` when present.
- MySQL support is gated by `NRO_HAS_MYSQL` and a Windows-specific `MYSQL_ROOT` path.
- `AH_BE_NRO_SERVER_C++/.env` provides MySQL and server settings. Do not treat checked-in secrets as a design pattern; use environment or local secrets outside source control for migration work.

## Main Java Subsystems

- Bootstrap/API: `Run`, `api/*`, Spring repository injection, and REST endpoints.
- Server lifecycle: `DragonBall`, `Server`, `ServerMaintenance`, `AutoSaveData`, `AutoBackup`.
- Config: `Config`, `ConfigStudio`, `MainConfig`, DB-backed config services.
- Persistence:
  - Spring Data repositories under `com.ngocrong.repository`
  - JPA entities under `com.ngocrong.data`
  - raw JDBC through `MySQLConnect` and direct `PreparedStatement` usage
  - static repository locator `GameRepository`
- Network/protocol: `Session`, `Message`, `MessageHandler`, `Service`, `FastDataInputStream`, `FastDataOutputStream`.
- World/map runtime: `MapManager`, `TMap`, `Zone` and specialized `tzone` subclasses.
- Player/domain logic: `Player`, `User`, `Info`, `Item`, `Skill`, `Mob`, `Npc`, `Task`, `Clan`, `Shop`, `Top`.
- Events/game modes: `_HunrProvision.MainUpdate`, `_HunrProvision.services.*`, `com.ngocrong.NQMP.*`, `_event.*`, boss/bot packages.
- Security/client validation: matrix challenge, ECC/multilayer security, DLL validation.
- Voice: game voice server/session plus voice chat manager/service classes.

## C++ Target Subsystems

- Config/content: `nro::config::EnvLoader`, `MySqlContentLoader`, `ContentRegistry`, content definition structs.
- Core utilities: logger, blocking queue, time helpers, MySQL backup service.
- Persistence: `IGameRepository`, in-memory repository, MySQL repository.
- Network/server: `GatewayServer`, `CommandDispatcher`, socket platform abstraction.
- World: `WorldService`, `Zone`, `PlayerService`, `InventoryService`, `CombatEngine`, `CombatService`, `MobService`, `ZoneService`, `BossManager`, schedulers.
- Domain model: `nro::domain` structs in `include/nro/domain/entities.h`.

## Dependency Overview

High-level Java dependency direction observed:

```text
Spring Boot Run
  -> Spring Data repositories
  -> GameRepository singleton
  -> DropRateService / BoMongService / MatrixChallengePC
  -> DragonBall
      -> Server
          -> Config
          -> MySQLConnect
          -> SQLStatement tables
          -> Content caches and template loaders
          -> MapManager / TMap / Zone
          -> SessionManager / Session / MessageHandler / Service
          -> MainUpdate / AutoSaveData / BossManager / Lucky / Top / Consignment
```

The runtime code frequently reaches sideways through global singletons:

```text
Player -> DragonBall.getInstance().getServer()
Player -> GameRepository.getInstance()
Player -> MapManager.getInstance()
Player -> Zone -> MapService/Service
Service -> DragonBall.getInstance().getServer()
Session -> SessionManager / MessageHandler / Service / User / Player
Top/Event/Shop/Clan -> MySQLConnect and/or GameRepository
```

The current C++ target is already more layered than the Java source, but `WorldService` and `GatewayServer` still carry broad responsibilities:

```text
main.cpp
  -> EnvLoader
  -> MySqlContentLoader -> ContentRegistry
  -> IGameRepository implementation
  -> WorldService
      -> PlayerService / InventoryService / BossManager / EventScheduler / Zones
  -> GatewayServer
      -> CommandDispatcher
      -> WorldService command execution
```

