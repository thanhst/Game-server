# Proposed C++ Architecture

## Design Goals

- Keep Java behavior as the source-of-truth oracle during migration.
- Keep DB fields and JSON structures grounded in the actual SQL/source code; do not invent columns.
- Make ownership explicit.
- Keep protocol compatibility isolated from gameplay logic.
- Make new attributes/features data-driven where possible.
- Prefer `std::unique_ptr` for single ownership.
- Use references or raw pointers only for non-owning access where lifetime is controlled by a parent subsystem.
- Use `std::shared_ptr` only for genuinely shared async lifetime, and prefer stable IDs in queues over shared ownership.

## Proposed Subsystems

### Application/Bootstrap

Core classes:

- `ServerApplication`
- `AppConfig`
- `ServiceContainer`
- `ShutdownCoordinator`

Responsibilities:

- Parse config and environment.
- Construct owned services in dependency order.
- Start/stop services in reverse ownership order.
- Register signal/console shutdown.
- Avoid gameplay side effects during construction.

Ownership:

- `ServerApplication` owns all top-level services using `std::unique_ptr`.
- Services receive dependencies as references when lifetime is guaranteed by `ServerApplication`.

### Config

Core classes:

- `EnvConfigLoader`
- `RuntimeConfigRepository`
- `FeatureFlagService`
- `RateConfigService`

Responsibilities:

- Load process config from `.env`/environment.
- Load mutable runtime config from DB.
- Separate process settings from gameplay settings.
- Version and validate config snapshots.

Extension point:

- Runtime feature flags should be data/config, not compile-time constants like Java `ConfigStudio`.

### Persistence

Core interfaces/classes:

- `IDatabase`
- `ITransaction`
- `AccountRepository`
- `PlayerRepository`
- `ContentRepository`
- `ClanRepository`
- `EventRepository`
- `RankingRepository`
- `AuditRepository`

Responsibilities:

- Own SQL access and transactions.
- Hide MySQL C API details.
- Centralize JSON parsing/serialization for persisted player/content fields.
- Provide narrow repository methods instead of raw SQL strings in gameplay systems.

Ownership:

- `ServerApplication` owns `std::unique_ptr<IDatabase>`.
- Repositories either own no DB resources and hold `IDatabase&`, or are owned by a `PersistenceService`.

### Content

Core classes:

- `ContentRegistry`
- `ContentLoader`
- `ContentValidator`
- `ContentVersionService`
- `MapTemplate`, `ItemTemplate`, `SkillTemplate`, `MobTemplate`, `NpcTemplate`

Responsibilities:

- Load read-only template data from DB and resource files.
- Parse JSON fields once at startup.
- Validate references between maps, mobs, NPCs, skills, and items.
- Expose immutable content snapshots to runtime systems.

Ownership:

- `ContentService` owns the active `ContentRegistry`.
- Runtime systems receive `const ContentRegistry&`.

### Network And Protocol

Core classes:

- `GatewayServer`
- `Session`
- `SessionRegistry`
- `LegacyNroCodec`
- `TextDebugCodec`
- `ProtocolStateMachine`
- `CommandDispatcher`
- `ResourceDownloadService`

Responsibilities:

- Accept sockets.
- Own socket/session lifetime.
- Decode legacy binary protocol into typed commands.
- Encode server responses.
- Keep resource download/version handling out of world logic.
- Keep auth/client validation state explicit.

Ownership:

- `GatewayServer` owns `std::unique_ptr<Session>` objects in `SessionRegistry`.
- `Session` owns its socket and protocol state.
- `Session` does not own `Player`; it stores IDs and receives snapshots/responses.
- `GatewayServer` has a non-owning `WorldService&`.

### World Runtime

Core classes:

- `WorldService`
- `WorldShard` or `WorldLoop`
- `MapInstance`
- `Zone`
- `EntityRegistry`
- `WorldClock`
- `WorldCommandQueue`

Responsibilities:

- Own authoritative runtime state.
- Process commands on the world thread or on clearly partitioned shard loops.
- Own maps, zones, mobs, items, NPC runtime entities, and online character runtime state.
- Produce outbound events/responses for the network layer.

Ownership:

- `WorldService` owns `std::vector<std::unique_ptr<MapInstance>>`.
- `MapInstance` owns `std::vector<std::unique_ptr<Zone>>`.
- `Zone` owns mobs, map items, and NPC runtime entities.
- Online characters should be owned by `WorldService` or a dedicated `PlayerRuntimeStore`, keyed by `CharacterId`.
- Zones should store non-owning IDs or raw pointers/references only when the parent store guarantees lifetime for the tick.
- Commands should carry IDs, not owning pointers.

Avoid:

- `Zone : Thread`
- starting threads from constructors
- storing owning object graphs with cycles

### Gameplay Systems

Core systems:

- `AuthSystem`
- `PlayerSystem`
- `MovementSystem`
- `CombatSystem`
- `InventorySystem`
- `ItemEffectSystem`
- `SkillSystem`
- `DropLootSystem`
- `ShopSystem`
- `QuestTaskSystem`
- `ClanSocialSystem`
- `TradeSystem`
- `RankingSystem`
- `EventSystem`
- `BossSystem`
- `BotSystem`
- `VoiceChatSystem`
- `SecurityChallengeSystem`

Responsibilities:

- Each system mutates a narrow part of world state through explicit APIs.
- Systems share readonly content through `const ContentRegistry&`.
- Systems write persistence through repository interfaces at defined save points.
- Network serialization is not part of gameplay systems.

### Attributes And Feature Extension

Core classes:

- `AttributeSet`
- `AttributeId`
- `StatBlock`
- `Modifier`
- `ModifierSource`
- `ItemOptionRule`
- `EffectRule`
- `AttributeResolver`

Responsibilities:

- Represent base stats and derived stats explicitly.
- Apply item options, buffs, skills, map effects, event bonuses, and temporary effects through one resolver.
- Centralize option ID semantics.
- Support new attributes without editing `Player`, `Info`, `Item`, `Service`, and combat code together.

Suggested model:

```text
Character
  -> BaseStats
  -> Inventory
  -> ActiveEffects
  -> AttributeResolver
      -> vector<Modifier>
      -> computed StatBlock
```

Extension points:

- New item option: add an `ItemOptionRule`.
- New buff/debuff: add an `EffectRule`.
- New combat stat: add an `AttributeId` and resolver rule.
- New event bonus: add a `ModifierSource` provider.
- New feature command: add a typed command handler and system API, not direct edits to a monolithic player class.

## Ownership Model

Recommended top-level ownership:

```text
ServerApplication
  owns unique_ptr<Logger>
  owns unique_ptr<IDatabase>
  owns unique_ptr<PersistenceService>
  owns unique_ptr<ContentService>
  owns unique_ptr<WorldService>
  owns unique_ptr<GatewayServer>
  owns unique_ptr<VoiceServer> when implemented
```

Recommended runtime ownership:

```text
WorldService
  owns MapInstance objects
  owns PlayerRuntimeStore
  owns scheduler/event queues

MapInstance
  owns Zone objects

Zone
  owns MobRuntime objects
  owns NpcRuntime objects
  owns ItemDropRuntime objects
  references online players by CharacterId or non-owning pointer

GatewayServer
  owns Session objects
  references WorldService

Session
  owns socket/protocol buffers
  stores SessionId, AccountId, CharacterId
```

Use raw pointer/reference only when the owner is clear and outlives the callee:

- `WorldService&` in `GatewayServer`
- `const ContentRegistry&` in gameplay systems
- `IDatabase&` in repositories

Use `shared_ptr` only if a task must safely outlive its submitter. Prefer command queues with IDs and cancellation over shared ownership.

## Relationship To Current C++ Target

Keep or evolve:

- `CMakeLists.txt` structure with `nro_core`, `nro_server`, and tests.
- `EnvLoader` concept, after removing checked-in secrets from normal workflow.
- `ContentRegistry`, after strengthening validation and making it immutable after load.
- `IGameRepository` concept, but split into narrower repositories.
- `CombatEngine` concept, but make it consume `AttributeResolver` output.

Refactor:

- `WorldService` into smaller systems and world-state ownership.
- `GatewayServer` into accept/session/codec/resource/auth responsibilities.
- `MySqlContentLoader` into table-specific loaders with tested parsers.

Avoid using as foundation:

- Generated full-port skeleton code as authoritative architecture.
- Direct string-command behavior as the only command model.
- Java-style global singletons and process-wide mutable registries.

