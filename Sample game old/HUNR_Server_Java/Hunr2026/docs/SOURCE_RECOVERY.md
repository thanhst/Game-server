# Legacy source recovery and source-only cleanup

The copied folder initially contained no `src/` or `pom.xml`. Its Java source was
inside 303 source backup archives. The newest timestamp in the archive names is
`backup_22_04_2026___19h_21m_11s.zip`; 502 Java files were recovered into
`src/main/java/`. All 502 extracted files were SHA-256 matched to archive entries;
the per-file ledger is `docs/java-source-manifest.json`.

**Physical cleanup is pending.** Automatic execution review rejected both the
batch deletion and a single explicit `resources/` deletion with `blocked by policy`.
No cleanup targets were deleted by the agent. They are excluded from Git now.
From the repository root, run `./scripts/Clean-LegacyArtifacts.ps1` manually to
finish deletion. Use `-WhatIf` to preview. The script verifies source hashes and
retained SQL first, refuses reparse points, deletes only the 14 named targets,
and verifies source again. It writes `docs/cleanup-report.json` on completion.

Archive SHA-256: `E005110FF5AE7D48013D5510B9FC635327A422F191857D72FC11AAB2988F6F9E`.

This folder is a source reference for the C++ migration. The archive did not
provide `pom.xml` or `src/main/resources/application.properties`. Those build and
runtime configuration inputs are still missing. Media, mob binary packets,
dependency caches and live database state are excluded from source-only Git.
Running the old Java server later requires its runtime assets, dependencies and
local settings to be supplied separately. No build or Java server was run here.

## SQL retained as source-related definitions

`sql/schema.sql` contains all 102 table definitions and no data statements.
Current table-level auto-increment counters were removed; column declarations,
constraints and defaults remain. This is a MySQL/MariaDB reference schema, not
the C++ server's implemented persistence layer.

`sql/content.sql` contains 9,302 original INSERT statements for 52 reviewed static
definition tables. It excludes 29,529 of the original dump's 38,831 INSERTs.
Definitions include skills, skill effects and animation metadata, items, maps,
mobs, NPCs, quests, progression thresholds, shop catalogs and reward/drop rules.
Image/frame IDs and layout metadata remain because Java loaders and protocol
serialization use them; actual media files are removed.

The explicit allowlist was checked against:

- `src/main/java/com/ngocrong/server/SQLStatement.java`: static initialization queries.
- `src/main/java/com/ngocrong/server/Server.java`: head-frame loader and `initOthers()`.
- `src/main/java/com/ngocrong/shop/Shop.java`: static shop item catalogs.
- `src/main/java/com/ngocrong/data/DropRateData.java` and `BoMong*Data.java`:
  reward, drop and quest configuration entities, distinct from player quest state.

The retained tables are:

```text
array_head_2_frames
nr_achievement nr_arrow nr_background_item
nr_bo_mong_boss_config nr_bo_mong_config nr_bo_mong_moc_diem nr_bo_mong_nhiem_vu_config
nr_caption nr_collection_book nr_dart nr_drop_rate nr_effect nr_effect_data
nr_image nr_image_by_name nr_item nr_item_option_template nr_lucky_wheel
nr_map nr_mob_template nr_npc_template nr_others nr_part nr_power nr_salon
nr_shop_amulet nr_shop_bulma nr_shop_bunma_tet nr_shop_equipment
nr_shop_food_kemdau nr_shop_food_mily nr_shop_food_pudding nr_shop_food_sushi
nr_shop_food_xucxich nr_shop_huy_diet nr_shop_itemdetu nr_shop_linhthu nr_shop_ltn
nr_shop_lytieunuong nr_shop_popo nr_shop_santa nr_shop_thientu nr_shop_tv
nr_shop_uron nr_shop_whis
nr_skill nr_skill_disciple nr_skill_option_template nr_skill_paint nr_special_skill nr_task
```

`nr_others` is filtered further: only `avatar`, `shop`, `tile_set`, and `open_power`
keys remain. The notification row is excluded. No account, player, clan, balance,
trade, event-progress, history, security, ranking, website-post or live bot-state
rows are retained. Shop prices and skill parameters are static game definitions,
not balances owned by players. The empty legacy tables still have schema definitions.

`sql/source-content-manifest.json` records input/output SHA-256 hashes and exact
per-table INSERT counts. From the GameServer repository root:

```powershell
# Before removing the original SQL dump: regenerate prepared SQL, without deletion.
.\scripts\Prepare-LegacySource.ps1

# After cleanup: verify retained SQL, manifest hashes and recovered Java file count.
.\scripts\Prepare-LegacySource.ps1 -VerifyOnly
```

Preparation and verification passed: 502 Java files, 102 schemas, 52 content tables,
9,302 retained INSERTs. This checks source artifacts only; it does not import SQL
or claim that the old Java application or C++ server has been built or executed.

## Non-source inputs awaiting physical cleanup

The cleanup scope below was inventoried and is excluded from Git. `backup/` is eligible
only after Java source recovery; `SQL_HUNR_2025.sql` was eligible only after schema
and approved gameplay content extraction. Deletion remains pending as explained above.

| Relative path | Files | Bytes before cleanup |
| --- | ---: | ---: |
| `.m2/` | 1,937 | 98,071,392 |
| `AddItem/` | 114 | 207,655 |
| `backup/` | 303 | 248,861,627 |
| `backup_sql/` | 32 | 85,171,742 |
| `BuyItem/` | 29 | 20,349 |
| `data/` | 420 | 19,271,014 |
| `DropItem/` | 4 | 642 |
| `logs/` | 2 | 0 |
| `LogThoiVang/` | 6 | 97,482 |
| `resources/` | 6,876 | 41,122,332 |
| `autorun_lock.txt` | 1 | 8 |
| `matrix_pc_debug.txt` | 1 | 0 |
| `dummy` | 1 | 139 |
| `SQL_HUNR_2025.sql` | 1 | 5,456,663 |
| Total | 9,727 | 498,281,045 |

`resources/` had 6,654 `.png` files and 219 extensionless files whose signatures
were also PNG, an asset archive, a filename listing, and a .NET runtime JSON file.
It contained no Java, XML or properties source. `data/mob/x1..x4` contained packed
binary mob assets. `dummy` was an empty notebook with zero code cells. No nested
`.git` directory was detected inside this legacy folder.

## Local operational settings

`backupsql.bat` and `check_gold.bat` now take connection information from
`HUNR_DB_HOST`, `HUNR_DB_USER`, `HUNR_DB_NAME`, and `HUNR_DB_PASSWORD` environment
variables. `check_gold.bat` also requires `HUNR_GOLDBAR_WEBHOOK`; the embedded
webhook URL was removed. Optional `HUNR_MYSQL_EXE` and `HUNR_MYSQLDUMP_EXE` override
executables found through PATH. MySQL receives the password in its local process
environment through `MYSQL_PWD`, not a committed command-line argument.

`autoGoldbar.bat` checks required settings before starting its scheduler. These
scripts were inspected and updated but not executed, so no database queries,
backups or outbound webhook requests were made during preparation.

Recovered Java application/protocol constants were left unchanged. The fixed
guest-account password assignments were reviewed as application logic, not
database deployment credentials. A source scan found no literal HTTP URLs or
webhook endpoints in the recovered Java files; this is a bounded source check,
not a claim that dynamic/configured outbound connections cannot exist.
