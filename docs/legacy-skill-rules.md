# Selected HUNR2026 skill rules in C++

`game::legacy` contains a small source-traced rules library for a future migration adapter. It is independent of `game::Content` and the new demo combat system. The host does not yet dispatch the old Java skill protocol into this library. Recognizing all 24 old IDs does not implement all 24 skills.

## Source references

All paths below are relative to `Sample game old/HUNR_Server_Java/Hunr2026/src/main/java/com/ngocrong/` in this repository. References were read from the restored Java source in this task.

| Java source | C++ counterpart / coverage |
| --- | --- |
| `skill/SkillName.java:5` | All 24 named numeric IDs, preserved verbatim including source spellings such as `CHIEU_ANTOMIC`. |
| `skill/SkillTemplate.java:7` | Template metadata; type `1` means attack, `2` buff-to-player, `3` use-alone. |
| `skill/Skill.java:20` | Per-level row ID, point, power requirement, base cooldown, range fields, target count, mana, options, damage, price, extra text. |
| `skill/Skill.java:36` | Cooldown comparison: absolute elapsed milliseconds is less than cooldown. |
| `skill/SkillOption.java:5` | Option parameter and template identity, retained as values instead of Java object references. |
| `util/Utils.java:72` | Integer percentage, mathematically `x * p / 100`. |
| `user/Player.java:2901` | Selected-skill cooldown gate. |
| `user/Player.java:2910` | Fixed mana cost, or percentage of maximum MP for `manaUseType == 1`; bosses use zero mana. |
| `user/Player.java:3009` | Transform lasts `55 + 10 * level` seconds, multiplied by five for `setCaDic`. |
| `user/Player.java:3036` | Summon template IDs `[8,11,32,25,43,49,50]`; HP is caster maximum HP times level. |
| `user/Player.java:3051` | Summon lifetime is `55 + 10 * level` seconds. |
| `user/Player.java:3060` | Energy shield duration is `15 + 5 * (level - 1)` seconds. |
| `user/Player.java:3144` | In this protected-hit branch, incoming damage at least maximum HP breaks the shield, then damage becomes one. |
| `user/Info.java:594` | Inspected transformation stat stage: HP/MP doubled, attack gains `5 * level` percent, speed gains two. This stage is **not ported** because its placement within the full stat pipeline matters. |

The source-defined seven summon levels and skill-book levels bound the selected duration/summon helpers to levels 1–7. They throw outside that range; new gameplay can extend its separate content schema without changing the legacy contract.

## Definition and runtime ownership

`SkillTemplate` contains class metadata and `SkillLevel` values. The template ID identifies the named skill; the level row ID identifies its database/protocol record. `SkillLevel::point` is its level number. `SkillTemplate::validate()` rejects invalid IDs, duplicate level points/row IDs, levels above `maxPoint`, negative cooldown/mana/range values, and invalid sizes. Partially loaded level lists are permitted; `level(point)` throws when that level is unavailable. No numeric balance table is invented by this library.

Keep templates/levels immutable after loading and give each actor a separate `SkillRuntime`. Runtime state contains the last-use timestamp and an optional actor-specific cooldown override. A cooldown adjustment therefore does not mutate another player's shared definition. All time inputs are explicit, enabling tests and future clock integration without calling the system clock inside the rules.

`SkillRuntime::recordUse()` records time only. The caller must perform mana, target, power, learned-level, status, and other admission checks before recording a successful use. `manaCost()` calculates cost only and does not spend mana. It validates its arithmetic operands; importers must separately validate the whole template and level relationship.

## Exact rules and intentional safety differences

- `manaUseType == 1` uses a percentage of **maximum** mana; every other value is fixed, as in the Java branch. Percentages use integer truncation: 15% of 999 is 149. Boss exemption is exposed as an explicit argument.
- `percentOf()` splits the product before multiplying. It returns the same mathematical integer result for nonnegative values while avoiding an intermediate `int64` overflow when the final result fits. It rejects negative operands and an unrepresentable final result. `summonHp()` also rejects overflow instead of reproducing Java signed wraparound.
- A cooldown finishes exactly at its boundary. After recording 5000 ms with a 1000 ms cooldown, 5999 is blocked and 6000 is ready. The legacy absolute-time comparison is preserved explicitly: 4001 is also blocked and 4000 is ready. This means a sufficiently large backward wall-clock jump can make a skill ready, matching the old comparison; a future adapter should choose and document its clock policy. The new `World` uses its own monotonic simulation clock.
- Timestamps must be nonnegative, allowing safe absolute subtraction across the entire nonnegative `int64` domain. An unset runtime timestamp means “never used” and is ready, including at timestamp zero. This intentionally improves on the Java default-zero field for simulated clocks; callers restoring a saved zero timestamp can explicitly set `lastUseMs=0` to reproduce that comparison.
- Duration helpers return **seconds**, while level cooldowns and runtime timestamps use **milliseconds**. Shield levels 1 and 7 last 15 and 45 seconds. Summon/ordinary transform levels 1 and 7 last 65 and 125 seconds.
- `protectedHit()` implements only the `isProtected` branch at `Player.java:3144`: `{damage=1, breaksShield=incomingDamage>=targetMaxHp}`. It deliberately preserves that branch's result of one even for zero input. The caller owns the protection flag, break/expiry events, and subsequent `injure()` behavior. This is not the demo's finite shield absorption pool.

## Work still required for a complete game port

The library does not load SQL/JSON level rows, decode legacy packets, implement skill learning/books, spend or restore actor resources, schedule Java callbacks, or replicate client visual opcodes. It does not implement complete targeting, PvP eligibility, disciple ownership, special-skill discounts, item/set modifiers, transformation stat recalculation, summon AI/damage, death/expiry behavior, or the whole damage/defense pipeline. Other skill-specific behaviors remain in the retained Java source. In particular, `Info.setInfo()` has many ordered modifiers; applying its isolated monkey multiplier directly to the new demo's final stats would not establish legacy parity.

`tests/LegacySkillTests.cpp` provides source-rule boundary cases, integer overflow cases, and independent actor cooldown checks through `runLegacySkillTests()`. The tests were authored and source-reviewed; the project was not built or executed in this task, per the user's instruction.
