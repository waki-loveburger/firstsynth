#!/usr/bin/env bash
# Developer ID signing, notarization and stapling for the macOS artifacts.
#
#   scripts/dist-mac.sh app  [build-mac-auv3/out/Release/FirstSynth.app]  [build-mac-auv3/dist]
#   scripts/dist-mac.sh vst3 [build-mac-vst3/out/Release/FirstSynth.vst3] [build-mac-vst3/dist]
#
# Copies the bundle into the dist directory, signs it inside-out with the
# hardened runtime, submits a ZIP to Apple, staples the ticket and writes the
# distribution DMG. Re-running the auv3-local target replaces the signature,
# so build first, then run this. Never commit the dist directory.
#
# Only a DMG is made, and it is what goes on the shared drive. A ZIP has to be
# extracted, and anything but Finder (Drive's "download as ZIP", third-party
# unarchivers) can scatter AppleDouble ._* files into the bundle and break the
# codesign seal - that is how FirstSynth got refused by Gatekeeper on
# 2026-09-21. A DMG mounts as-is, and is signed, notarized and stapled on its own.
#
# On any failure the partially built DMG and the submission ZIP are removed,
# and a rejected notarization leaves its reasons in dist/notarize*-issues.json.
#
# Ported from medisynth's scripts/dist-mac.sh, minus its distribution ZIP
# (mako, 2026-09-21). The packaging steps are functions so
# tests/dist_mac_package_test.sh can run them without signing.
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

# stage_payload <app|vst3> <bundle> <stage> <version> <arch>: the stapled
# bundle, the third-party notices (VST3 SDK licence) and a note on where the
# bundle goes.
stage_payload() {
  local kind="$1" bundle="$2" stage="$3" version="$4" arch="$5"
  rm -rf "$stage"
  mkdir -p "$stage"
  ditto "$bundle" "$stage/$(basename "$bundle")"
  cp THIRD-PARTY-NOTICES.txt "$stage/"
  if [ "$kind" = app ]; then
    cat > "$stage/README.txt" <<TXT
FirstSynth ${version} (macOS, ${arch})

FirstSynth.app を Applications フォルダへドラッグしてください。
Logic などの AUv3 ホストでは、アプリを一度起動すると音源として登録されます。

Drag FirstSynth.app to the Applications folder. Launch it once so AUv3 hosts
such as Logic pick up the instrument.
TXT
  else
    cat > "$stage/README.txt" <<TXT
FirstSynth ${version} VST3 (macOS, ${arch})

FirstSynth.vst3 を次のフォルダへコピーしてください（無ければ作成）:
  ~/Library/Audio/Plug-Ins/VST3/
Finder で「移動」→「フォルダへ移動」に上のパスを貼り付けると開けます。
コピー後、ホスト（Ableton Live など）でプラグインを再スキャンしてください。

Copy FirstSynth.vst3 to ~/Library/Audio/Plug-Ins/VST3/ (create it if missing),
then rescan plug-ins in your host.
TXT
  fi
}

# make_dmg <app|vst3> <stage> <dmg> <volname>: the payload, plus a drag
# target for the app.
make_dmg() {
  if [ "$1" = app ]; then
    ln -s /Applications "$2/Applications"
  fi
  rm -f "$3"
  hdiutil create -quiet -volname "$4" -srcfolder "$2" -format UDZO -ov "$3"
}

# notarize <profile> <file> <log>: stops the script unless Apple accepts it,
# saving Apple's reasons next to the log when it does not.
notarize() {
  local profile="$1" file="$2" log="$3" id
  xcrun notarytool submit "$file" --keychain-profile "$profile" --wait | tee "$log"
  if ! grep -q "status: Accepted" "$log"; then
    id="$(sed -n 's/^ *id: \([0-9a-f-]*\)$/\1/p' "$log" | head -1)"
    [ -n "$id" ] && xcrun notarytool log "$id" --keychain-profile "$profile" "${log%.log}-issues.json" || true
    echo "notarization of $(basename "$file") was not accepted; see ${log%.log}-issues.json" >&2
    exit 1
  fi
}

main() {
  cd "$(dirname "$0")/.."
  local kind="${1:?app or vst3}"
  local sign_id="${FIRSTSYNTH_SIGN_ID:-Developer ID Application: Anycreative Inc. (3EQKG4B352)}"
  local profile="${FIRSTSYNTH_NOTARY_PROFILE:-easyandnice-notary}"
  local version src dist name volname
  version="$(firstsynth_version)"

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

  # Absolute paths, so the positional overrides work from anywhere, and a guard
  # so a mistyped dist can never delete the build output it copies from.
  [ -d "$src" ] || { echo "not a bundle: $src" >&2; exit 2; }
  src="$(cd "$(dirname "$src")" && pwd)/$(basename "$src")"
  mkdir -p "$dist"
  dist="$(cd "$dist" && pwd)"
  case "$src" in "$dist"/*) echo "dist ($dist) must not contain src ($src)" >&2; exit 2 ;; esac

  rm -rf "$dist"
  mkdir -p "$dist"
  local bundle="$dist/$name" stage="$dist/stage" arch dmg partial
  ditto "$src" "$bundle"
  arch="$(lipo -archs "$bundle/Contents/MacOS/FirstSynth" | tr ' ' '-')"
  dmg="$dist/$(artifact_name "$kind" "$version" "$arch").dmg"
  # Built under a .partial name and only renamed once it is signed, notarized
  # and stapled, so a half-made DMG never looks like a finished one.
  partial="${dmg%.dmg}.partial.dmg"
  trap 'rm -f "$partial" "$dist/submit.zip"' EXIT

  if [ "$kind" = app ]; then
    codesign --force --timestamp --options runtime --sign "$sign_id" \
      "$bundle/Contents/Frameworks/FirstSynthAU.framework"
    codesign --force --timestamp --options runtime --sign "$sign_id" \
      --entitlements resources/FirstSynth-AUv3.entitlements \
      "$bundle/Contents/PlugIns/FirstSynthAUv3.appex"
  fi
  codesign --force --timestamp --options runtime --sign "$sign_id" "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"
  # Same checks as on the build output, now on the bundle that actually ships:
  # identity, version against config.h, minimum OS against the binary,
  # entitlements (the AUv3 network.client one in particular).
  python3 "scripts/verify-${kind//app/auv3}.py" "$bundle"

  # This ZIP only goes to Apple, so keeping the extended attributes is harmless.
  ditto -c -k --keepParent "$bundle" "$dist/submit.zip"
  notarize "$profile" "$dist/submit.zip" "$dist/notarize.log"
  rm -f "$dist/submit.zip"
  xcrun stapler staple "$bundle"
  xcrun stapler validate "$bundle"
  if [ "$kind" = app ]; then
    spctl --assess --type execute --verbose=2 "$bundle"
  fi

  stage_payload "$kind" "$bundle" "$stage" "$version" "$arch"
  make_dmg "$kind" "$stage" "$partial" "$volname"
  rm -rf "$stage"
  codesign --force --timestamp --sign "$sign_id" "$partial"
  notarize "$profile" "$partial" "$dist/notarize-dmg.log"
  xcrun stapler staple "$partial"
  xcrun stapler validate "$partial"
  spctl --assess --type open --context context:primary-signature --verbose=2 "$partial"
  mv "$partial" "$dmg"
  shasum -a 256 "$dmg" | tee "$dmg.sha256"
  echo "OK: $dmg  <- share this one"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi
