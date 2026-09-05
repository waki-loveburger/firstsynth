# 1st synth (FirstSynth)

iPlug2-based synth plug-in for EASYANDNICE INSTRUMENTS. Ships as **Standalone**,
**CLAP** and **VST3** on Windows. The GUI is HTML/CSS/JS in `resources/web/`,
hosted in a platform WebView; the DSP is hand-written in `FirstSynth_DSP.h` /
`FirstSynth_Effects.h` / `FirstSynth_Looper.h`.

`progress.md` is the authoritative log of what's been done and why — read it
before changing anything. This file only covers getting a build running.

## Building on Windows

### 1. Toolchain

Visual Studio 2022 (or **Build Tools** 2022 — the IDE isn't needed) with the
**Desktop development with C++** workload. Nothing else: CMake and NuGet are not
used, and the WebView2 / WIL NuGet packages are committed under `packages/`.

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### 2. The iPlug2 fork, as a sibling folder

This project builds against a **modified fork** of iPlug2, not stock upstream
(it adds `ADSREnvelope::SetAttackShape`, LFO S&H, `GetScreenScale`, a WASAPI
driver, Standalone preset load/save, and several DPI / WebView2 fixes). The
repo is private — ask waki for access.

The vcxproj files reach it as `..\..\iPlug2`, so it must sit **next to** this
folder. The parent folder's name doesn't matter:

```
<anywhere>\FirstSynth\      <- this repo
<anywhere>\iPlug2\          <- the fork
```

```
git clone https://github.com/waki-loveburger/iPlug2 ..\iPlug2
```

Keep that checkout on `master` (fork). It intentionally does **not** track the
latest upstream — see `progress.md`.

### 3. iPlug2's SDKs

The VST3 and CLAP SDKs aren't committed to iPlug2; fetch them once, from **Git
Bash** (the scripts are bash, and they use `git`):

```
cd ../iPlug2/Dependencies/IPlug && ./download-iplug-sdks.sh
```

RtAudio / RtMidi are already in the iPlug2 checkout, so nothing else is needed.

### 4. Build

```
.\build.ps1
```

That builds Standalone + CLAP in `Debug|x64` — the two formats used day to day.
It finds MSBuild itself (via `vswhere`, so no hard-coded Visual Studio path),
resolves everything relative to the script, closes a running Standalone so the
post-build copy can't fail with a file-in-use error, and creates the CLAP
destination folder if it's missing.

```
.\build.ps1 app                            # one format
.\build.ps1 app clap vst3 -Configuration Release   # what the installer stages
.\build.ps1 all -Rebuild
.\build.ps1 -?                             # full help
```

Equivalent by hand, if you'd rather not use the script:

```
MSBuild.exe FirstSynth.sln -t:FirstSynth-app -p:Configuration=Debug -p:Platform=x64 -m
```

(Note there is no `-t:FirstSynth-app:Build` — the solution metaproject exposes
`<project>`, `<project>:Clean` and `<project>:Rebuild` only, and the bare name
already means build.)

### Where the output goes

| Format | Path |
|---|---|
| Standalone | `build-win\FirstSynth_x64.exe` |
| CLAP | `%LOCALAPPDATA%\Programs\Common\CLAP\FirstSynth.clap` |
| VST3 | `C:\Program Files\Common Files\VST3\FirstSynth.vst3\` |

The VST3 path is a deliberate override of iPlug2's default (Renoise and
BespokeSynth don't scan iPlug2's `%LOCALAPPDATA%` location) — see the long
comment in `config\FirstSynth-win.props`. Writing there needs a **one-time,
admin-elevated** permission grant on each machine; without it the build still
succeeds and the copy is silently skipped.

`icudtl.dat not found ... skipping` in the post-build output is expected: it's
an IGraphics/Skia asset, and this project is `NO_IGRAPHICS` + WebView.

## Releases

Building the installer is a separate flow — see `installer\README.md`.

## Other targets

`CMakeLists.txt`, the `*.mk` files and `.vscode\c_cpp_properties.json` are
leftovers from the iPlug2 `IPlugWebUI` example this project started from. They
are **not** maintained and do not currently work (wrong relative depth, missing
`eni_auth` sources, IGraphics defines this project doesn't use). The macOS /
iOS Xcode projects have not been built recently either. Windows + MSBuild is
the supported path.
