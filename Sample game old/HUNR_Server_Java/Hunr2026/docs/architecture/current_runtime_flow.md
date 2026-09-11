# Current Runtime Flow

## Startup Flow

1. `Run.main()` starts Spring Boot.
2. Spring constructs `Run` and injects repository beans.
3. `Run.run(...)` copies injected repository beans into the static `GameRepository` singleton.
4. `Run.run(...)` loads early DB-backed static services:
   - `DropRateService.load()`
   - `BoMongService.loadConfig()`
   - `MatrixChallengePC.loadPCKey()`
5. `Run.run(...)` starts the game runtime through `DragonBall.getInstance().start()`.
6. `DragonBall.start()` creates a new `Server`.
7. `Server` constructor creates `Config` and immediately calls `config.load()`.
8. `DragonBall.start()` registers a JVM shutdown hook that calls `server.stop()`.
9. `DragonBall.start()` calls `server.init()`.
10. `DragonBall.start()` calls `server.start()`.

Uncertainty: `Config.load()` requires classpath `application.properties`. That file was not present in the repository scan. If the file is injected outside this tree or generated elsewhere, that source was not found.

## Config Loading Flow

### Java Main Properties

`com.ngocrong.server.Config.load()`:

```text
ClassLoader resource application.properties
  -> java.util.Properties
  -> Config fields
  -> Server constructor stores Config
```

Required properties are inferred from code, not from a present properties file:

- server identity/listening: `server.id`, `server.name`, `server.port_game`, `server.host`, `server.redirect`, `server.port_voice`
- database: `database.host`, `database.port`, `database.name`, `database.user`, `database.password`
- game versions: `game.data.version`, `game.item.version`, `game.map.version`, `game.skill.version`
- runtime tuning: `server.autosave.delay`, `game.exp`, `game.item.quantity.max`, `game.servers`

### Java Flat File Config

`_HunrProvision.MainConfig.load()` reads `Config/config.ini` after most server content is loaded. It extracts:

- EXP/reward multipliers: `exp`, `mobReward`, `bossReward`
- combine star count and per-star values: `SoSao`, `Sao_1` through configured max
- cold-map reward thresholds: `dothancold`, `docuoicold`, `huydietcold`

This file is parsed by repeated full-file reads in `loadStr(...)`.

### Java Hardcoded Flags

`_HunrProvision.ConfigStudio` contains compile-time constants:

- server version used by client validation
- website/messages
- admin/coming-soon modes
- event toggles such as `NPC_BO_MONG` and `EVENT_NEWYEAR_2026`

### Java DB Config

DB-backed configs are loaded by multiple paths:

- Spring Data repositories via `GameRepository`
- raw JDBC through `MySQLConnect`
- raw SQL string constants in `SQLStatement`
- ad hoc SQL helpers such as `_HunrProvision.HoangAnhDz.ExcuteQuery(...)`

Examples directly seen in startup:

- `DropRateService` reads `nr_drop_rate`
- `BoMongService` reads the Bo Mong config repositories
- `MatrixChallengePC` reads `nr_security`
- `Server.init()` reads many static game-content tables through `SQLStatement`

### C++ Target Config

`AH_BE_NRO_SERVER_C++/src/main.cpp` loads configuration through `EnvLoader`:

```text
.env file path, default ".env"
  -> EnvLoader::LoadAppConfig
  -> optional environment override
  -> AppConfig
```

Important keys include:

- `MYSQL_HOST`, `MYSQL_PORT`, `MYSQL_USER`, `MYSQL_PASSWORD`, `MYSQL_DATABASE`
- `SERVER_PORT`, `SERVER_STORAGE`
- `MYSQL_CONTENT_REQUIRED`, `MYSQL_STORAGE_REQUIRED`
- MySQL backup keys when enabled

## Data Loading Flow

### Java Repository Wiring

Before the game server starts, `Run.run(...)` assigns every injected repository to fields on `GameRepository.getInstance()`. After that, most non-Spring classes pull repositories from the static singleton.

Observed repository/entity tables include:

- accounts/characters: `nr_user`, `nr_player`, `nr_disciple`
- social: `nr_clan`, `nr_clan_member`
- economy/shop: `nr_consignment_shop`, `nr_gift_code`, `nr_gift_code_history`
- events/ranking/config: `nr_statistic`, `nr_event_open`, `nr_event_tet`, `nr_event_vqtd`, `nr_top_*`, `nr_security`, `nr_drop_rate`
- Bo Mong: tables defined in `sql/bo_mong_setup.sql`

These table names come from annotations and query strings. Field-level schema should be taken only from actual SQL files or entity fields during implementation work.

### Java Server.init Load Order

`Server.init()` performs a bulk synchronous load before accepting clients:

1. Start `AutoBackup`.
2. Create one raw JDBC connection through `MySQLConnect.create(...)`.
3. Load resource/content version arrays: background small, small, resource.
4. Load item templates and item caches.
5. Load `array_head_2_frames`.
6. Load captions, powers, effect data, NPC templates, mob templates, and maps.
7. Build map cache data.
8. Load skill templates and skill cache.
9. Load dart, arrow, effect, image, part, and skill-paint/cache data.
10. Load background items and miscellaneous config from `nr_others`.
11. Initialize skills, flags, tasks, images-by-name, clan images, game info, disciple skills, achievements.
12. Load CrackBall items, clan data, random item data, top rankings, lucky wheel, Namek dragon state, special skills, card templates, consignment data.
13. Reset DHVT/day/rank state.
14. Load `MainConfig`, valid DLL names, auto-reward list.
15. Calculate checksums for `nr_part`, `nr_item`, and `nr_item_option_template`.
16. Initialize multilayer crypto.

