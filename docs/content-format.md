# Gameplay content format

`content/demo.game` is a new, illustrative set of combat values for the C++ prototype. Its three player archetypes reference the Earth, Namek, and Saiyan class concepts in HUNR2026. The numbers, skill combinations, timings, and shield behavior are **not legacy balance parity**. The shield is a finite absorption pool; it does not reduce every hit to one damage. `visual` contains an event identifier, not an asset path. No image, sound, or other client resource is required by the simulation.

## File grammar

Files are UTF-8 text, optionally starting with a UTF-8 BOM. LF and CRLF line endings are accepted. Leading/trailing ASCII spaces, tabs, and carriage returns are trimmed. Empty lines and whole lines starting with `#` or `;` are ignored. There are no inline comments, quotes, escapes, interpolation, includes, or multiline values. Text values such as names are used literally after trimming. Numeric conversion is locale independent.

```text
section := '[' kind whitespace identifier ']'
kind := 'effect' | 'skill' | 'character' | 'map'
entry := key '=' value
identifier := [A-Za-z0-9_][A-Za-z0-9_.-]{0,127}
list := identifier (',' identifier)*
```

Section kinds, keys, identifiers, and enum values are case sensitive. Spaces and tabs may surround a section's kind/identifier and each list item. Each entry belongs to the last section. Forward references are allowed. A section ID may appear in different kinds, but cannot repeat within one kind. Keys and list items cannot repeat. A section cannot be reopened later. Unknown section kinds or keys, empty keys/values, empty list elements, and malformed headers are errors. To specify an empty optional list, omit its key.

Decimal values accept an optional `+` or `-`, a decimal point, and an optional `e`/`E` exponent: `12`, `-0.5`, `.25`, `1.`, `+2e3`. At least one mantissa digit is required. Hex numbers, NaN, infinity, overflow, unrepresentable underflow, comma decimal separators, and trailing text are rejected. Integer fields accept base-10 digits only; only `legacy_id` and `legacy_class` allow a leading minus, and their minimum is `-1`. Integer fields do not accept `+`, decimals, or exponents. Booleans are exactly `true` or `false`.

Limits are deliberate validation boundaries, adjustable in `Content.cpp`: 64 MiB per file; 16,384 bytes per line; 10,000 definitions per kind; 256 items per list or attributes per character; 128 bytes per identifier; 512 bytes per name or visual. Names/visuals cannot contain control characters. Ordinary numeric values and attributes must be finite and within `[-1e9, 1e9]`. Attributes are extensible string keys, not enum members.

## Effect sections

```ini
[effect frost_damage]
handler=damage
magnitude=10
scaling_attribute=spirit
scaling_factor=0.5
tags=frost
visual=frost_hit
```

| Key | Default | Constraints / meaning |
| --- | --- | --- |
| `handler` | Required | Identifier of a registered C++ effect handler. |
| `magnitude` | `0` | Base effect amount, within `[-1e9, 1e9]`. |
| `scaling_attribute` | Absent | Optional caster attribute identifier. |
| `scaling_factor` | `0` | Within `[-1e6, 1e6]`; nonzero requires `scaling_attribute`. |
| `attribute` | Absent | Attribute modified by `modifier`; optional identifier for other handlers. |
| `duration_ms` | `0` | Integer `0..86400000`; zero means instant. |
| `period_ms` | `0` | Integer `0..86400000`; nonzero requires duration at least this long. |
| `stacking` | `refresh` | Exactly `refresh`, `stack`, or `replace`. |
| `max_stacks` | `1` | Integer `1..100`; values above one require `stack`. Instant effects cannot stack. |
| `tags` | Empty | Identifier list; control handlers use tags such as `stunned`, `silenced`, `rooted`. |
| `visual` | Empty | Optional client-facing event text, up to 512 bytes. |

The effect amount is `magnitude + caster[scaling_attribute] * scaling_factor`; omitted/missing scaling attributes contribute zero. `modifier` applies the amount to its named `attribute`. For example, `attribute=attack` and `magnitude=12` is a temporary additive attack bonus; a negative amount is a debuff. The built-in damage, heal, periodic damage, and shield handlers require nonnegative magnitude and scaling factor.

