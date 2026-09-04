; =============================================================================
; FirstSynth installer script  (Inno Setup 6.x)
; EASYANDNICE INSTRUMENTS
;
; Build:
;   1. Build Release|x64 for FirstSynth-app / -vst3 / -clap (see ..\progress.md)
;   2. installer\stage.ps1        (assembles installer\src\ from ..\build-win\)
;   3. Export the two manuals to PDF into installer\  (see README.md)
;   4. (optional) put MicrosoftEdgeWebview2Setup.exe in installer\redist\
;   5. Open this file in Inno Setup Compiler -> Build -> Compile
;      -> installer\installer_output\FirstSynth_setup_v1.0.0.exe
; =============================================================================

#define MyAppName "FirstSynth"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "EASYANDNICE INSTRUMENTS"
#define MyAppURL "https://easyandnicewaki.com/"

; Bundle the Evergreen WebView2 bootstrapper only if the user dropped it in redist\
#ifexist "redist\MicrosoftEdgeWebview2Setup.exe"
  #define HaveWebView2Bootstrapper
#endif

[Setup]
AppId={{16A3DF42-586C-4E0D-AA71-A748360127BE}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DefaultGroupName={#MyAppPublisher}
DisableProgramGroupPage=yes
SetupIconFile=FirstSynth.ico
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\FirstSynth.exe
OutputDir=installer_output
OutputBaseFilename=FirstSynth_setup_v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[CustomMessages]
japanese.CompVST3=VST3 プラグイン
english.CompVST3=VST3 plugin
japanese.CompCLAP=CLAP プラグイン
english.CompCLAP=CLAP plugin
japanese.CompApp=スタンドアロン アプリ
english.CompApp=Standalone application
japanese.DesktopIcon=デスクトップに FirstSynth のショートカットを作成
english.DesktopIcon=Create a desktop shortcut for FirstSynth
japanese.RunApp=FirstSynth を今すぐ起動
english.RunApp=Launch FirstSynth now
japanese.ManualJP=マニュアル(日本語)
english.ManualJP=Manual (Japanese)
japanese.ManualEN=マニュアル(英語)
english.ManualEN=Manual (English)
japanese.UninstallIconText=FirstSynth をアンインストール
english.UninstallIconText=Uninstall FirstSynth
japanese.WV2Missing=このプラグインの画面表示には Microsoft Edge WebView2 ランタイムが必要です。見つからなかったため、次のページからインストールしてください:%n%nhttps://developer.microsoft.com/microsoft-edge/webview2/%n%n(Windows 11 には標準で入っています)
english.WV2Missing=This plugin's UI needs the Microsoft Edge WebView2 Runtime, which was not found. Please install it from:%n%nhttps://developer.microsoft.com/microsoft-edge/webview2/%n%n(It is preinstalled on Windows 11.)

[Components]
Name: "vst3"; Description: "{cm:CompVST3}"; Types: full custom
Name: "clap"; Description: "{cm:CompCLAP}"; Types: full custom
Name: "app";  Description: "{cm:CompApp}";  Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; Components: app

[Files]
; ---- VST3 : whole bundle -> C:\Program Files\Common Files\VST3\FirstSynth.vst3 ----
Source: "src\FirstSynth.vst3\*"; DestDir: "{commoncf64}\VST3\FirstSynth.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "src\THIRD-PARTY-NOTICES.txt"; DestDir: "{commoncf64}\VST3\FirstSynth.vst3"; Flags: ignoreversion; Components: vst3

; ---- CLAP -> C:\Program Files\Common Files\CLAP ----
Source: "src\clap\FirstSynth.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "src\clap\Resources\*"; DestDir: "{commoncf64}\CLAP\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: clap
Source: "src\THIRD-PARTY-NOTICES.txt"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap

; ---- Standalone -> {app} ----
Source: "src\FirstSynth.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app
Source: "src\app\Resources\*"; DestDir: "{app}\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: app

; ---- shared (docs / icon / manuals) - always installed ----
Source: "src\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "FirstSynth.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\manual\FirstSynth 取扱説明書.pdf"; DestDir: "{app}\manual"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\manual\FirstSynth User Manual.pdf"; DestDir: "{app}\manual"; Flags: ignoreversion skipifsourcedoesntexist

#ifdef HaveWebView2Bootstrapper
Source: "redist\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsWebView2
#endif

[Icons]
Name: "{group}\FirstSynth"; Filename: "{app}\FirstSynth.exe"; IconFilename: "{app}\FirstSynth.ico"; Components: app
Name: "{group}\{cm:ManualJP}"; Filename: "{app}\manual\FirstSynth 取扱説明書.pdf"; Check: FileExists(ExpandConstant('{app}\manual\FirstSynth 取扱説明書.pdf'))
Name: "{group}\{cm:ManualEN}"; Filename: "{app}\manual\FirstSynth User Manual.pdf"; Check: FileExists(ExpandConstant('{app}\manual\FirstSynth User Manual.pdf'))
Name: "{group}\{cm:UninstallIconText}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FirstSynth"; Filename: "{app}\FirstSynth.exe"; IconFilename: "{app}\FirstSynth.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\FirstSynth.exe"; Description: "{cm:RunApp}"; Flags: nowait postinstall skipifsilent unchecked; Components: app

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\FirstSynth.vst3"
Type: filesandordirs; Name: "{commoncf64}\CLAP\Resources"
Type: files; Name: "{commoncf64}\CLAP\FirstSynth.clap"
Type: files; Name: "{commoncf64}\CLAP\THIRD-PARTY-NOTICES.txt"

[Code]
function NeedsWebView2: Boolean;
var
  v: String;
begin
  Result := not (
    RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v) or
    RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v) or
    RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v)
  );
end;

procedure CurStepChanged(CurStep: TSetupStep);
#ifdef HaveWebView2Bootstrapper
var
  rc: Integer;
#endif
begin
  if (CurStep = ssPostInstall) and NeedsWebView2 then
  begin
#ifdef HaveWebView2Bootstrapper
    Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe'), '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, rc);
    if NeedsWebView2 then
      MsgBox(ExpandConstant('{cm:WV2Missing}'), mbInformation, MB_OK);
#else
    MsgBox(ExpandConstant('{cm:WV2Missing}'), mbInformation, MB_OK);
#endif
  end;
end;
