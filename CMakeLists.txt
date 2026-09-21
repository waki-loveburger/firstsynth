cmake_minimum_required(VERSION 3.14)

# config.h holds the version everything else follows. Windows keeps its own copy
# in the installer / resources; on macOS the value is written out in the Info.plists.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config.h" _firstsynth_config_h)
string(REGEX MATCH "#define PLUG_VERSION_STR \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _firstsynth_match "${_firstsynth_config_h}")
if(NOT _firstsynth_match)
  message(FATAL_ERROR "Could not read PLUG_VERSION_STR from config.h")
endif()
set(FIRSTSYNTH_VERSION "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define PLUG_VERSION_HEX (0x[0-9A-Fa-f]+)" _firstsynth_match "${_firstsynth_config_h}")
if(NOT _firstsynth_match)
  message(FATAL_ERROR "Could not read PLUG_VERSION_HEX from config.h")
endif()
math(EXPR FIRSTSYNTH_VERSION_INT "${CMAKE_MATCH_1}") # AudioComponents.version is this as decimal

# Must precede project(): CMake bakes the deployment target into the compiler
# checks it runs there.
set(FIRSTSYNTH_MACOS_MIN "12.0")
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${FIRSTSYNTH_MACOS_MIN}" CACHE STRING "Minimum macOS version")
endif()

project(FirstSynth VERSION ${FIRSTSYNTH_VERSION})

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

# iPlug2 defaults this to ON, and on macOS its APP deploy step does
# `rm -rf ~/Applications/FirstSynth.app` followed by a symlink to the build tree.
# That is where a notarized app may live (docs/guide/04-macos-distribution.md),
# so never deploy unless asked for on the command line.
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Deploy built plugins to system directories")

include("${IPLUG2_DIR}/iPlug2.cmake")
find_package(iPlug2 REQUIRED)

set(FIRSTSYNTH_FORMATS "APP" CACHE STRING "Plugin formats to build (APP;AUV3;AU;VST3)")
set(ENI_SOURCES
  eni_auth/eni_auth.cpp
  eni_auth/eni_json.cpp
  eni_auth/eni_http_win.cpp
  eni_auth/eni_http_null.cpp
  eni_auth/vendor/monocypher/monocypher.c
  eni_auth/vendor/monocypher/monocypher-ed25519.c
)
if(APPLE)
  list(APPEND ENI_SOURCES eni_auth/eni_http_mac.mm)
  set(ENI_LIBRARIES "-framework Foundation")

  # The macOS bundles carry their version (and minimum OS) as literals in the
  # Info.plists, and those files are only visible from a Mac checkout - a bump
  # made on Windows leaves them behind. Fail configure instead of shipping a
  # bundle that tells the host it is still the previous release (MediSynth
  # shipped 1.0.5 announcing itself as 1.0.3 this way).
  #
  # Note CMake keeps CMAKE_MATCH_<n> from the previous successful match when a
  # regex does not match, so the match result variable is checked first.
  function(firstsynth_expect_plist plist key tag expected source)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/${plist}" _text)
    string(REGEX MATCH "<key>${key}</key>[ \t\r\n]*<${tag}>([^<]*)</${tag}>" _m "${_text}")
    if(NOT _m)
      message(FATAL_ERROR "resources/${plist}: no <${tag}> value for ${key}")
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL expected)
      message(FATAL_ERROR
        "resources/${plist}: ${key} is ${CMAKE_MATCH_1}, ${source} says ${expected}.\n"
        "Bring the macOS plists up to date, e.g. "
        "sd -s '${CMAKE_MATCH_1}' '${expected}' resources/${plist}")
    endif()
  endfunction()
  foreach(_plist IN ITEMS
      "FirstSynth-macOS-Info.plist"
      "FirstSynth-macOS-AUv3-Info.plist"
      "FirstSynth-macOS-AUv3Framework-Info.plist"
      "FirstSynth-VST3-Info.plist")
    firstsynth_expect_plist("${_plist}" CFBundleShortVersionString string "${FIRSTSYNTH_VERSION}" config.h)
    firstsynth_expect_plist("${_plist}" CFBundleVersion string "${FIRSTSYNTH_VERSION}" config.h)
  endforeach()
  # Compare with the target actually in effect (a -DCMAKE_OSX_DEPLOYMENT_TARGET
  # on the command line wins), so the plists can never promise an older macOS
  # than the binaries support.
  foreach(_plist IN ITEMS "FirstSynth-macOS-Info.plist" "FirstSynth-macOS-AUv3-Info.plist" "FirstSynth-VST3-Info.plist")
    firstsynth_expect_plist("${_plist}" LSMinimumSystemVersion string "${CMAKE_OSX_DEPLOYMENT_TARGET}" CMAKE_OSX_DEPLOYMENT_TARGET)
  endforeach()
  # The integer the AU host actually registers (AudioComponents[0].version).
  firstsynth_expect_plist("FirstSynth-macOS-AUv3-Info.plist" version integer "${FIRSTSYNTH_VERSION_INT}" "config.h PLUG_VERSION_HEX")
  # CoreAudio names must be UTF-8 for SWELL and settings.ini, including Japanese.
  set_property(SOURCE "${IPLUG2_DIR}/Dependencies/IPlug/RTAudio/RtAudio.cpp"
    APPEND PROPERTY COMPILE_DEFINITIONS UNICODE)
