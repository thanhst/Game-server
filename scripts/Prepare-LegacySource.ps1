[CmdletBinding()]
param(
    [string]$LegacyRoot = (Join-Path $PSScriptRoot '../Sample game old/HUNR_Server_Java/Hunr2026'),
    [string]$DumpPath,
    [switch]$VerifyOnly
)
# Offline preparation only: never deletes files, builds, connects to a database,
# extracts archives, or changes recovered Java source. -VerifyOnly needs no dump.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$legacyPath = (Resolve-Path -LiteralPath $LegacyRoot).Path
if (-not $DumpPath) { $DumpPath = Join-Path $legacyPath 'SQL_HUNR_2025.sql' }
$outputPath = Join-Path $legacyPath 'sql'
$schemaPath = Join-Path $outputPath 'schema.sql'
$contentPath = Join-Path $outputPath 'content.sql'
$manifestPath = Join-Path $outputPath 'source-content-manifest.json'
$javaCount = @(Get-ChildItem -LiteralPath (Join-Path $legacyPath 'src/main/java') -Recurse -File -Filter '*.java').Count
if ($javaCount -ne 502) { throw "Expected the 502 recovered Java files; found $javaCount. Review recovery." }

# Reviewed against SQLStatement.java, Server.initOthers(), Shop.init(), and the
# DropRate/BoMong entities. Prefix matching is unsafe for similarly named live tables.
$contentTables = @(
    'array_head_2_frames', 'nr_achievement', 'nr_arrow', 'nr_background_item',
    'nr_bo_mong_boss_config', 'nr_bo_mong_config', 'nr_bo_mong_moc_diem',
    'nr_bo_mong_nhiem_vu_config', 'nr_caption', 'nr_collection_book',
    'nr_dart', 'nr_drop_rate', 'nr_effect', 'nr_effect_data', 'nr_image',
    'nr_image_by_name', 'nr_item', 'nr_item_option_template', 'nr_lucky_wheel',
    'nr_map', 'nr_mob_template', 'nr_npc_template', 'nr_others', 'nr_part',
    'nr_power', 'nr_salon', 'nr_shop_amulet', 'nr_shop_bulma',
    'nr_shop_bunma_tet', 'nr_shop_equipment', 'nr_shop_food_kemdau',
    'nr_shop_food_mily', 'nr_shop_food_pudding', 'nr_shop_food_sushi',
    'nr_shop_food_xucxich', 'nr_shop_huy_diet', 'nr_shop_itemdetu',
    'nr_shop_linhthu', 'nr_shop_ltn', 'nr_shop_lytieunuong', 'nr_shop_popo',
    'nr_shop_santa', 'nr_shop_thientu', 'nr_shop_tv', 'nr_shop_uron',
    'nr_shop_whis', 'nr_skill', 'nr_skill_disciple', 'nr_skill_option_template',
    'nr_skill_paint', 'nr_special_skill', 'nr_task'
)
$otherKeys = @('avatar', 'shop', 'tile_set', 'open_power')
$ddlPattern = '(?ms)^CREATE TABLE\s+`(?<name>[^`]+)`.*?^\)\s*[^;]*;\s*$'
$insertPattern = '^INSERT INTO `(?<name>[A-Za-z0-9_]+)` VALUES .+;\s*$'
$otherPattern = '^INSERT INTO `nr_others` VALUES \([^,]+, ''(?<key>[^'']+)'''

function Assert-PreparedContent([string]$Schema, [string]$Content) {
    if ([regex]::Matches($Schema, $ddlPattern).Count -ne 102) { throw 'Expected 102 complete table definitions.' }
    if ($Schema -match '(?im)^\s*(INSERT|REPLACE|UPDATE|DELETE)\b' -or $Schema -match '(?i)AUTO_INCREMENT\s*=') {
        throw 'Schema contains data statements or a live auto-increment counter.'
    }
    $counts = [ordered]@{}
    foreach ($table in $contentTables) { $counts[$table] = 0 }
    foreach ($line in ($Content -split '\r?\n')) {
        if ($line -match '^INSERT INTO') {
            if ($line -notmatch $insertPattern) { throw 'Unexpected INSERT format.' }
            $table = $Matches['name']
            if ($table -notin $contentTables) { throw "Non-allowlisted content table: $table" }
            if ($table -eq 'nr_others') {
                if ($line -notmatch $otherPattern) { throw 'Unexpected nr_others row format.' }
                if ($Matches['key'] -notin $otherKeys) { throw 'Non-allowlisted nr_others key.' }
            }
            $counts[$table]++
        } elseif ($line -notmatch '^\s*(?:--.*|SET NAMES utf8mb4;|START TRANSACTION;|COMMIT;)?\s*$') {
            throw 'Unexpected non-comment/non-INSERT content statement.'
        }
    }
    foreach ($table in $contentTables) {
        if ($counts[$table] -eq 0) { throw "Expected reviewed content rows for $table." }
    }
    return ,$counts
}

