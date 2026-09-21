#!/usr/bin/env bash
# Packaging tests for scripts/dist-mac.sh. No Developer ID or network needed:
# a fake bundle is ad-hoc signed and only the DMG steps run.
#
#   bash tests/dist_mac_package_test.sh
#
# Regression for 2026-09-21: the distribution ZIP kept extended attributes as
# AppleDouble ._* files, `unzip` left them inside the bundle, the codesign seal
# broke and Gatekeeper refused the app. The script now makes a DMG only, and
# a bundle carrying extended attributes must come out of it intact.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
# shellcheck source=scripts/dist-mac.sh
source scripts/dist-mac.sh
set +e

work="$(mktemp -d "${TMPDIR:-/tmp}/firstsynth-dist-test.XXXXXX")"
cleanup() {
  for m in "$work"/mnt-*; do
    [ -d "$m" ] && hdiutil detach -quiet "$m" 2>/dev/null
  done
  rm -rf "$work"
}
trap cleanup EXIT

failures=0
check() {
  local name="$1"
  shift
  if "$@" >/dev/null 2>&1; then
    echo "ok   - $name"
  else
    echo "FAIL - $name"
    failures=$((failures + 1))
  fi
}
no_appledouble() { [ -d "$1" ] && [ -z "$(find "$1" -name '._*')" ]; }
links_to() { [ "$(readlink "$1")" = "$2" ]; }
no_link() { [ -d "$1/FirstSynth.vst3" ] && [ ! -e "$1/Applications" ]; }

# A signed bundle whose files carry an extended attribute, like a real build.
fake_bundle() {
  local bundle="$1" package_type="$2"
  mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
  cp /usr/bin/true "$bundle/Contents/MacOS/FirstSynth"
  cat > "$bundle/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>FirstSynth</string>
  <key>CFBundleIdentifier</key><string>com.EASYANDNICE.test.FirstSynth</string>
  <key>CFBundlePackageType</key><string>$package_type</string>
</dict>
</plist>
PLIST
  echo preset > "$bundle/Contents/Resources/preset.txt"
  codesign --force --sign - "$bundle" 2>/dev/null
  xattr -w com.easyandnice.test 1 "$bundle/Contents/Info.plist"
  xattr -w com.easyandnice.test 1 "$bundle/Contents/Resources/preset.txt"
}

# 1-2. Version and artifact names
printf '#define PLUG_NAME "FirstSynth"\r\n#define PLUG_VERSION_STR "9.8.7"\r\n' > "$work/config.h"
check "reads PLUG_VERSION_STR from a CRLF config.h" \
  [ "$(firstsynth_version "$work/config.h")" = "9.8.7" ]
check "app artifact name" \
  [ "$(artifact_name app 9.8.7 arm64)" = "FirstSynth-9.8.7-macOS-arm64" ]
check "vst3 artifact name" \
  [ "$(artifact_name vst3 9.8.7 arm64)" = "FirstSynth-9.8.7-VST3-macOS-arm64" ]

# 3. App DMG, and a bundle with extended attributes survives it (regression
# for 2026-09-21)
fake_bundle "$work/src/FirstSynth.app" APPL
check "fake bundle is validly signed before packaging" \
  codesign --verify --deep --strict "$work/src/FirstSynth.app"
stage_payload "$work/src/FirstSynth.app" "$work/stage-app"
make_dmg app "$work/stage-app" "$work/app.dmg" "FirstSynth test"
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$work/mnt-app" "$work/app.dmg"
check "app DMG contains FirstSynth.app" [ -d "$work/mnt-app/FirstSynth.app" ]
check "app DMG links Applications to /Applications" links_to "$work/mnt-app/Applications" /Applications
check "app DMG carries THIRD-PARTY-NOTICES.txt" [ -f "$work/mnt-app/THIRD-PARTY-NOTICES.txt" ]
check "bundle inside the app DMG keeps its signature" \
  codesign --verify --deep --strict "$work/mnt-app/FirstSynth.app"
mkdir -p "$work/installed"
cp -R "$work/mnt-app/FirstSynth.app" "$work/installed/"
hdiutil detach -quiet "$work/mnt-app"
check "bundle copied out of the DMG has no ._*" no_appledouble "$work/installed/FirstSynth.app"
check "bundle copied out of the DMG keeps its signature" \
  codesign --verify --deep --strict "$work/installed/FirstSynth.app"

# 4. VST3 DMG has no Applications link
fake_bundle "$work/src/FirstSynth.vst3" BNDL
stage_payload "$work/src/FirstSynth.vst3" "$work/stage-vst3"
make_dmg vst3 "$work/stage-vst3" "$work/vst3.dmg" "FirstSynth VST3 test"
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$work/mnt-vst3" "$work/vst3.dmg"
check "vst3 DMG contains FirstSynth.vst3" [ -d "$work/mnt-vst3/FirstSynth.vst3" ]
check "vst3 DMG has no Applications link" no_link "$work/mnt-vst3"
hdiutil detach -quiet "$work/mnt-vst3"

# 5. Unknown kind stops before touching anything
bash scripts/dist-mac.sh bogus >/dev/null 2>&1
check "unknown kind exits with 2" [ $? -eq 2 ]

if [ "$failures" -ne 0 ]; then
  echo "$failures failed"
  exit 1
fi
echo "all passed"
