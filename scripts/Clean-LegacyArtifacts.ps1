[CmdletBinding(SupportsShouldProcess)]
param()

# Run manually from PowerShell. This script was prepared but deletion was blocked
# by the agent execution policy. -WhatIf lists the exact planned removals.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$legacyPath = (Resolve-Path -LiteralPath (Join-Path $workspacePath 'Sample game old/HUNR_Server_Java/Hunr2026')).Path
if (-not $legacyPath.StartsWith($workspacePath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The legacy directory must be inside this workspace.'
}
$sourceManifest = Get-Content -LiteralPath (Join-Path $legacyPath 'docs/java-source-manifest.json') -Raw | ConvertFrom-Json

function Assert-SourcePreserved {
    foreach ($entry in $sourceManifest) {
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $legacyPath $entry.path))
        if (-not $sourcePath.StartsWith($legacyPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Source manifest path escapes the legacy directory.'
        }
        if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Source changed since recovery: $($entry.path). Review before deleting backups."
        }
    }
}
Assert-SourcePreserved
& (Join-Path $PSScriptRoot 'Prepare-LegacySource.ps1') -VerifyOnly | Out-Host
$names = @('.m2', 'AddItem', 'backup', 'backup_sql', 'BuyItem', 'data', 'DropItem',
    'logs', 'LogThoiVang', 'resources', 'autorun_lock.txt', 'matrix_pc_debug.txt', 'dummy', 'SQL_HUNR_2025.sql')
$targets = foreach ($name in $names) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $legacyPath $name))
    if (-not $candidate.StartsWith($legacyPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A cleanup target escapes the named legacy directory.'
    }
    if (-not (Test-Path -LiteralPath $candidate)) { continue }
    $resolved = (Resolve-Path -LiteralPath $candidate).Path
    if ($resolved -ne $candidate) { throw "Cleanup target resolves elsewhere: $candidate" }
    $item = Get-Item -LiteralPath $candidate -Force
    $children = if ($item.PSIsContainer) { @($item) + @(Get-ChildItem -LiteralPath $candidate -Recurse -Force) } else { @($item) }
    if (@($children | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }).Count) {
        throw "Reparse point inside cleanup target: $candidate"
    }
    $files = @($children | Where-Object { -not $_.PSIsContainer })
    [pscustomobject]@{ name = $name; absolute = $candidate; files = $files.Count; bytes = ($files | Measure-Object Length -Sum).Sum }
}
$removed = [Collections.Generic.List[object]]::new()
foreach ($target in $targets) {
    if ($PSCmdlet.ShouldProcess($target.absolute, 'Delete inspected non-source artifacts')) {
        Remove-Item -LiteralPath $target.absolute -Recurse -Force
        $removed.Add($target)
    }
}
Assert-SourcePreserved
if (-not $WhatIfPreference) {
    & (Join-Path $PSScriptRoot 'Prepare-LegacySource.ps1') -VerifyOnly | Out-Host
    $remaining = @($names | Where-Object { Test-Path -LiteralPath (Join-Path $legacyPath $_) })
    $report = [ordered]@{
        status = $(if ($remaining.Count) { 'partial' } else { 'complete' })
        completedUtc = [DateTime]::UtcNow.ToString('o')
        preservedJavaFiles = @($sourceManifest).Count
        removedFilesThisRun = ($removed | Measure-Object files -Sum).Sum
        removedBytesThisRun = ($removed | Measure-Object bytes -Sum).Sum
        remainingTargets = $remaining
    }
    [IO.File]::WriteAllText((Join-Path $legacyPath 'docs/cleanup-report.json'), ($report | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    [pscustomobject]$report
}