Built-in timing constraints:

- `damage`, `heal`: duration and period must both be zero.
- `periodic_damage`: duration is positive, period is at least 50 ms and no greater than duration.
- `modifier`, `control`, `shield`: duration is positive and period is zero; `modifier` also requires `attribute`, and `control` requires at least one tag.

`refresh` refreshes an existing application; `stack` accumulates up to the declared maximum; `replace` replaces an existing application. These policies are implemented by the runtime. Unknown handler identifiers pass structural content validation, so a new handler can be added without rewriting the parser. The runtime's effect registry must confirm that each referenced handler exists before gameplay starts. A custom handler owns validation of any additional semantic restrictions; it must use the existing effect fields or intentionally extend the schema.

## Skill sections

```ini
[skill frost_bolt]
name=Frost bolt
mana_cost=8
cooldown_ms=1500
range=20
target=enemy
effects=frost_damage
```

| Key | Default | Constraints / meaning |
| --- | --- | --- |
| `name` | Required | Nonempty text. |
| `legacy_id` | `-1` | Integer `-1..INT_MAX`; metadata, not a dispatch enum. |
| `mana_cost` | `0` | `0..1e9`, or `0..100` when `mana_percent=true`. |
| `mana_percent` | `false` | Cost is a percentage when true; `10` means ten percent. |
| `cooldown_ms` | `0` | Integer `0..86400000`. |
| `range` | `0` | Finite `0..1e6`, in world units. |
| `radius` | `0` | Finite `0..1e6`, in world units. |
| `max_targets` | `1` | Integer `1..1024`. |
| `target` | `enemy` | Exactly `self`, `enemy`, or `ally`. |
| `effects` | Required | Nonempty list of existing effect IDs, applied in listed order. |

## Character sections

```ini
[character frost_guardian]
name=Frost guardian
stat.hp=130
stat.mana=90
stat.attack=14
stat.defense=6
stat.speed=7
stat.spirit=20
tags=player,frost
skills=frost_bolt
```

| Key | Default | Constraints / meaning |
| --- | --- | --- |
| `name` | Required | Nonempty text. |
| `legacy_class` | `-1` | Integer `-1..INT_MAX`; optional migration metadata. |
| `stat.<identifier>` | See below | Finite numeric attribute, within `[-1e9, 1e9]`. |
| `tags` | Empty | Identifier list; demo player archetypes use `player`, mobs use `monster`. |
| `skills` | Empty | List of existing skill IDs. |

Every character requires `hp`, `mana`, `attack`, `defense`, and `speed`. `hp` and `speed` must be positive; the other three must be nonnegative. Additional attributes such as `spirit` need no C++ enum change. New mechanics still need runtime handlers that consume those attributes.

## Map sections

```ini
[map arena]
width=500
height=500
```

`width` and `height` are required finite dimensions, both greater than zero and at most `1e6`. Map IDs identify simulation spaces; client map graphics are a separate concern.

## Loading, extension, and checks

`Content::load(path)` parses into a temporary value, checks all definitions and references, and returns only on success. Assigning its result leaves existing content unchanged if loading throws. It does not mutate a running world or implement hot reload. Parse errors report the path and line; validation errors report the definition/reference. `Content::validate()` applies the same semantic checks to definitions constructed directly in C++ and also rejects mismatched map keys/IDs and invalid enum values. A pack may omit entire categories; startup code decides which maps/archetypes it requires.

To add a data-only skill, add an effect using an existing handler, add a skill referencing it, and list that skill on a character. The frost example above introduces a new attribute, effect, skill, and character entirely through content. Add a C++ handler and register it when behavior cannot be expressed by existing handlers. IDs are not limited by a central character or skill enum. This separation supports growth, while content limits and runtime budgets remain necessary.

`tests/ContentTests.cpp` contains parser and validation cases plus a data-only extension case. Run them through the repository's test target after building manually. Tests were added without building or running the project in this task.
