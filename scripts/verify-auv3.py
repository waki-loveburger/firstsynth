#!/usr/bin/env python3
"""Check the built FirstSynth.app (standalone + AUv3), including the extension's
framework lookup. Run on the build output and again on the signed copy that ships."""
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
        raise SystemExit(f"verify-auv3: {msg}")


def binary_minos(executable):
    # The deployment target the binary was actually built for (LC_BUILD_VERSION).
    out = subprocess.check_output(["otool", "-l", str(executable)], text=True)
    m = re.search(r"^\s*minos (\d+\.\d+)", out, re.M)
    if not m:
        raise SystemExit(f"verify-auv3: no minos in {executable}")
    return m.group(1)


def verify(app, expected_version, expected_version_int):
    contents = app / "Contents"
    appex = contents / "PlugIns/FirstSynthAUv3.appex/Contents"
    framework = contents / "Frameworks/FirstSynthAU.framework"
    app_info = plist(contents / "Info.plist")
    ext_info = plist(appex / "Info.plist")
    fw_info = plist(framework / "Resources/Info.plist")
    ext = ext_info["NSExtension"]
    attrs = ext["NSExtensionAttributes"]
    component = attrs["AudioComponents"][0]
    check((component["type"], component["subtype"], component["manufacturer"]) == ("aumu", "hnve", "EZAN"),
          "AU identity does not match FirstSynth")
    check(attrs["AudioComponentBundle"] == fw_info["CFBundleIdentifier"], "AudioComponentBundle does not name the framework")
    check(ext_info["CFBundleIdentifier"].startswith(app_info["CFBundleIdentifier"] + "."), "appex id is not under the app id")
    # Compare against config.h, not just the three bundles against each other:
    # they are separate files and a missed bump leaves all three agreeing on the
    # previous version (MediSynth 1.0.5 shipped announcing itself as 1.0.3).
    for name, info in (("app", app_info), ("appex", ext_info), ("framework", fw_info)):
        check(info["CFBundleShortVersionString"] == info["CFBundleVersion"] == expected_version, (
            f"{name} bundle version {info['CFBundleShortVersionString']} does not match config.h {expected_version}"
        ))
    # The integer the AU host registers and shows (auval "Component Version").
    check(component["version"] == expected_version_int, (
        f"AudioComponents version {component['version']} (0x{component['version']:08x}) "
        f"does not match config.h PLUG_VERSION_HEX 0x{expected_version_int:08x}"
    ))
    # The plist must promise the same floor the binaries were built for.
    minos = binary_minos(contents / "MacOS" / app_info["CFBundleExecutable"])
    for name, info in (("app", app_info), ("appex", ext_info)):
        check(info.get("LSMinimumSystemVersion") == minos, (
            f"{name} LSMinimumSystemVersion is {info.get('LSMinimumSystemVersion')}, binary minos is {minos}"
        ))
        check("Acme" not in info.get("CFBundleGetInfoString", ""), f"{name}: template copyright left in Info.plist")
    check(ext["NSExtensionPrincipalClass"] == "FirstSynthAUViewController", "principal class is not FirstSynthAUViewController")
    executable = appex / "MacOS" / ext_info["CFBundleExecutable"]
    check(executable.is_file(), f"missing {executable}")
    signed_entitlements = subprocess.check_output(
        ["codesign", "-d", "--entitlements", ":-", str(appex.parent)],
        stderr=subprocess.DEVNULL,
    )
    entitlements = plistlib.loads(signed_entitlements)
    check(entitlements.get("com.apple.security.app-sandbox") is True, "appex is not sandboxed")
    check(entitlements.get("com.apple.security.network.client") is True, (
        "WKWebView's Networking process requires network.client, even for bundled HTML"
    ))
    check((framework / fw_info["CFBundleExecutable"]).is_file(), "Framework executable missing")
    for base in (contents / "Resources", framework / "Resources"):
        for name in WEB_RESOURCES:
            check((base / "web" / name).is_file(), f"Missing web resource: {base}/{name}")
    deps = subprocess.check_output(["otool", "-L", str(executable)], text=True)
    dependency = "@rpath/FirstSynthAU.framework/Versions/A/FirstSynthAU"
    check(dependency in deps, "Framework must use @rpath")
    commands = subprocess.check_output(["otool", "-l", str(executable)], text=True)
    rpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", commands)
    resolved = [Path(p.replace("@executable_path", str(executable.parent))) /
                dependency.removeprefix("@rpath/") for p in rpaths]
    check(any(p.is_file() and p.resolve().is_relative_to(app.resolve()) for p in resolved), (
        f"Framework cannot be resolved inside app: {rpaths}"
    ))
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    print(f"AUv3 bundle {expected_version}: identity, resources, runtime paths and signature verified")


def config_versions(root):
    text = root.joinpath("config.h").read_text(encoding="utf-8-sig")
    ver = re.search(r'^#define PLUG_VERSION_STR "([^"]+)"', text, re.M)
    hexv = re.search(r"^#define PLUG_VERSION_HEX (0x[0-9A-Fa-f]+)", text, re.M)
    if not ver or not hexv:
        raise SystemExit("PLUG_VERSION_STR / PLUG_VERSION_HEX not found in config.h")
    return ver.group(1), int(hexv.group(1), 16)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 scripts/verify-auv3.py path/to/FirstSynth.app")
    verify(Path(sys.argv[1]).resolve(), *config_versions(Path(__file__).resolve().parent.parent))
