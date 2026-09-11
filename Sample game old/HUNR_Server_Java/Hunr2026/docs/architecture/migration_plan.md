# Migration Plan

## Keep / Refactor / Replace / Rebuild

### Keep

- Java source as the behavior oracle until each C++ slice is verified.
- DB table names and JSON shapes that are actually loaded by Java code.
- Existing game assets under `resources`/`data` where directly used by map/resource flows.
- Legacy protocol command IDs and message shapes, but isolate them in a codec.
- C++ CMake/test structure in `AH_BE_NRO_SERVER_C++`.
- C++ `ContentRegistry` and `IGameRepository` direction, with refinements.
- C++ `EnvLoader` idea, without relying on checked-in secrets.

### Refactor

- C++ `WorldService`: split command handling, world state, player service, inventory, combat, tasks, events, and persistence save policy.
- C++ `GatewayServer`: split socket accept, session state, binary codec, text/debug codec, resource download, and auth.
- C++ `MySqlContentLoader`: split into table loaders and central JSON parsers with validation tests.
- Java-derived content loading: make a C++ `ContentValidator` that verifies references after loading instead of allowing null/fallback behavior to hide data problems.

### Replace

- Java static singleton access (`DragonBall`, `GameRepository`, `MapManager`, `SessionManager`) with explicit C++ ownership and dependency references.
- Java `Zone extends Thread` with owned `Zone` objects updated by a world loop or explicit shard loops.
- Static `MySQLConnect` with an explicit DB/connection pool abstraction.
- Raw SQL helper methods that accept composed SQL strings.
- Compile-time feature flags for operational modes/events with DB/config-backed flags.
- Startup source-code backup side effects inside game server initialization.

### Rebuild

- Legacy protocol codec and session state machine.
- Content load/validation from DB and map resources.
- Login/select/create character path.
- Map/zone entry and movement.
- Mob spawning/respawn and basic combat.
- Inventory/item option/stat calculation.
- Persistence save/autosave with transactions.
- Event/ranking/social systems only after core world behavior is stable.

## Minimal Migration Order

1. **Build and source contract**
   - Record exact Java runtime requirements that are missing from source: Maven POM and `application.properties`.
   - Freeze table/JSON contracts from actual SQL/entity/code evidence.
   - Add C++ tests around parsing before gameplay behavior.

2. **C++ config and content loading**
   - Load `.env`/environment.
   - Load MySQL content tables already used by C++: `nr_map`, `nr_mob_template`, `nr_npc_template`, `nr_skill`, `nr_item`, `nr_item_option_template`, `nr_task`, `nr_others`.
   - Parse `nr_map` JSON fields and resource map files.
   - Validate references and report missing tables/fields explicitly.

3. **Read-only resource/version handshake**
   - Implement the legacy socket handshake, session key, client info, version/resource responses, and DLL list path.
   - No gameplay mutation yet.

4. **Account and character vertical slice**
   - Implement login using `nr_user`.
   - Implement select/load character using `nr_player`.
   - Implement create character only after confirming exact default data and schema.
   - Keep save disabled or write to a test DB until serialization is verified.

5. **Single-map entry slice**
   - Load one normal map and its zones.
   - Enter character into a zone.
   - Send map info with mobs/NPCs/items/background positions.
   - Support movement with server-side position updates.

6. **Mob/combat slice**
   - Spawn mobs from `nr_map.mob` plus `nr_mob_template`.
   - Implement one or two verified skills first.
   - Apply damage, cooldown, MP cost, death, and respawn.
   - Compare against Java behavior in controlled cases.

7. **Inventory/stat slice**
   - Load item bag/body/box JSON.
   - Implement `AttributeSet` and item option resolution for the minimum options used by the first combat slice.
   - Add item use for one consumable and one equipment path.

8. **Persistence slice**
   - Implement transaction-backed save for character state.
   - Add autosave policy.
   - Add logout/offline state.
   - Verify no partial save corrupts JSON fields.

9. **Shop/drop/economy slice**
   - Load shop tables used by the selected flow.
   - Implement buying/selling and item drops.
   - Add audit logging before enabling currency-changing operations.

10. **Social/events/ranking slices**
    - Add clan, friend, trade, tops, Bo Mong, seasonal events, bosses, and bots one at a time.
    - Each feature should have a table contract, owner system, command handlers, and tests before porting more feature code.

## Safest Vertical Slices To Rebuild First

### Slice 1: Content Loader Validation

Why it is safe:

- Read-only.
- Does not affect live players.
- Catches missing schema/resource assumptions early.

Expected output:

- Counts for maps, mob templates, NPCs, skills, items, item options, tasks, config.
- Validation errors for missing referenced templates/resources.
- No invented DB fields.

### Slice 2: Legacy Handshake And Resource Header

Why it is safe:

- Exercises socket/protocol compatibility without persistence mutation.
- Keeps client compatibility work isolated.

Expected output:

- Client connects.
- Session key exchange works.
- Client info/version/resource responses are sent.
- Server rejects unsupported client state clearly.

### Slice 3: Login And Character Snapshot

Why it is safe:

- Can be read-only at first.
- Verifies account, password, activation/status/lock handling.

Expected output:

- Authenticated account.
- Loaded active character.
- Server can produce a textual/debug and legacy snapshot.

### Slice 4: One Map, One Zone, Movement

Why it is safe:

- No economy or combat writes.
- Exercises map data, zone ownership, and player presence.

Expected output:

- Character enters a loaded map.
- Server sends map info.
- Movement updates position and broadcasts state.

### Slice 5: Basic Mob Combat

Why it is safe:

- Bounded to one map and a small skill set.
- Good place to prove `AttributeSet`, `CombatSystem`, mob runtime IDs, and respawn.

Expected output:

- Mob spawn from DB/resource data.
- Attack command applies cooldown/MP/damage.
- Mob death/respawn behavior is deterministic enough to test.

### Slice 6: Bo Mong Read/Progress

Why it is safe:

- The requested SQL folder directly defines Bo Mong schema.
- It is feature-contained compared with broad player inventory/economy behavior.

Expected output:

- Load Bo Mong config tables.
- Read/update a test player's Bo Mong task progress.
- No reward generation until item/stat systems are ready.

## Cross-Cutting Rules During Migration

- Use Java as a trace oracle, not as an architecture template.
- Port behavior by vertical slice, not by package copy.
- Do not add C++ schema fields unless an explicit SQL migration is reviewed.
- Do not write to production-like data until read/parse/save round trips are tested.
- Keep command codec, domain mutation, and persistence separate.
- Every new feature gets:
  - owner subsystem
  - input command or scheduled trigger
  - state it owns
  - repositories it may call
  - outbound events it emits
  - tests for at least the core happy path and one failure path