elseif(WIN32)
  set(ENI_LIBRARIES winhttp)
endif()

file(GLOB FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.preset")

iplug_add_plugin(${PROJECT_NAME}
  FORMATS ${FIRSTSYNTH_FORMATS}
  SOURCES
    ${ENI_SOURCES}
    FirstSynth.cpp
    FirstSynth.h
    resources/resource.h
  WEB_RESOURCES
    resources/web/index.html
    resources/web/script.js
    resources/web/knob-control.js
    resources/web/button-control.js
    resources/web/waveform-display.js
    resources/web/eq-curve-display.js
    resources/web/dragon-bg.svg
  LINK iPlug2::Extras::Synth ${ENI_LIBRARIES}
  UI WEBVIEW
  EXCLUDE_FORMATS WAM
)

if(APPLE AND TARGET FirstSynth-app)
  set_target_properties(FirstSynth-app PROPERTIES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.EASYANDNICE.app.FirstSynth"
  )
endif()

if(APPLE)
  if(TARGET FirstSynthAU-framework)
    target_sources(FirstSynthAU-framework PRIVATE ${FACTORY_PRESETS})
  endif()
  foreach(format app au vst3)
    if(TARGET FirstSynth-${format})
      target_sources(FirstSynth-${format} PRIVATE ${FACTORY_PRESETS})
    endif()
  endforeach()
  set_source_files_properties(${FACTORY_PRESETS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/presets"
  )
endif()

if(TARGET FirstSynthAUv3-appex)
  target_sources(FirstSynthAU-framework PRIVATE FirstSynthAUViewController.mm)
  set_source_files_properties(FirstSynthAUViewController.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set_target_properties(FirstSynthAUv3-appex PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../../../../Frameworks"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.EASYANDNICE.app.FirstSynth.AUv3"
  )
  if(NOT TARGET FirstSynth-app)
    message(FATAL_ERROR "AUV3 requires APP to embed the extension")
  endif()
  set(_app "$<TARGET_BUNDLE_DIR:FirstSynth-app>")
  set(_out "${CMAKE_BINARY_DIR}/out")
  if(CMAKE_GENERATOR STREQUAL "Xcode")
    string(APPEND _out "/$<CONFIG>")
  endif()
  # Re-embed even when only the framework changed and APP did not relink.
  add_custom_target(FirstSynth-auv3-local
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_app}/Contents/Frameworks" "${_app}/Contents/PlugIns"
    COMMAND cp -R "$<TARGET_BUNDLE_DIR:FirstSynthAU-framework>" "${_app}/Contents/Frameworks/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_out}/FirstSynthAUv3.appex" "${_app}/Contents/PlugIns/FirstSynthAUv3.appex"
    COMMAND codesign --force --sign - "${_app}/Contents/Frameworks/FirstSynthAU.framework"
    COMMAND codesign --force --sign - --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/resources/FirstSynth-AUv3.entitlements" "${_app}/Contents/PlugIns/FirstSynthAUv3.appex"
    COMMAND codesign --force --sign - "${_app}"
    COMMAND codesign --verify --deep --strict "${_app}"
    DEPENDS FirstSynth-app
    VERBATIM
  )
endif()
