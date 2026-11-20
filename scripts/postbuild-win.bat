@echo off

REM - CALL "$(SolutionDir)scripts\postbuild-win.bat" "$(TargetExt)" "$(BINARY_NAME)" "$(Platform)" "$(COPY_VST2)" "$(TargetPath)" "$(VST2_ARM64EC_PATH)" "$(VST2_X64_PATH)" "$(VST3_ARM64EC_PATH)" "$(VST3_X64_PATH)" "$(AAX_ARM64EC_PATH)" "$(AAX_X64_PATH)" "$(CLAP_ARM64EC_PATH)" "$(CLAP_X64_PATH)" "$(BUILD_DIR)" "$(VST_ICON)" "$(AAX_ICON)" "$(CREATE_BUNDLE_SCRIPT)" "$(ICUDAT_PATH)" "$(Configuration)"

set FORMAT=%1
set NAME=%2
set PLATFORM=%3
set COPY_VST2=%4
set BUILT_BINARY=%5
set VST2_ARM64EC_PATH=%6
set VST2_X64_PATH=%7 
set VST3_ARM64EC_PATH=%8
set VST3_X64_PATH=%9
shift
shift 
shift
shift
shift 
shift
shift
set AAX_ARM64EC_PATH=%3
set AAX_X64_PATH=%4
set CLAP_ARM64EC_PATH=%5
set CLAP_X64_PATH=%6
set BUILD_DIR=%7
set VST_ICON=%8
set AAX_ICON=%9
shift
set CREATE_BUNDLE_SCRIPT=%9
shift
set ICUDAT_PATH=%9
shift
set CONFIGURATION=%9

echo POSTBUILD SCRIPT VARIABLES -----------------------------------------------------
echo FORMAT %FORMAT% 
echo NAME %NAME% 
echo PLATFORM %PLATFORM% 
echo COPY_VST2 %COPY_VST2% 
echo BUILT_BINARY %BUILT_BINARY% 
echo VST2_ARM64EC_PATH %VST2_ARM64EC_PATH% 
echo VST2_X64_PATH %VST2_X64_PATH% 
echo VST3_ARM64EC_PATH %VST3_ARM64EC_PATH% 
echo VST3_X64_PATH %VST3_X64_PATH% 
echo CLAP_ARM64EC_PATH %CLAP_ARM64EC_PATH%
echo CLAP_X64_PATH %CLAP_X64_PATH% 
echo BUILD_DIR %BUILD_DIR%
echo VST_ICON %VST_ICON% 
echo AAX_ICON %AAX_ICON% 
echo CREATE_BUNDLE_SCRIPT %CREATE_BUNDLE_SCRIPT%
echo ICUDAT_PATH %ICUDAT_PATH%
echo CONFIGURATION %CONFIGURATION%
echo END POSTBUILD SCRIPT VARIABLES -----------------------------------------------------

REM 2026-08-25: source for the WebView UI's resources - needed next to every
REM built binary so LoadIndexHtml()'s Release-mode branch (IPlugWebViewEditorDelegate.h)
REM can actually find index.html at runtime via GetCurrentModuleDirWin() instead
REM of the Debug-only compile-time __FILE__ path. %BUILD_DIR% is always
REM "<project root>\build-win", so its parent is the project root.
set RESOURCES_WEB_SRC=%BUILD_DIR%\..\resources\web

REM 2026-09-04: the factory preset set (37 *.preset + _factory.txt), bundled
REM next to every built binary the same way as resources\web above. FirstSynth's
REM SeedFactoryPresets() (FirstSynth.cpp) copies any that are missing into
REM %LOCALAPPDATA%\FirstSynth\Presets\ on first run - Release resolves this via
REM GetCurrentModuleDirWin()+"\Resources\presets", Debug reads the project
REM presets\ folder directly. Regenerate presets\ from a working folder with
REM scripts\collect-factory-presets.ps1.
set RESOURCES_PRESETS_SRC=%BUILD_DIR%\..\presets

REM 2026-08-25 user report: Debug and Release Standalone builds used to both
REM copy to the same "%NAME%_%PLATFORM%.exe" in %BUILD_DIR%, silently
REM overwriting each other - whichever config was built most recently is
REM whatever the user's every day double-click launched, even if they'd just
REM asked to test the other one. Gives Release its own distinct filename so
REM both can exist side by side; Debug keeps its original name unchanged
REM (the one already in the user's own muscle memory/tooling, e.g. CPU_Monitor.bat).

if %PLATFORM% == "ARM64EC" (
  if exist "%ICUDAT_PATH%" (
    echo copying icudtl.dat file next to built binary: %BUILT_BINARY%
    for %%F in (%BUILT_BINARY%) do (
      copy /y %ICUDAT_PATH% "%%~dpF"
    )
  ) else (
    echo icudtl.dat not found at %ICUDAT_PATH%, skipping...
  )

  if %FORMAT% == ".exe" (
    if %CONFIGURATION% == "Release" (
      echo copying exe to build dir: %BUILD_DIR%\%NAME%_%PLATFORM%_Release.exe
      copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%_Release.exe
    ) else (
      echo copying exe to build dir: %BUILD_DIR%\%NAME%_%PLATFORM%.exe
      copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%.exe
    )
    if exist "%ICUDAT_PATH%" (
      echo copying dat file to build dir: %BUILD_DIR%
      copy /y %ICUDAT_PATH% %BUILD_DIR%
    )
    if exist "%RESOURCES_WEB_SRC%" (
      echo copying WebView resources to build dir: %BUILD_DIR%\Resources\web
      xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%BUILD_DIR%\Resources\web\" >nul
    )
    if exist "%RESOURCES_PRESETS_SRC%" (
      echo copying factory presets to build dir: %BUILD_DIR%\Resources\presets
      xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%BUILD_DIR%\Resources\presets\" >nul
    )
  )

  if %FORMAT% == ".dll" (
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%.dll
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%
    )
  )

  if %FORMAT% == ".dll" (
    if %COPY_VST2% == "1" (
      echo copying ARM64EC binary to ARM64EC VST2 Plugins folder ...
      copy /y %BUILT_BINARY% %VST2_ARM64EC_PATH%
      if exist "%ICUDAT_PATH%" (
        copy /y %ICUDAT_PATH% %VST2_ARM64EC_PATH%
      )
    ) else (
      echo not copying ARM64EC VST2 binary
    )
  )

  if %FORMAT% == ".vst3" (
    echo copying ARM64EC binary to VST3 BUNDLE ..
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.vst3 %VST_ICON% %FORMAT%
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%.vst3\Contents\arm64ec-win
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%\%NAME%.vst3\Contents\arm64ec-win
    )
    if exist "%RESOURCES_WEB_SRC%" (
      echo copying WebView resources into VST3 bundle
      xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%BUILD_DIR%\%NAME%.vst3\Contents\arm64ec-win\Resources\web\" >nul
    )
    if exist "%RESOURCES_PRESETS_SRC%" (
      xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%BUILD_DIR%\%NAME%.vst3\Contents\arm64ec-win\Resources\presets\" >nul
    )
    if exist %VST3_ARM64EC_PATH% (
      echo copying VST3 bundle to ARM64EC VST3 Plugins folder ...
      call %CREATE_BUNDLE_SCRIPT% %VST3_ARM64EC_PATH%\%NAME%.vst3 %VST_ICON% %FORMAT%
      xcopy /E /H /Y %BUILD_DIR%\%NAME%.vst3\Contents\*  %VST3_ARM64EC_PATH%\%NAME%.vst3\Contents\
    )
  )
  
  if %FORMAT% == ".aaxplugin" (
    echo copying ARM64EC binary to AAX BUNDLE ..
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.aaxplugin %AAX_ICON% %FORMAT%
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%.aaxplugin\Contents\Arm64ec
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%\%NAME%.aaxplugin\Contents\Arm64ec
    )
    echo copying ARM64EC bundle to ARM64EC AAX Plugins folder ...
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.aaxplugin %AAX_ICON% %FORMAT%
    xcopy /E /H /Y %BUILD_DIR%\%NAME%.aaxplugin\Contents\* %AAX_ARM64EC_PATH%\%NAME%.aaxplugin\Contents\
  )

  if %FORMAT% == ".clap" (
    echo copying ARM64EC binary to CLAP Plugins folder ...
    if exist %CLAP_ARM64EC_PATH% (
      copy /y %BUILT_BINARY% %CLAP_ARM64EC_PATH%
      if exist "%ICUDAT_PATH%" (
        copy /y %ICUDAT_PATH% %CLAP_ARM64EC_PATH%
      )
      if exist "%RESOURCES_WEB_SRC%" (
        echo copying WebView resources next to CLAP plugin
        xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%CLAP_ARM64EC_PATH%\Resources\web\" >nul
      )
      if exist "%RESOURCES_PRESETS_SRC%" (
        xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%CLAP_ARM64EC_PATH%\Resources\presets\" >nul
      )
    )
  )
)