Most static content loads read from MySQL. Maps additionally read binary resources from `resources/map/{mapId}` and block data from `resources/map/block/{mapId}`.

### Java Map/Zone Data Creation

`Server.initMap()` reads `nr_map` and parses JSON columns for:

- waypoints
- mob coordinates
- NPC coordinates
- background item positions
- map effects
- event effects

For each map:

```text
nr_map row
  -> TMap
  -> TMap.init()
      -> load resources/map/{mapId}
      -> load resources/map/block/{mapId} when present
      -> construct Zone or specialized tzone subclass
      -> Zone constructor calls start()
```

Each `Zone` creates runtime mobs from `TMap.mobs` and templates from `Mob.getMobTemplate(...)`, clones NPCs, and starts its own 100 ms update loop.

### C++ Content Loading

The current C++ target loads content from MySQL:

```text
main.cpp
  -> MySqlContentLoader.Connect()
  -> LoadAllContent()
      -> nr_map
      -> nr_mob_template
      -> nr_npc_template
      -> nr_skill
      -> nr_item
      -> nr_item_option_template
      -> nr_task
      -> nr_others
      -> optional nr_boss_spawn
      -> optional merge from nr_bo_mong_boss_config
  -> ContentRegistry::DerivePlanetsFromMaps()
```

The C++ loader parses JSON embedded in `nr_map` for waypoints, mobs, NPCs, background items, and effects.

## World And Runtime Flow

### Java Socket Runtime

`Server.start()`:

1. Opens `ServerSocket(config.port)` with backlog `10000`.
2. Starts `VoiceServer` on `config.voicePort`.
3. Sets `start = true`.
4. Starts `MainUpdate` event loop thread.
5. Starts `AutoSaveData` thread.
6. Starts command-line/CCU background tasks.
7. Spawns bosses via `_HunrProvision.boss.BossManager.bornBoss()`.
8. Opens scheduled map events through `MapManager`.
9. Starts `MapManager` thread.
10. Accepts TCP clients while `start` is true.
11. Applies IP/session limit and maintenance gate.
12. Creates `Session` per accepted socket and registers it in `SessionManager`.

### Java Session Flow

`Session` owns socket streams, `MessageHandler`, `Service`, a sender thread, a collector thread, and a heartbeat task.

Client connection flow:

```text
TCP accept
  -> new Session(socket, ip, id)
      -> MessageHandler.onConnectOK()
      -> Service
      -> start MessageCollector
      -> schedule Heartbeat
      -> SessionManager.addSession

client GET_SESSION_ID
  -> Session.generateKey()
  -> Session.sendKey()
  -> encrypted protocol mode starts

client CLIENT_INFO
  -> Session.setClientType()
      -> validate zoom/device/version
      -> send list server/resource headers/DLL list
      -> send matrix challenge

client MATRIX_CHALLENGE response
  -> Session verifies challenge

client LOGIN
  -> Session.login()
      -> User.login()
      -> repository lookup
      -> send resource/version/player data responses
```

### Java Command Flow

`Session.MessageCollector` reads messages and dispatches to `MessageHandler.onMessage(...)`.

`MessageHandler` switches by command group:

- not logged in: login, client info, register
- not in map: update map/skill/item, request map template, create player
- in map: movement, map change, attack, item use, shop, clan, chat, trade, task, special skill, voice, etc.

Most command handling delegates directly into `Player` methods or `Service` response methods. `Player` methods often update domain state, write DB state, and send network responses in the same call path.

### Java Zone/Map Flow

Each `Zone` thread ticks every 100 ms:

- update all players in the zone
- update satellites roughly every 10 seconds
- update mobs every 1 second
- update map items
- respawn mobs after `Mob.DELAY_RESPAWN`

`MapManager` runs a separate 1 second loop for map-level event objects. `MainUpdate` runs every 100 ms for global events and scheduled daily tasks.

### Java Save/Shutdown Flow

- `AutoSaveData` sleeps for `config.delayAutoSave`, then calls `server.saveData()`.
- `server.saveData()` saves sessions, clan data, and consignment data.
- Player logout calls `Player.saveData()`, leaves the zone, writes info-client data, and marks player offline.
- `ServerMaintenance` broadcasts countdown messages, closes sessions, calls `server.saveData()`, marks players offline, closes maps/server, and exits the JVM.

## Explicit Uncertainties

- The Maven build file and Spring application properties were not present, so dependency versions and actual production property values could not be verified from source.
- The requested SQL folder defines only the Bo Mong schema. Java references many other tables, but their field definitions are outside the requested schema source.
- The runtime behavior of the real deployed DB depends on tables/data not fully represented by `HUNR_Server_Java/Hunr2026/sql`.
- Some Java strings are mojibake in the checked-in source; documentation avoids interpreting user-facing text.

