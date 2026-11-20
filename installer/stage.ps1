<#
.SYNOPSIS
  Assemble installer\src\ from the Release|x64 build outputs in ..\build-win\.
  Run this after building FirstSynth-app / -vst3 / -clap in Release|x64, and
  before compiling FirstSynth.iss.

.NOTES
  Layout produced:
    src\FirstSynth.vst3\        - the whole VST3 bundle (DLL + Resources\web + Resources\presets + icon)
    src\clap\FirstSynth.clap    - the CLAP binary
    src\clap\Resources\         - web + presets for the CLAP
    src\FirstSynth.exe          - the Release Standalone exe
    src\app\Resources\          - web + presets for the Standalone
    src\THIRD-PARTY-NOTICES.txt - licensing notices (VST3 SDK + Steinberg ASIO)
#>
$ErrorActionPreference = 'Stop'
$root     = Split-Path $PSScriptRoot -Parent          # project root
$build    = Join-Path $root 'build-win'
$src      = Join-Path $PSScriptRoot 'src'

function Need($p) { if (-not (Test-Path -LiteralPath $p)) { throw "missing build output: $p  (build Release|x64 first)" } }

$vst3Bundle = Join-Path $build 'FirstSynth.vst3'
$clapBin    = Join-Path $build 'clap\x64\Release\FirstSynth.clap'
$appExe     = Join-Path $build 'FirstSynth_x64_Release.exe'
$resources  = Join-Path $build 'Resources'                # web\ + presets\, refreshed by every Release postbuild
$notices    = Join-Path $root 'THIRD-PARTY-NOTICES.txt'

Need $vst3Bundle; Need $clapBin; Need $appExe; Need $resources; Need $notices
Need (Join-Path $resources 'web\index.html')
Need (Join-Path $resources 'presets\_factory.txt')

if (Test-Path -LiteralPath $src) { Remove-Item -LiteralPath $src -Recurse -Force }
New-Item -ItemType Directory -Path $src, "$src\clap", "$src\app" | Out-Null

# VST3 - whole bundle
Copy-Item -LiteralPath $vst3Bundle -Destination $src -Recurse -Force

# CLAP - binary + its own copy of Resources
Copy-Item -LiteralPath $clapBin -Destination "$src\clap\FirstSynth.clap" -Force
Copy-Item -LiteralPath $resources -Destination "$src\clap\Resources" -Recurse -Force

# Standalone - exe (renamed) + its own copy of Resources
Copy-Item -LiteralPath $appExe -Destination "$src\FirstSynth.exe" -Force
Copy-Item -LiteralPath $resources -Destination "$src\app\Resources" -Recurse -Force

# shared
Copy-Item -LiteralPath $notices -Destination "$src\THIRD-PARTY-NOTICES.txt" -Force

# quick sanity report
$presetN = (Get-ChildItem "$src\app\Resources\presets" -Filter *.preset).Count
"staged -> $src"
"  VST3 bundle : $([bool](Test-Path "$src\FirstSynth.vst3\Contents\x86_64-win\FirstSynth.vst3"))"
"  CLAP        : $([bool](Test-Path "$src\clap\FirstSynth.clap"))"
"  Standalone  : $([bool](Test-Path "$src\FirstSynth.exe"))"
"  presets     : $presetN  (expect 37)"
"  notices     : $([bool](Test-Path "$src\THIRD-PARTY-NOTICES.txt"))"
if ($presetN -ne 37) { Write-Warning "expected 37 factory presets, found $presetN" }
