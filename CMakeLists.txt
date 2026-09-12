cmake_minimum_required(VERSION 3.14)
project(FirstSynth VERSION 1.0.0)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

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
