# HUNR2026 combat rules: source-traced calculation core

`CombatRules.h/.cpp` evaluates integer battle snapshots. It returns updated values, ordered semantic events, and delayed-effect recipes. It has no network, persistence, thread, or global clock dependency. `hunr2026Rules()` registers `legacy.mob`, `legacy.player`, and `legacy.nonfocus`; another `RuleProfile` can register different mechanics without editing the old formulas. No new balance numbers are supplied: `SkillTemplate`, `SkillLevel`, actor attributes/options, map flags, and target state come from an importer/adapter.

This is a calculation and decision core, **not a claim that the entire Java server is ported**. An adapter must commit the returned snapshots and implement the returned lifecycle/reward/effect events. The demo `World` remains a different ruleset. `Applied` means the represented branch was evaluated; it does not mean an external database, client, scheduler, summon AI, or reward system executed it.

## API and deterministic inputs

`AttackContext` carries the selected template/level, actor-owned `SkillRuntime`, explicit milliseconds, current actor/target snapshots, and `random(minInclusive,maxExclusive)`. Supply resolved stats from the same point of the original `Info.setInfo()` pipeline. Do not substitute base stats for final stats. The C++ core preserves draw order; no global RNG is used. Java `Utils.nextLong(min,max)` chooses below `max`, while its equal-bound guard returns `max` without a draw. Damage variation is therefore `[damage-damage/10, damage)`, not an inclusive “90–100%” integer roll.

`CombatOutcome::Rejected` identifies admission failures. `Unsupported` identifies a missing rule, invalid/overflowing arithmetic domain, absent required area data, or an unported context. Both return original snapshots with no mutations/events. Random callback state may already have advanced if an unsupported condition is detected after a draw. Callbacks must enforce the requested half-open domain; out-of-domain draws become `Unsupported`.

`BattleActor::unsupportedMechanics` is a fail-closed adapter boundary: populate it for active systems that the snapshot does not represent, such as an attached attacking summon, subclass-specific injury/death behavior, or special `dameSTC` target mutation. Omitting such state while calling the generic rules would not establish Java parity. Player-versus-player permission is an explicit `playerTargetAllowed` input; the old flag, clan, ownership, tournament, map and disciple eligibility functions are not guessed from team IDs.

## Source references and represented branches

The source root in this repository is `Sample game old/HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/`.

| Source | Represented behavior |
| --- | --- |
| `util/Utils.java:505` | Integer random domains and floor Euclidean distance at line 523. Positions stay in the Java short domain. |
| `user/Player.java:16313` | Ordinary player attack admission: dead/frozen/asleep/held/stone; blindness alone is not this gate. |
| `map/tzone/Zone.java:1302` | Mob admission, immortal charm at one HP, pet-map restriction, maximum-MP percentage costs, special/boss exemptions, use-alone rejection, cooldown. |
| `map/tzone/Zone.java:1340` | Mob miss/critical draw order; first-hit critical consumption; sleep, teleport/blind, chocolate early branches. |
| `map/tzone/Zone.java:1413` | Ordered base skill damage, variation, option 19, powerful charm, Kakarot/Songoku sets, disciple charm, special-skill modifiers, critical, next-damage bonus. |
| `map/tzone/Zone.java:1459` | Makankosappo current-MP damage; spirit-bomb zone HP/attack formula; OcTieu combo modifier. |
| `map/tzone/Zone.java:1480` | Option 111 miss, training dummy ID 0, range plus 50, full-health one-shot prevention, template 70 override. |
| `user/Player.java:16496` | `useSkill` Kaioken HP gate/payment and Hold branch; returned hold recipe requires a lifecycle adapter. |
| `map/tzone/Zone.java:1519` | Cost/cooldown/stamina, boss-mob 10% cap exemptions, damage/life steal, experience input stage and death events. |
| `map/tzone/Zone.java:1265` | Spirit-bomb total: current HP of human players plus all zone mobs. Adapter supplies the complete total and mob list. |
| `map/tzone/Zone.java:788` | Ordinary PvP wake-up grace, distinct miss/critical and control behavior, options/sets, reflection, protection, and range ordering. |
| `user/Player.java:3203` | Base `injure`: positive damage passes through; specialized boss subclasses are excluded explicitly. |
| `user/Player.java:2863` | Nonfocus recovery update/stop, charge, transform, summon, and energy-protection branches. |
| `user/Info.java:177` | Stamina decrement clamps at zero. |
| `user/Info.java:990` | Resource recovery truncates percentage and caps at maximum. |