if ($VerifyOnly) {
    $counts = Assert-PreparedContent ([IO.File]::ReadAllText($schemaPath)) ([IO.File]::ReadAllText($contentPath))
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $schemaPath -Algorithm SHA256).Hash -ne $manifest.schemaSha256 -or
        (Get-FileHash -LiteralPath $contentPath -Algorithm SHA256).Hash -ne $manifest.contentSha256) {
        throw 'Prepared SQL hashes differ from the source-content manifest.'
    }
    foreach ($table in $contentTables) {
        if ($counts[$table] -ne $manifest.contentInsertCounts.$table) { throw "Content count differs for $table." }
    }
    [pscustomobject]@{ Verified = $true; JavaFiles = $javaCount; SchemaTables = 102; ContentTables = $counts.Count; ContentInserts = ($counts.Values | Measure-Object -Sum).Sum }
    return
}

$dump = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $DumpPath).Path)
$ddl = [regex]::Matches($dump, $ddlPattern)
if ($ddl.Count -ne 102 -or [regex]::Matches($dump, '(?im)^CREATE TABLE\b').Count -ne 102) {
    throw 'Expected the reviewed 102-table legacy dump; review a different dump explicitly.'
}
$header = "-- Source-only reference extracted from SQL_HUNR_2025.sql. No live records.`n-- Generated by scripts/Prepare-LegacySource.ps1; MySQL/MariaDB reference, not C++ persistence.`nSET NAMES utf8mb4;`n`n"
$schemaParts = foreach ($block in $ddl) {
    # Retain column AUTO_INCREMENT declarations; remove only deployment counters.
    ([regex]::Replace($block.Value.Trim(), '\s+AUTO_INCREMENT\s*=\s*\d+', '') -replace '\r\n', "`n")
}
$schema = $header + ($schemaParts -join "`n`n") + "`n"
$rows = [Collections.Generic.List[string]]::new()
$inputInsertCount = 0
foreach ($line in ($dump -split '\r?\n')) {
    if ($line -notmatch '^INSERT INTO') { continue }
    $inputInsertCount++
    if ($line -notmatch $insertPattern) { throw 'Unexpected or multiline INSERT: review before extracting.' }
    $table = $Matches['name']
    if ($table -notin $contentTables) { continue }
    if ($table -eq 'nr_others') {
        if ($line -notmatch $otherPattern) { throw 'Unexpected nr_others row format.' }
        if ($Matches['key'] -notin $otherKeys) { continue }
    }
    $rows.Add($line.TrimEnd())
}
if ($inputInsertCount -ne 38831) { throw "Expected 38831 original INSERTs; found $inputInsertCount. Review the dump." }
$content = $header + "-- Static definitions only; no accounts, player state, balances, security, or history.`nSTART TRANSACTION;`n" + ($rows -join "`n") + "`nCOMMIT;`n"
$counts = Assert-PreparedContent $schema $content
[void][IO.Directory]::CreateDirectory($outputPath)
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($schemaPath, $schema, $encoding)
[IO.File]::WriteAllText($contentPath, $content, $encoding)
$manifest = [ordered]@{
    formatVersion = 1
    sourceDump = 'SQL_HUNR_2025.sql'
    sourceDumpSha256 = (Get-FileHash -LiteralPath $DumpPath -Algorithm SHA256).Hash
    sourceArchive = 'backup_22_04_2026___19h_21m_11s.zip'
    sourceArchiveSha256 = 'E005110FF5AE7D48013D5510B9FC635327A422F191857D72FC11AAB2988F6F9E'
    recoveredJavaFiles = $javaCount
    schemaTables = $ddl.Count
    originalInsertStatements = $inputInsertCount
    contentInsertStatements = $rows.Count
    excludedInsertStatements = $inputInsertCount - $rows.Count
    contentInsertCounts = $counts
    allowedOtherKeys = $otherKeys
    schemaSha256 = (Get-FileHash -LiteralPath $schemaPath -Algorithm SHA256).Hash
    contentSha256 = (Get-FileHash -LiteralPath $contentPath -Algorithm SHA256).Hash
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6) + "`n", $encoding)
[pscustomobject]@{ Prepared = $true; JavaFiles = $javaCount; SchemaTables = $ddl.Count; ContentTables = $counts.Count; ContentInserts = $rows.Count; ExcludedInserts = $inputInsertCount - $rows.Count }
