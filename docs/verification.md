# Verification record — 2026-09-11

No CMake configure, compilation, CTest run, C++ executable, Java server, database
import/query, or network client session was run. The user explicitly builds the
project manually. Source review and artifact checks below are not runtime proof.

Completed checks:

- SHA-256 comparison of all 502 Java files against archive members; 3,609,587
  source bytes preserved. Per-file hashes are in the legacy source manifest.
- Offline SQL extraction and subsequent `Prepare-LegacySource.ps1 -VerifyOnly`:
  102 schemas; 52 static content tables; 9,302 INSERT statements. Manifest hashes
  and exact per-table counts matched; no allowlisted account/player/history rows.
- Git candidate scan: all 502 Java files included; no files from the legacy root's
  media/data/cache/log/backup directories, original full SQL dump, or image/archive/
  JAR/executable extensions included. Java package `com/ngocrong/data` is source
  and is correctly retained. No Git staging, commit or push was performed.
- Parsed CMakePresets JSON, two PowerShell scripts using PowerShell's AST parser,
  and debug_client.py using Python's AST parser. The client and cleanup script
  were not executed.
- Confirmed all 11 CMake-listed C++ source paths and project-local quoted include
  paths exist. Reviewed source for C++17/MSVC compatibility without compiling.
- Whitespace/conflict-marker scan of new source/docs/scripts and `git diff --check`.
- ServerEngine submodule remains at `436f299c7bf4cfa04409a8273a3dce83910583ce`,
  without source changes.

C++ tests were authored for parsing/reference/numeric failures, custom attributes
and handlers, per-entity cooldown, mana, target validation, movement credit, DoT
cadence and refresh, expiry, stat modifiers, shield depletion, death, event-budget
atomic rejection, source-derived skill arithmetic and protocol/session ownership.
They still need to be compiled and run by the user.

Physical artifact deletion was attempted but rejected by automatic execution
review (`blocked by policy`). A more narrowly scoped explicit resources deletion
was also rejected. **Zero cleanup files were deleted.** The 9,727 files totaling
498,281,045 bytes remain ignored on disk. `Clean-LegacyArtifacts.ps1` is prepared
for manual execution and checks source preservation before and after removal.
Legacy `docs/cleanup-report.json` is authoritative for the physical cleanup state.
