#!/usr/bin/env python3
"""Check the built macOS FirstSynth.vst3: identity, version, minimum OS, architecture,
web resources, signature. Run on the build output and again on the signed copy that ships."""
import plistlib
import re
import subprocess
import sys
from pathlib import Path

WEB_RESOURCES = ("index.html", "script.js", "knob-control.js", "button-control.js",
                 "eq-curve-display.js", "waveform-display.js", "dragon-bg.svg")


def plist(path):
    return plistlib.loads(path.read_bytes().removeprefix(b"\xef\xbb\xbf"))


def check(cond, msg):
    # Not assert: python -O / PYTHONOPTIMIZE would silently skip every check.
    if not cond:
        raise SystemExit(f"verify-vst3: {msg}")


def verify(bundle, expected_version):
    contents = bundle / "Contents"
    info = plist(contents / "Info.plist")
    check(info["CFBundleIdentifier"] == "com.EASYANDNICE.vst3.FirstSynth", f"unexpected id {info['CFBundleIdentifier']}")
    check(info["CFBundleSignature"] == "hnve", "Template signature left in Info.plist")
    check("Acme" not in info.get("CFBundleGetInfoString", ""), "Template copyright left in Info.plist")
    check(info["CFBundleShortVersionString"] == info["CFBundleVersion"] == expected_version, (
        f"Bundle version {info['CFBundleShortVersionString']} does not match config.h {expected_version}"
    ))
    executable = contents / "MacOS" / info["CFBundleExecutable"]
    check(executable.is_file(), f"missing {executable}")
    # The plist must promise the same floor the binary was built for (LC_BUILD_VERSION).
    load_cmds = subprocess.check_output(["otool", "-l", str(executable)], text=True)
    minos = re.search(r"^\s*minos (\d+\.\d+)", load_cmds, re.M)
    check(minos is not None, "no minos in the executable")
    check(info["LSMinimumSystemVersion"] == minos.group(1), (
        f"LSMinimumSystemVersion is {info['LSMinimumSystemVersion']}, binary minos is {minos.group(1)}"
    ))
    archs = subprocess.check_output(["lipo", "-archs", str(executable)], text=True).split()
    check("arm64" in archs, f"Expected an arm64 slice, got {archs}")
    for name in WEB_RESOURCES:
        check((contents / "Resources" / "web" / name).is_file(), f"Missing web resource: {name}")
    check((contents / "PkgInfo").is_file(), "PkgInfo missing")
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    print(f"VST3 bundle {expected_version}: identity, arm64 slice, resources and signature verified")


def config_version(root):
    for line in (root / "config.h").read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("#define PLUG_VERSION_STR"):
            return line.split('"')[1]
    raise SystemExit("PLUG_VERSION_STR not found in config.h")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 scripts/verify-vst3.py path/to/FirstSynth.vst3")
    verify(Path(sys.argv[1]).resolve(), config_version(Path(__file__).resolve().parent.parent))