if %PLATFORM% == "x64" (
  
  if exist "%ICUDAT_PATH%" (
    echo copying icudtl.dat file next to built binary: %BUILT_BINARY%
    for %%F in (%BUILT_BINARY%) do (
      copy /y %ICUDAT_PATH% "%%~dpF"
    )
  ) else (
    echo icudtl.dat not found at %ICUDAT_PATH%, skipping...
  )

  if not exist "%ProgramFiles(x86)%" (
    echo "This batch script fails on 32 bit windows... edit accordingly"
  )

  if %FORMAT% == ".exe" (
    if %CONFIGURATION% == "Release" (
      echo copying exe to build dir: %BUILD_DIR%\%NAME%_%PLATFORM%_Release.exe
      copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%_Release.exe
    ) else (
      echo copying exe to build dir: %BUILD_DIR%\%NAME%_%PLATFORM%.exe
      copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%.exe
    )
    if exist "%ICUDAT_PATH%" (
      echo copying dat file to build dir: %BUILD_DIR%
      copy /y %ICUDAT_PATH% %BUILD_DIR%
    )
    if exist "%RESOURCES_WEB_SRC%" (
      echo copying WebView resources to build dir: %BUILD_DIR%\Resources\web
      xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%BUILD_DIR%\Resources\web\" >nul
    )
    if exist "%RESOURCES_PRESETS_SRC%" (
      echo copying factory presets to build dir: %BUILD_DIR%\Resources\presets
      xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%BUILD_DIR%\Resources\presets\" >nul
    )
  )

  if %FORMAT% == ".dll" (
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%_%PLATFORM%.dll
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%
    )
  )

  if %FORMAT% == ".dll" (
    if %COPY_VST2% == "1" (
      echo copying 64bit binary to 64bit VST2 Plugins folder ...
      copy /y %BUILT_BINARY% %VST2_X64_PATH%
      if exist "%ICUDAT_PATH%" (
        copy /y %ICUDAT_PATH% %VST2_X64_PATH%
      )
    ) else (
      echo not copying 64bit VST2 binary
    )
  )

  if %FORMAT% == ".vst3" (
    echo copying 64bit binary to VST3 BUNDLE ...
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.vst3 %VST_ICON% %FORMAT%
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%.vst3\Contents\x86_64-win
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%\%NAME%.vst3\Contents\x86_64-win
    )
    if exist "%RESOURCES_WEB_SRC%" (
      echo copying WebView resources into VST3 bundle
      xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%BUILD_DIR%\%NAME%.vst3\Contents\x86_64-win\Resources\web\" >nul
    )
    if exist "%RESOURCES_PRESETS_SRC%" (
      xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%BUILD_DIR%\%NAME%.vst3\Contents\x86_64-win\Resources\presets\" >nul
    )
    if exist %VST3_X64_PATH% (
      echo copying VST3 bundle to 64bit VST3 Plugins folder ...
      call %CREATE_BUNDLE_SCRIPT% %VST3_X64_PATH%\%NAME%.vst3 %VST_ICON% %FORMAT%
      xcopy /E /H /Y %BUILD_DIR%\%NAME%.vst3\Contents\*  %VST3_X64_PATH%\%NAME%.vst3\Contents\
    )
  )
  
  if %FORMAT% == ".aaxplugin" (
    echo copying 64bit binary to AAX BUNDLE ...
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.aaxplugin %AAX_ICON% %FORMAT%
    copy /y %BUILT_BINARY% %BUILD_DIR%\%NAME%.aaxplugin\Contents\x64
    if exist "%ICUDAT_PATH%" (
      copy /y %ICUDAT_PATH% %BUILD_DIR%\%NAME%.aaxplugin\Contents\x64
    )
    echo copying 64bit bundle to 64bit AAX Plugins folder ... 
    call %CREATE_BUNDLE_SCRIPT% %BUILD_DIR%\%NAME%.aaxplugin %AAX_ICON% %FORMAT%
    xcopy /E /H /Y %BUILD_DIR%\%NAME%.aaxplugin\Contents\* %AAX_X64_PATH%\%NAME%.aaxplugin\Contents\
  )
  
  if %FORMAT% == ".clap" (
    echo copying x64 binary to CLAP Plugins folder ...
    if exist %CLAP_X64_PATH% (
      copy /y %BUILT_BINARY% %CLAP_X64_PATH%
      if exist "%ICUDAT_PATH%" (
        copy /y %ICUDAT_PATH% %CLAP_X64_PATH%
      )
      if exist "%RESOURCES_WEB_SRC%" (
        echo copying WebView resources next to CLAP plugin
        xcopy /E /H /Y /I "%RESOURCES_WEB_SRC%" "%CLAP_X64_PATH%\Resources\web\" >nul
      )
      if exist "%RESOURCES_PRESETS_SRC%" (
        xcopy /E /H /Y /I "%RESOURCES_PRESETS_SRC%" "%CLAP_X64_PATH%\Resources\presets\" >nul
      )
    )
  )
)