The normal mob core includes attack damage for Dragon, Demon, Galick, Kamejoko, Masenko, Antomic, Kaioken, Lien Hoan, generic Danh/Chuong, teleport, Makankosappo, and spirit bomb. Sleep/chocolate/hold take their separate source branches. The ordinary PvP core additionally models option Kame/Laze, beam multiplier timing, absorbed beams, Cell armor, low-MP reduction, critical resistance, damage limits, reflection, and protection. Equipment options not consumed by these methods are expected to have already affected the resolved stats where appropriate.

The calculated experience event is `add_power_and_potential_input`, the value passed to Java `addExp(POWER_AND_POTENTIAL, ...)`. It includes the represented map branch, innate bonus, flag, options 101/155/88/83, and satellite 343 count. Later progression limits, multipliers, sharing, quest updates, loot, achievements, and persistence belong to the reward adapter; they are not implemented by this event.

## Preserved source details that differ from the new demo

- Mob misses use damage `-1`; out-of-range attacks use zero, still paying mana/stamina and starting cooldown. A mob at full HP cannot be killed in one ordinary hit; a special attack bypasses that prevention. The boss-mob cap applies after that check.
- Template 70 recomputes damage **after** miss and range checks. The source override can therefore cause damage even when the earlier range check set it to zero.
- Mob sleep and chocolate return before the normal cooldown tail. PvP sleep adds a second strict `elapsed > cooldown` condition, distinct from the main `elapsed >= cooldown` admission.
- Teleport computes range against the original positions, then moves the actor; it does not recompute the distance for the later check.
- Makankosappo sets current mana to one before the ordinary cost tail. If invoked without the special flag, the resulting mana can be negative. The normal charged route's zero cost avoids this; the core preserves the represented Java branch rather than silently clamping it.
- Spirit bomb can hit a surviving primary mob again in the area loop because the Java loop includes that mob. `completeZoneMobs` and `totalZoneHp` are mandatory; no synthetic area total is used.
- PvP reflection happens before miss/range filtering. The PvP energy shield uses a strict `damage > maxHp` break check and repeats its branch after range filtering, unlike the separate explosion-related `Player.java:3144` branch. Consequently that second PvP shield branch can restore one damage after an out-of-range result. These are preserved ordering details, not recommended mechanics for a new ruleset.
- `xuyenGiap` consumes its chance draw in the Java PvP method, but that local boolean is never subsequently read there. The port consumes the same draw; it does not invent a defense subtraction.
- Arithmetic rejects unrepresentable results instead of reproducing Java signed wraparound. Signed percentage reductions preserve Java truncation toward zero before addition.

## Effect recipes and boundaries

Recipes contain `kind`, target ID, duration in milliseconds, optional delay, magnitude, and optional template ID. `sleep`, `blind`, `chocolate`, and `hold` describe the corresponding target state/lifecycle. `energy_protection` is the old protection state, **not** an absorption pool. `summon` carries exact level-based max HP, lifetime, and mob template ID; a separate event carries the summon damage percentage. The adapter must also use the Java initial position `(caster.x, caster.y-40)`, status/ownership, AI and cleanup rules.

Transform produces a recipe after 3000 ms with an alive/zone check requirement, the exact duration and CaDic multiplier, plus a charge-clear recipe. The adapter must execute the original skin/stat/body updates and 50% resource recovery at the delayed stage. The core does not apply an isolated multiplier to already-modified final stats. Charging Makankosappo/spirit bomb produces charge-clear at 3000 ms and special-clear at 4000 ms; cancellation and target dispatch remain scheduler responsibilities.

Explicitly unsupported: complete area healing/revival (`Tri Thuong`), solar-flare area eligibility, whistle area buffs, explosion, boss-player subclasses and their custom injury rules, arbitrary additional nonfocus actions, PvP auto-play death teleport, and systems marked by `unsupportedMechanics`. Full server equivalence also requires the legacy adapter to consume rewards, effect expiry, death, mob retaliation, companion attack, and protocol events. A custom `RuleProfile` can add implementations incrementally; an unknown key never falls back to the demo or ordinary damage.

`tests/LegacyCombatTests.cpp` has deterministic source-domain draws, boundary damage/cost checks, preserved source edge cases, unsupported-result checks, and registry extension checks. No project build or source execution was performed in this task.
