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

add_executable(eni_auth_tests
  test_eni_auth.cpp
  ../eni_auth.cpp
  ../eni_json.cpp
  ../eni_http_win.cpp
  ../eni_http_mac.mm
  ../eni_http_null.cpp
  ../vendor/monocypher/monocypher.c
  ../vendor/monocypher/monocypher-ed25519.c
)

if(APPLE)
  target_link_libraries(eni_auth_tests PRIVATE "-framework Foundation")
elseif(WIN32)
  target_link_libraries(eni_auth_tests PRIVATE winhttp)
endif()

find_package(Threads REQUIRED)
target_link_libraries(eni_auth_tests PRIVATE Threads::Threads)

enable_testing()
add_test(NAME eni_auth COMMAND eni_auth_tests)
