<#
.SYNOPSIS
  Build FirstSynth from the command line, on any machine, without hard-coded paths.

.DESCRIPTION
  Everything this repo needs is already relative: the vcxproj files and
  config\FirstSynth-win.props reach the framework as ..\..\iPlug2, so the only
  requirement is that the iPlug2 checkout sits NEXT TO this folder:

      <anywhere>\FirstSynth\      <- this repo
      <anywhere>\iPlug2\          <- the modified fork (github.com/waki-loveburger/iPlug2)

  The one genuinely machine-dependent thing was MSBuild.exe's absolute path, so
  this script finds it with vswhere instead (vswhere itself is always at a fixed
  location on any machine with Visual Studio or the Build Tools installed).

  It also does the two chores that are easy to forget and fail silently:
  killing a running Standalone (postbuild's copy fails with file-in-use
  otherwise) and creating the CLAP destination folder (postbuild-win.bat only
  copies "if exist" and never creates it).

  REAPER is deliberately NOT killed - it's the user's own app, not disposable
  test tooling. If it's running you get a warning and it's your call.

.PARAMETER Targets
  Which plug-in formats to build: app, clap, vst3, vst2, aax, or all.
  Defaults to "app clap" - the two formats this project actually ships and tests.

.PARAMETER Configuration
  Debug (default), Release or Tracer.

.EXAMPLE
  .\build.ps1
  # Debug|x64 Standalone + CLAP

.EXAMPLE
  .\build.ps1 app clap vst3 -Configuration Release
  # the three formats the installer stages

.EXAMPLE
  .\build.ps1 all -Rebuild
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
  [ValidateSet('app', 'clap', 'vst3', 'vst2', 'aax', 'all')]
  [string[]]$Targets = @('app', 'clap'),

  [ValidateSet('Debug', 'Release', 'Tracer')]
  [string]$Configuration = 'Debug',

  [ValidateSet('x64', 'ARM64EC')]
  [string]$Platform = 'x64',

  # Full rebuild instead of an incremental build.
  [switch]$Rebuild,

  # Leave a running Standalone alone (the postbuild copy will then fail).
  [switch]$NoKill
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$sln  = Join-Path $root 'FirstSynth.sln'

# --- the sibling iPlug2 checkout -------------------------------------------
# Same ..\..\iPlug2 relation the vcxproj files use, resolved from here instead.
$iplug2 = Join-Path (Split-Path $root -Parent) 'iPlug2'
if (-not (Test-Path -LiteralPath (Join-Path $iplug2 'common-win.props'))) {
  throw @"
iPlug2 not found at: $iplug2

This project builds against a MODIFIED fork of iPlug2, expected as a sibling
folder. Clone it there:

    git clone https://github.com/waki-loveburger/iPlug2 "$iplug2"

then fetch its dependencies (Git Bash):

    cd "$iplug2/Dependencies/IPlug" && ./download-iplug-sdks.sh
"@
}

# --- MSBuild ---------------------------------------------------------------
# Prefer the 64-bit MSBuild: under the 32-bit one $(CommonProgramFiles) silently
# means the "(x86)" folder, which has bitten this project before (see the comment
# on VST3_X64_PATH in config\FirstSynth-win.props).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw "vswhere.exe not found at $vswhere - install Visual Studio 2022 Build Tools with the C++ workload."
}

$found = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -find 'MSBuild\**\Bin\**\MSBuild.exe'

$msbuild = $found | Where-Object { $_ -match '\\amd64\\MSBuild\.exe$' } | Select-Object -First 1
if (-not $msbuild) { $msbuild = $found | Select-Object -First 1 }
if (-not $msbuild) {
  throw "No MSBuild with the C++ toolset found. Install the 'Desktop development with C++' workload."
}

# --- chores postbuild-win.bat can't do for itself --------------------------
if (-not $NoKill) {
  # Must be a wildcard, not a fixed list. postbuild-win.bat names the copy in
  # build-win\ after the configuration and platform - FirstSynth_x64.exe for
  # Debug but FirstSynth_x64_Release.exe / _Tracer.exe otherwise (and ARM64EC
  # variants) - and its "copy /y" fails SILENTLY when that file is running, so
  # a missed name leaves a stale binary at the path you then launch. Cost an
  # hour of debugging a "fix that didn't work" on 2026-09-05; it had built
  # fine, the root copy just never got replaced.
  Get-Process -Name 'FirstSynth*' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
}
if (Get-Process -Name 'reaper' -ErrorAction SilentlyContinue) {
  Write-Warning 'REAPER is running - close it yourself if the plug-in copy fails with a file-in-use error.'
}

if ($Targets -contains 'clap' -or $Targets -contains 'all') {
  # postbuild-win.bat copies "if exist" only, so a missing folder means the
  # build "succeeds" while quietly installing nothing.
  $clapDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\CLAP'
  if (-not (Test-Path -LiteralPath $clapDir)) {
    New-Item -ItemType Directory -Force -Path $clapDir | Out-Null
    Write-Host "created CLAP folder: $clapDir" -ForegroundColor DarkGray
  }
}
if ($Targets -contains 'vst3' -or $Targets -contains 'all') {
  # This project overrides iPlug2's default to the real system-wide VST3 folder,
  # which needs a one-time admin-elevated permission grant on each machine.
  $vst3Dir = Join-Path $env:CommonProgramW6432 'VST3'
  if (-not (Test-Path -LiteralPath $vst3Dir)) {
    Write-Warning "VST3 folder missing: $vst3Dir - the postbuild copy will be skipped."
  }
}

# --- build -----------------------------------------------------------------
if ($Targets -contains 'all') { $Targets = @('app', 'clap', 'vst3', 'vst2', 'aax') }

Write-Host "MSBuild : $msbuild"
Write-Host "iPlug2  : $iplug2"
Write-Host "building: $($Targets -join ', ')  ($Configuration|$Platform)`n"

foreach ($t in $Targets) {
  $project = "FirstSynth-$t"
  Write-Host "==> $project" -ForegroundColor Cyan

  # The solution metaproj exposes each project as a target named after it, plus
  # "<name>:Clean" / "<name>:Rebuild". There is deliberately no "<name>:Build" -
  # the bare name IS the build, and asking for ":Build" fails with MSB4057.
  $slnTarget = if ($Rebuild) { "${project}:Rebuild" } else { $project }

  & $msbuild $sln `
    "-t:$slnTarget" `
    "-p:Configuration=$Configuration" `
    "-p:Platform=$Platform" `
    -m -nologo -v:minimal

  if ($LASTEXITCODE -ne 0) { throw "$project failed ($Configuration|$Platform)" }
}

# --- where things landed ---------------------------------------------------
$build = Join-Path $root 'build-win'
$suffix = if ($Configuration -eq 'Debug') { '' } else { "_$Configuration" }

Write-Host "`nbuilt:" -ForegroundColor Green
foreach ($t in $Targets) {
  switch ($t) {
    'app'  { Write-Host "  Standalone  $(Join-Path $build ("FirstSynth_${Platform}${suffix}.exe"))" }
    'clap' { Write-Host "  CLAP        $(Join-Path $env:LOCALAPPDATA 'Programs\Common\CLAP\FirstSynth.clap')" }
    'vst3' { Write-Host "  VST3        $(Join-Path $env:CommonProgramW6432 'VST3\FirstSynth.vst3')" }
    default { Write-Host "  $t          $(Join-Path $build $t)" }
  }
}
