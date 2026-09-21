#!/usr/bin/env bash
# Developer ID signing, notarization and stapling for the macOS artifacts.
#
#   scripts/dist-mac.sh app  [build-mac-auv3/out/Release/FirstSynth.app]  [build-mac-auv3/dist]
#   scripts/dist-mac.sh vst3 [build-mac-vst3/out/Release/FirstSynth.vst3] [build-mac-vst3/dist]
#
# Copies the bundle into the dist directory, signs it inside-out with the
# hardened runtime, submits a ZIP to Apple, staples the ticket and writes the
# distribution ZIP and DMG. Re-running the auv3-local target replaces the
# signature, so build first, then run this. Never commit the dist directory.
#
# The DMG is what goes on the shared drive. A ZIP has to be extracted, and
# anything but Finder (Drive's "download as ZIP", third-party unarchivers)
# can scatter AppleDouble ._* files into the bundle and break the codesign
# seal - that is how FirstSynth got refused by Gatekeeper on 2026-09-21. A
# DMG mounts as-is, and is signed, notarized and stapled on its own.
#
# Ported from medisynth's scripts/dist-mac.sh. The packaging steps are
# functions so tests/dist_mac_package_test.sh can run them without signing.
set -euo pipefail

firstsynth_version() {
  sed -n 's/^#define PLUG_VERSION_STR "\(.*\)"/\1/p' "${1:-config.h}" | tr -d '\r'
}

# artifact_name <app|vst3> <version> <arch>
artifact_name() {
  case "$1" in
    app) echo "FirstSynth-$2-macOS-$3" ;;
    vst3) echo "FirstSynth-$2-VST3-macOS-$3" ;;
  esac
}

# stage_payload <bundle> <stage>: the stapled bundle plus the third-party
# notices (VST3 SDK licence).
stage_payload() {
  rm -rf "$2"
  mkdir -p "$2"
  ditto "$1" "$2/$(basename "$1")"
  cp THIRD-PARTY-NOTICES.txt "$2/"
}

# make_zip <stage> <zip>
make_zip() {
  rm -f "$2"
  # --norsrc: extended attributes would become AppleDouble ._* files, which
  # anything but Finder extracts into the bundle and breaks the seal.
  ditto -c -k --norsrc "$1" "$2"
}

# make_dmg <app|vst3> <stage> <dmg> <volname>: same payload as the ZIP, plus a
# drag target for the app.
make_dmg() {
  if [ "$1" = app ]; then
    ln -s /Applications "$2/Applications"
  fi
  rm -f "$3"
  hdiutil create -quiet -volname "$4" -srcfolder "$2" -format UDZO -ov "$3"
}

# notarize <profile> <file> <log>: stops the script unless Apple accepts it.
notarize() {
  xcrun notarytool submit "$2" --keychain-profile "$1" --wait | tee "$3"
  grep -q "status: Accepted" "$3"
}

main() {
  cd "$(dirname "$0")/.."
  local kind="${1:?app or vst3}"
  local sign_id="${FIRSTSYNTH_SIGN_ID:-Developer ID Application: Anycreative Inc. (3EQKG4B352)}"
  local profile="${FIRSTSYNTH_NOTARY_PROFILE:-easyandnice-notary}"
  local version arch src dist name volname
  version="$(firstsynth_version)"
  arch="$(uname -m)"

  case "$kind" in
    app)
      src="${2:-build-mac-auv3/out/Release/FirstSynth.app}"
      dist="${3:-build-mac-auv3/dist}"
      name="FirstSynth.app"
      volname="FirstSynth ${version}"
      ;;
    vst3)
      src="${2:-build-mac-vst3/out/Release/FirstSynth.vst3}"
      dist="${3:-build-mac-vst3/dist}"
      name="FirstSynth.vst3"
      volname="FirstSynth VST3 ${version}"
      ;;
    *) echo "unknown kind: $kind (app or vst3)" >&2; exit 2 ;;
  esac
  case "$dist" in /*) ;; *) dist="$PWD/$dist" ;; esac
  local base zip dmg bundle stage
  base="$(artifact_name "$kind" "$version" "$arch")"
  zip="$dist/$base.zip"
  dmg="$dist/$base.dmg"
  bundle="$dist/$name"
  stage="$dist/stage"

  rm -rf "$dist"
  mkdir -p "$dist"
  ditto "$src" "$bundle"

  if [ "$kind" = app ]; then
    codesign --force --timestamp --options runtime --sign "$sign_id" \
      "$bundle/Contents/Frameworks/FirstSynthAU.framework"
    codesign --force --timestamp --options runtime --sign "$sign_id" \
      --entitlements resources/FirstSynth-AUv3.entitlements \
      "$bundle/Contents/PlugIns/FirstSynthAUv3.appex"
  fi
  codesign --force --timestamp --options runtime --sign "$sign_id" "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"

  # This ZIP only goes to Apple, so keeping the extended attributes is harmless.
  ditto -c -k --keepParent "$bundle" "$dist/submit.zip"
  notarize "$profile" "$dist/submit.zip" "$dist/notarize.log"
  rm -f "$dist/submit.zip"
  xcrun stapler staple "$bundle"
  xcrun stapler validate "$bundle"
  if [ "$kind" = app ]; then
    spctl --assess --type execute --verbose=2 "$bundle"
  fi

  stage_payload "$bundle" "$stage"
  make_zip "$stage" "$zip"
  shasum -a 256 "$zip" | tee "$zip.sha256"

  make_dmg "$kind" "$stage" "$dmg" "$volname"
  rm -rf "$stage"
  codesign --force --timestamp --sign "$sign_id" "$dmg"
  notarize "$profile" "$dmg" "$dist/notarize-dmg.log"
  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
  spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
  shasum -a 256 "$dmg" | tee "$dmg.sha256"
  echo "OK: $zip"
  echo "OK: $dmg  <- share this one"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi
