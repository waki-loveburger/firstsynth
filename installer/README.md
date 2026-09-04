# FirstSynth installer

Inno Setup 6 installer for FirstSynth (VST3 + CLAP + Standalone), EASYANDNICE INSTRUMENTS.

## Build steps

1. **Build Release|x64** for all three targets (from the project root):

   ```
   MSBuild FirstSynth.sln -t:FirstSynth-app  -p:Configuration=Release -p:Platform=x64 -m
   MSBuild FirstSynth.sln -t:FirstSynth-vst3 -p:Configuration=Release -p:Platform=x64 -m
   MSBuild FirstSynth.sln -t:FirstSynth-clap -p:Configuration=Release -p:Platform=x64 -m
   ```

2. **Stage** the build outputs into `installer\src\`:

   ```
   powershell -ExecutionPolicy Bypass -File installer\stage.ps1
   ```

3. **Export the manuals to PDF** and put them in this `installer\` folder, with
   these exact names (they are referenced by the `.iss`):

   - `FirstSynth 取扱説明書.pdf`   (from `manual\FirstSynth 取扱説明書.docx`)
   - `FirstSynth User Manual.pdf`  (from `manual\FirstSynth User Manual.docx`)

   Open each `.docx` in Word -> File -> Save As -> PDF. If a PDF is missing the
   installer still compiles and installs, it just omits that manual + its Start
   Menu shortcut.

4. **(optional) WebView2 bootstrapper.** The plugin UI needs the Microsoft Edge
   WebView2 Runtime (preinstalled on Windows 11, usually present on Windows 10).
   The installer checks for it and, if absent, either runs a bundled bootstrapper
   or shows a download link. To bundle it, download the *Evergreen Bootstrapper*
   from <https://developer.microsoft.com/microsoft-edge/webview2/> and save it as:

   - `installer\redist\MicrosoftEdgeWebview2Setup.exe`

   The `.iss` auto-detects this file (`#ifexist`) - no edit needed.

5. **Compile** `FirstSynth.iss` — open it in the Inno Setup Compiler and
   Build -> Compile, or:

   ```
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\FirstSynth.iss
   ```

   Output: `installer\installer_output\FirstSynth_setup_v1.0.0.exe`

## What it installs

| Component  | Location |
|------------|----------|
| VST3       | `C:\Program Files\Common Files\VST3\FirstSynth.vst3\` |
| CLAP       | `C:\Program Files\Common Files\CLAP\FirstSynth.clap` (+ `Resources\`) |
| Standalone | `C:\Program Files\EASYANDNICE INSTRUMENTS\FirstSynth\` + Start Menu (and optional desktop) shortcut |

Each plugin folder and the Standalone folder get a copy of
`THIRD-PARTY-NOTICES.txt` (VST3 SDK + Steinberg ASIO attributions - required,
must ship). Factory presets ride inside each format's `Resources\presets\` and
are seeded into `%LOCALAPPDATA%\FirstSynth\Presets\` on first run.

Requires admin (writes to `Program Files`). x64 only. Uninstaller removes all
three.

## Not committed

`installer\src\`, `installer\installer_output\`, `installer\redist\`, and
`installer\*.pdf` are build artifacts / large binaries - git-ignored. Commit only
`FirstSynth.iss`, `stage.ps1`, `FirstSynth.ico`, and this file.
