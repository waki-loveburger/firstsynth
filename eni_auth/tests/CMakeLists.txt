# Standalone test build for the licence check - no iPlug2, no plug-in host.
#
#   cmake -S eni_auth/tests -B build/eni_auth_tests && cmake --build build/eni_auth_tests
#   ./build/eni_auth_tests/eni_auth_tests
#
# The library is plain C/C++ with no dependency on the plug-in, which is the
# point: the licence logic can be exercised on its own, the same way
# 8-Control's Python twin is.

cmake_minimum_required(VERSION 3.14)
project(eni_auth_tests C CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# The .cpp transports are guarded by #if and compile to nothing off their
# platform, so they can be listed unconditionally. The macOS one cannot:
# it is Objective-C++, and a compiler that has no ObjC++ front end fails on
# the file itself before any preprocessor guard is reached.
set(ENI_AUTH_HTTP_SOURCES ../eni_http_win.cpp ../eni_http_null.cpp)
if(APPLE)
  list(APPEND ENI_AUTH_HTTP_SOURCES ../eni_http_mac.mm)
endif()

add_executable(eni_auth_tests
  test_eni_auth.cpp
  ../eni_auth.cpp
  ../eni_json.cpp
  ${ENI_AUTH_HTTP_SOURCES}
  ../vendor/monocypher/monocypher.c
  ../vendor/monocypher/monocypher-ed25519.c
)

if(APPLE)
  target_link_libraries(eni_auth_tests PRIVATE "-framework Foundation")
elseif(WIN32)
  target_link_libraries(eni_auth_tests PRIVATE winhttp)
endif()

# The library's user-facing messages are Japanese, so its sources are UTF-8.
# MSVC otherwise reads them in the machine's ANSI code page, and on a
# Japanese-locale machine (CP932) some byte sequences are not valid there and
# fail to compile - exactly what happened the first time FirstSynth was built
# on Windows (fixed there by adding /utf-8 to the three vcxproj files).
#
# CI cannot catch this: a runner in CP1252 compiles the same source happily.
# Any project that vendors eni_auth/ needs this flag - see the README.
if(MSVC)
  target_compile_options(eni_auth_tests PRIVATE /utf-8)
endif()

find_package(Threads REQUIRED)
target_link_libraries(eni_auth_tests PRIVATE Threads::Threads)

enable_testing()
add_test(NAME eni_auth COMMAND eni_auth_tests)
