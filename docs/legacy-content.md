# HUNR source content and Java cache compatibility

`content/hunr/content.json` contains the actual definitions from the reviewed
`Sample game old/HUNR_Server_Java/Hunr2026/sql/content.sql`, with their schema from
`schema.sql`. It is separate from `content/demo.game`. No values were rebalanced.

| Definition | Imported count |
| --- | ---: |
| Schema tables, including metadata-only live tables | 102 |
| Static tables carrying rows | 52 |
| Static rows | 9,302 |
| Class-scoped skill templates | 29 |
| Skill level entries / distinct level IDs | 191 / 156 |
| Items / item option templates | 1,579 / 248 |
| Maps / mobs / NPC templates | 185 / 82 / 76 |

The importer accepts only the reviewed 52-table allowlist. `nr_others` accepts
only `avatar`, `shop`, `tile_set`, and `open_power`. It never opens the original
live SQL dump, connects to a database, or imports player/account rows. Schema
metadata for those tables is retained because persistence needs the layout.

## Reproduce and verify

These are source processing commands, not a project build or server execution:

```powershell
python scripts/import_legacy_content.py --self-test
python scripts/import_legacy_content.py
python scripts/import_legacy_content.py --check
```

The importer checks `schema.sql` and `content.sql` SHA-256 against the reviewed
`sql/source-content-manifest.json`, checks table/row counts, then embeds the input
hashes in the snapshot. `--check` regenerates the document in memory and compares
it with the committed output without changing it. If the source SQL is edited,
review and regenerate the prepared SQL manifest before importing again.

The current SQL hashes are:

```text
schema.sql  1cdd1099d265b17c5bcadccd8b2db8037e3b195811ea74c3bdf57082a594804e
content.sql 6e9085e16a5f49d59ab50b5149cb1ba6db2f84f0956f0fff13ef2c8f7b5f73fe
```

The parser handles multiline statements, quoted semicolons, doubled quotes,
MySQL backslash escapes, NULL, positional and explicit-column literal INSERTs,
and multi-row VALUES. SQL expressions, executable comments, unexpected table
names, duplicate primary keys, partial rows, and mismatched column counts fail.

## Snapshot representation

Each table has `rows`, `embedded_json`, and `json_normalizations`. Arrays have
the same row order as the source INSERTs. Column names retain SQL spelling.
`rows` preserves SQL scalar types and the exact decoded SQL text; JSON stored in
a SQL text field remains text there. `embedded_json` holds parsed containers for
those fields, at the same row index. Unknown fields, paint definitions, recipes,
shops and task definitions remain accessible without an enum or C++ schema edit.

Integers are never converted through a double. Integers outside signed/unsigned
64-bit storage use `{"$sql_integer":"..."}`. Decimal values use
`{"$sql_decimal":"..."}` so their exact value survives nlohmann/json loading.
These tags can occur recursively inside embedded JSON. The SQL column's declared
type, nullability, primary key and original column definition are in `schema`.

The Java source accepts some content that strict JSON would reject. Maps 171,
174 and 176 contain a trailing comma in their mob arrays. Only their parsed
`embedded_json` representation removes that comma; the original string remains
unchanged and `json_normalizations` records the conversion. Duplicate object keys
and non-finite numeric values are not silently accepted as valid parsed content.

## C++ ownership and lookup

`game::legacy::ContentSnapshot::load(path)` creates and validates a complete
immutable definition snapshot. `table(name)` returns the raw rows;
`embeddedRow(name, index)` returns parsed JSON. Typed registries expose maps,
items, mobs, NPCs and skill templates through const accessors. A world can own a
`shared_ptr<const ContentSnapshot>` and retain it while another snapshot is
loaded and validated. Actor cooldowns and other mutable state do not belong here.

Skills use `(classId, skillId)` keys. Shield skill 19 occurs in classes 0, 1, 2,
3, 5 and 6, with repeated level IDs 121..127. `skillLevel(classId, levelRowId)`
therefore also requires class ID. Class 4's DANH and CHUONG actually have point
zero (rows 105 and 106); they are preserved. Kaioken's source array is ordered
points 2..7 followed by point 1; it is preserved for packet compatibility.

The C++ loader checks schema/row shape, duplicate keys, numeric ranges and typed
references. It preserves Java's explicit `(short)` conversion for level damage
and price. The 594 item options containing quoted `id`/`param` integers follow
`JSONObject.getInt` coercion in the typed item view; original strings remain in
the snapshot. The importer verifies 1,340 item-option references, 949 map-mob
references, 171 map-NPC references and 249 waypoint destinations.

`--check` is the integrity check for the generated raw/parsed representations;
do not hand-edit only one representation. New game's definitions should be a
separate content package/schema adapter, retaining immutable snapshot ownership.

## Java wire caches

`LegacyCaches::build(snapshot, CacheVersions{data, map, skill, itemBase})` produces
logical packets ready for the HUNR frame codec:

