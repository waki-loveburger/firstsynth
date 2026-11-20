#!/usr/bin/env python3
"""The latest-redirect body must never drop another instrument's rules."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "r2-latest-rules.py"
MEDISYNTH = {
    "ref": "medisynth_latest_windows",
    "description": "MediSynth latest windows",
    "expression": 'http.host eq "dl.easyandnicewaki.com" and http.request.uri.path eq "/medisynth/latest/windows"',
    "action": "redirect",
    "action_parameters": {"from_value": {"status_code": 302, "target_url": {
        "value": "https://dl.easyandnicewaki.com/medisynth/1.0.10/windows/MediSynth_setup_v1.0.10.exe"},
        "preserve_query_string": False}},
}
STALE_OURS = {"ref": "firstsynth_latest_mac", "description": "FirstSynth latest mac", "action": "redirect"}


def run(current, version="1.0.0", expect=0):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "current.json"
        path.write_text(json.dumps(current) if not isinstance(current, str) else current, encoding="utf-8")
        done = subprocess.run([sys.executable, str(SCRIPT), "--current", str(path), "--version", version],
                              capture_output=True, text=True)
        if done.returncode != expect:
            raise SystemExit(f"FAIL: exit {done.returncode} (expected {expect}): {done.stderr.strip()}")
        return json.loads(done.stdout) if expect == 0 else done.stderr


def check(ok, message):
    if not ok:
        raise SystemExit(f"FAIL: {message}")


# cf may print the ruleset bare or wrapped - both must work, and neither may lose a rule.
for shape in ({"rules": [MEDISYNTH]}, {"result": {"id": "x", "rules": [MEDISYNTH]}}):
    body = run(shape)
    refs = [r["ref"] for r in body["rules"]]
    check(refs.count("medisynth_latest_windows") == 1, f"MediSynth rule lost or duplicated: {refs}")
    check(body["rules"][0] == MEDISYNTH, "another product's rule was rewritten")
    check(sorted(r for r in refs if r.startswith("firstsynth_")) ==
          ["firstsynth_latest_mac", "firstsynth_latest_mac_vst3", "firstsynth_latest_windows"], f"ours: {refs}")

# Our own stale rules are replaced, not stacked.
body = run({"rules": [MEDISYNTH, STALE_OURS]}, version="1.2.3")
refs = [r["ref"] for r in body["rules"]]
check(refs.count("firstsynth_latest_mac") == 1, f"stale rule kept: {refs}")
mac = next(r for r in body["rules"] if r["ref"] == "firstsynth_latest_mac")
check(mac["action_parameters"]["from_value"]["target_url"]["value"] ==
      "https://dl.easyandnicewaki.com/firstsynth/1.2.3/mac/FirstSynth-1.2.3-macOS-arm64.dmg",
      f"wrong target: {mac}")
check(mac["expression"].endswith('eq "/firstsynth/latest/mac"'), f"wrong match: {mac['expression']}")

# Refuse rather than publish an empty ruleset.
check("no rules list" in run({"result": {"id": "x"}}, expect=1), "an input without rules must be refused")
check("not JSON" in run("not json at all", expect=1), "invalid JSON must be refused")
check("X.Y.Z" in run({"rules": []}, version="1.0", expect=1), "a malformed version must be refused")

print("latest redirect rules: PASS")
