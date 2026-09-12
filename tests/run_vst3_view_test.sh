#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="${VST3_SDK_DIR:-$PWD/private/vst3sdk}"
sdk_build="${VST3_SDK_BUILD:-$PWD/build-mac-vst3-sdk}"
mkdir -p build-mac-vst3-tests
test_binary="$PWD/build-mac-vst3-tests/view-test"
clang++ -std=c++17 -fobjc-arc -DRELEASE -I "$sdk" \
  tests/vst3_view_test.mm \
  "$sdk/public.sdk/source/vst/hosting/module_mac.mm" \
  "$sdk/public.sdk/source/vst/hosting/plugprovider.cpp" \
  "$sdk_build/lib/Release/libsdk_hosting.a" \
  "$sdk_build/lib/Release/libsdk_common.a" \
  "$sdk_build/lib/Release/libbase.a" \
  "$sdk_build/lib/Release/libpluginterfaces.a" \
  -framework AppKit -framework WebKit -o "$test_binary"
"$test_binary" "${1:-$PWD/build-mac-vst3/out/Release/FirstSynth.vst3}"