| Method | Command | Payload |
| --- | --- | --- |
| `versionPacket()` | -28 / subcommand 4 | Versions and full int64 power progression |
| `mapPacket()` | -28 / subcommand 6 | Map names, NPC menus/appearance, mob definitions |
| `skillPacket()` | -28 / subcommand 7 | Skill options and all 7 class skill lists |
| `itemPacket(0/1/2)` | -28 / subcommand 8 | Item options and item definitions split at 800 |
| `itemPacket(100)` | -28 / subcommand 8 | Head animation groups |
| `dataPacket()` | -87 | Length-prefixed dart, arrow, effect, image metadata, part, skill-paint caches |

These caches use DataOutputStream **modified UTF-8**. Ordinary live messages in
this Java source use FastDataOutputStream's standard UTF-8. Cache byte/short
numeric fields preserve Java's low-bit encoding. Definition counts exceeding
the client's field capacity fail with an explicit error; changing the new
game's protocol is the way to remove those wire limits.

Versions are deployment configuration, not SQL content. No original version is
guessed from missing configuration. Effective item version is Java's byte of
`abs(itemOptionCount + itemCount + configuredItemVersion + headGroupCount)`.

Java loads item templates into a default HashMap keyed by integer item ID. The
serializer reproduces non-tree bucket iteration with insertion-order resize
semantics. For the supplied 1,579 IDs (0..2188), final capacity is 4,096, each
bucket contains one item and iteration is ascending ID. Tree-bin cases in other
datasets fail explicitly because their JVM traversal must be profiled. Other
caches preserve the SQL row order. The Java SQL queries have no `ORDER BY`;
equivalence to a differently ordered live database still requires capture-based
comparison against that deployment.

Source references: `Server.java:setCacheSkillTemplate` (517), `setCacheItem`
(566), `setCacheMap` (498), `setCacheDart`, `setCacheArrow`, `setCacheEffect`,
`setCacheImage`, `setCachePart`, `setCacheSkill`; `TMap.createData`,
`Npc.createData`, `Mob.createData`; `Service.updateData/updateMap/updateSkill/
updateItem/sendVersion` (2889..3017). `Part.java` fixes frame counts by type
(3/17/14/2); surplus source frames are retained in JSON but omitted from the
legacy packet exactly as its loader does.

## External collision grids and media

`TMap.loadMapFromResource` reads `resources/map/<mapID>`: unsigned tile width,
unsigned tile height, then tile bytes. Optional `resources/map/block/<mapID>`
marks blocked entries where the byte equals 1. Tile size is 24 pixels;
`nr_others.tile_set` supplies tile classification. The supplied checkout has no
`resources/map` directory, and the latest source ZIP contains only Java files.
Map width/height, floor locations and collision grids cannot be reconstructed
from spawn coordinates, so none are fabricated.

The user intentionally removed the map/resource files and requested source-only
delivery. They remain external deployment inputs; this implementation does not
recover or add resource binaries. `Service.requestMapTemplate`
uses -28/subcommand 10 with the original map bytes; the extended form uses
subcommand 11, int map ID, then those bytes. Sprite/audio media remains external.

`TileSetCatalog::fromContent(snapshot)` loads all 33 actual tile classifications.
`MapGeometry::load(externalMapDirectory, mapId, tileSetId, tileSets)` reads the
external `<mapId>` and optional `block/<mapId>` paths at deployment time. It
validates dimensions/lengths, applies Java's bitwise-OR tile masks, calculates
pixel dimensions, preserves column-major floor order, and provides both normal
and extended map-template packets. `fromBytes` supports in-memory providers and
small source-code test fixtures. A `tile_id` of zero means Java does not load a
grid for that map; callers must skip geometry loading for that definition.

The external loader rejects missing required map files rather than inventing a
grid. A file is capped at 1 MiB; unsigned one-byte dimensions permit at most
65,025 grid cells, and surplus bytes are retained for exact template responses.
Only block bytes equal to 1 are blocked; absent/truncated optional block data
leaves the remaining entries unblocked, matching Java.

Legacy accessors preserve flattened-array indexing and integer-division
behavior, including Java's out-of-storage sentinel 1000. Network movement must
first call `containsPixel`, which enforces actual rectangular bounds. The
explicit `legacyCollisionLand` preserves the Java `y >= width` cutoff bug and
short arithmetic for compatibility. A new game should select its own collision
policy. `closestFloor` preserves Java's truncated distance and first-on-tie
selection; blocked floors remain in the list as in the source.

## Validation boundary

The Python importer fixtures and full SQL generation/reference validation have
been executed. C++ content/cache fixtures are included in the source test suite
but have not been built or run, following the user's no-build instruction.
There has been no live Java packet capture or old-client interoperability test.
These definitions and serializers alone do not implement accounts, gameplay,
world state or persistence.
