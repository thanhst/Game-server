# Replaying the C++ legacy runtime without client resources

`runReplay(shared_ptr<const ContentSnapshot>, scenarioJson)` creates a real `LegacyWorld`, loads declared actor/mob snapshots, and executes operations through the same public world methods as a host. It returns JSON with per-operation outcomes/events, actual random draw domains/values, and final authoritative actors, mobs, summons, cooldowns and scheduled effects. It does not mock combat or require sprites, audio, a database server, or the old Java client.

After building manually, the application entry point is:

```powershell
.\GameServer.exe --replay-legacy content/hunr/content.json content/hunr/replay.example.json
```

Run from the repository directory or pass absolute paths. The example includes two actors, a mob, ordinary attacks, a rejected cooldown retry, an energy shield, an owned summon with follow-up damage, sleep and expiry. Its actor/mob stats are declared test snapshots, not claimed production starting balance. Skill definitions/costs/cooldowns come from the retained SQL content.

## Input format

The required top-level fields are `format: "hunr-legacy-replay"`, `version: 1`, `map_id`, `start_time_ms`, `actors`, `mobs`, `random` and `operations`. An optional `note` documents a scenario. Unknown keys and malformed values are rejected. Numbers must be JSON integers within the target integer domain; no floating-point resource conversion occurs. Limits are 4096 actors plus mobs, 10,000 operations, 100,000 random draws, and 256 skills/options per actor.

Each actor has `id`, `class_id`, `selected_skill_id`, a `learned` array of `{skill_id, point}`, and `state`. The world validates class/skill/level references against actual imported content. Optional `cooldowns` is an array of `{skill_id, last_use_ms?, override_ms?}`; omit a timestamp to represent a never-used skill. Duplicates are rejected. Normal mobs have `id`, `template_id`, and `state`; player and mob IDs may have the same numeric value because their registries are separate.

Every `state` explicitly supplies `hp`, `max_hp`, `mana`, `max_mana`, `damage_full`, `critical_full`, `stamina`, `x`, and `y`. These are resolved scenario values. Additional snapshot fields use the snake-case names listed in `LegacyReplay.cpp`'s member-field tables, including status flags, set/charm flags, special-skill state, target protection/armor and timestamps. `options` is an array of `{id, value}`; `unsupported_mechanics` is an array of strings marking source systems that the core cannot represent. Absent optional flags use neutral struct state, with `human=true` for players, `human=false` for mobs, and `can_reflect=true`. This is a controlled scenario format, not a complete persisted-save importer: supplying an initially active status does not fabricate a matching historical expiry timer.

`random` must contain exactly one of:

- `draws`: an array of `{min, max, value}`. Each call must match the next recorded half-open domain exactly. `min <= value < max`. A missing draw or domain mismatch reports `unsupported` for that operation without committing it.
- `seed`: a nonnegative uint64 value. The runner uses `mt19937_64` and an explicit rejection-sampling mapper, avoiding implementation-specific `uniform_int_distribution` mapping. This provides reproducible C++ scenarios; it does not claim to reproduce Java `Math.random()` from the same numeric seed. Scripted draws are the format for an observed cross-runtime comparison.

The result records every fulfilled random draw. Unused scripted draws are reported; rejected operations may consume none, while unsupported operations may consume some before the unsupported condition is discovered. Random state is external to the world's mutation transaction.

## Operations

Every operation requires `op` and nonnegative `at_ms`. The runner advances the world to that absolute time first, processing due effects. A backward timestamp is a rejected step; the action is skipped. A later action can then continue from the unchanged clock.

| `op` | Additional required fields | Optional fields |
| --- | --- | --- |
| `advance` | None | None |
| `select_skill` | `actor`, `skill_id` | None |
| `attack_mob` | `actor`, `target` | `summoned_target`, `summon_owner_target_allowed` (both default false) |
| `attack_player` | `actor`, `target`, `pvp_allowed` | None |
| `nonfocus` | `actor`, `action_type` | None |
| `move` | `actor`, `x`, `y`, `flight` | None |
| `remove_actor` | `actor` | None |

Nonfocus action IDs remain the source action IDs; for example, 8 summons and 9 applies energy protection. PvP permission is declared explicitly for a scenario, rather than inferred from transport IDs. `attack_mob` with `summoned_target=true` addresses a summon by its owner ID and requires owner-target permission.

The runner installs no approximate stat resolver, terrain grid, movement rule, or arbitrary code callback. Transform requiring a full stat pipeline and movement requiring missing terrain therefore report `unsupported`. The C++ API can install source-backed adapters separately. JSON cannot execute code, load plugins, change files, or access the network.

The trace's `applied` means the C++ world committed the represented operation. It does not mean a client, quest/reward/persistence adapter, or autonomous NPC system consumed its emitted events. See `legacy-combat.md` and `legacy-world.md` for those boundaries. The example and tests were authored and statically inspected; this task did not build or execute C++/Java.
