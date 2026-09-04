<#
.SYNOPSIS
  Regenerate the repo's presets\ folder (the shipped factory set) from a working
  presets folder, keeping only the presets marked in that folder's _factory.txt.

.DESCRIPTION
  A .preset file is a headerless positional dump of the plugin's params; the
  "factory" mark lives only in a sidecar _factory.txt (one preset name per line),
  written by the in-app dev-only "Factory" toggle. This script reads that file
  and copies just the marked *.preset (plus _factory.txt itself, normalised to
  LF / no BOM so C++ ReadFactoryMarks and the seed step agree) into -DestDir,
  removing any *.preset there that is no longer marked.

  postbuild-win.bat then bundles DestDir next to every built binary, and
  FirstSynth.cpp's SeedFactoryPresets() copies any missing ones into
  %LOCALAPPDATA%\FirstSynth\Presets on first run.

.EXAMPLE
  pwsh scripts\collect-factory-presets.ps1
  # SrcDir defaults to %LOCALAPPDATA%\FirstSynth\Presets, DestDir to .\presets

.EXAMPLE
  pwsh scripts\collect-factory-presets.ps1 -SrcDir 'D:\my-presets' -DestDir '.\presets' -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$SrcDir  = (Join-Path $env:LOCALAPPDATA 'FirstSynth\Presets'),
  [string]$DestDir  = (Join-Path $PSScriptRoot '..\presets')
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = New-Object System.Text.UTF8Encoding $false

$SrcDir  = (Resolve-Path -LiteralPath $SrcDir).Path
if (-not (Test-Path -LiteralPath $DestDir)) { New-Item -ItemType Directory -Path $DestDir | Out-Null }
$DestDir = (Resolve-Path -LiteralPath $DestDir).Path

$marksPath = Join-Path $SrcDir '_factory.txt'
if (-not (Test-Path -LiteralPath $marksPath)) {
  throw "no _factory.txt in $SrcDir - mark some presets with the in-app Factory toggle first"
}

# read + normalise the mark list (strip BOM, any line ending, trim, drop blanks)
$raw   = [System.IO.File]::ReadAllText($marksPath)
$names = $raw -split "`r`n|`n|`r" | ForEach-Object { $_.Trim([char]0xFEFF).Trim() } | Where-Object { $_ -ne '' }
$names = $names | Select-Object -Unique

$copied = 0; $missing = @()
foreach ($n in $names) {
  $src = Join-Path $SrcDir ($n + '.preset')
  if (Test-Path -LiteralPath $src) {
    if ($PSCmdlet.ShouldProcess("$n.preset", 'copy to DestDir')) {
      Copy-Item -LiteralPath $src -Destination (Join-Path $DestDir ($n + '.preset')) -Force
    }
    $copied++
  } else {
    $missing += $n
  }
}

# prune *.preset in DestDir that is no longer marked
$keep = @{}; foreach ($n in $names) { $keep[($n + '.preset')] = $true }
$pruned = @()
foreach ($f in Get-ChildItem -LiteralPath $DestDir -Filter *.preset) {
  if (-not $keep.ContainsKey($f.Name)) {
    if ($PSCmdlet.ShouldProcess($f.Name, 'remove (no longer marked factory)')) {
      Remove-Item -LiteralPath $f.FullName -Force
    }
    $pruned += $f.Name
  }
}

# write the normalised _factory.txt into DestDir
if ($PSCmdlet.ShouldProcess('_factory.txt', 'write normalised copy to DestDir')) {
  [System.IO.File]::WriteAllText((Join-Path $DestDir '_factory.txt'), ($names -join "`n") + "`n", $utf8NoBom)
}

Write-Host ("factory set: {0} preset(s) -> {1}" -f $copied, $DestDir)
if ($pruned.Count)  { Write-Warning ("pruned (unmarked): {0}" -f ($pruned  -join ', ')) }
if ($missing.Count) { Write-Warning ("marked but no .preset file in {0}: {1}" -f $SrcDir, ($missing -join ', ')) }
if ($missing.Count) { exit 1 }
