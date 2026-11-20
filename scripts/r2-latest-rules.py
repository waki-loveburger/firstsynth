#!/usr/bin/env python3
"""Build the zone's redirect ruleset body for FirstSynth's `latest` download URLs.

The zone's single http_request_dynamic_redirect entrypoint ruleset is shared with
every other instrument (MediSynth owns `medisynth_latest_*`), so the body sent to
Cloudflare must carry their rules unchanged - writing only our three would take
their downloads down. This reads the ruleset as

    cf rulesets entrypoint get http_request_dynamic_redirect -z <zone>

prints it, keeps every rule whose ref is not `firstsynth_latest_*`, appends ours
for the given version, and writes the body for

    cf rulesets entrypoint update http_request_dynamic_redirect -z <zone> --body @<out>

It never guesses: an input it cannot find a rules list in is an error, not an
empty ruleset.
"""
import argparse
import json
import re
import sys

HOST = "dl.easyandnicewaki.com"
PRODUCT = "firstsynth"
# name under medisynth/<version>/ ... keyed by the `latest/<name>` URL
FILES = {
    "windows": "windows/FirstSynth_setup_v{version}.exe",
    "mac": "mac/FirstSynth-{version}-macOS-arm64.dmg",
    "mac-vst3": "mac/FirstSynth-{version}-VST3-macOS-arm64.dmg",
}


def find_rules(doc):
    """The rules list, wherever cf wrapped it (bare ruleset, or under result/...)."""
    stack = [doc]
    while stack:
        node = stack.pop(0)
        if isinstance(node, dict):
            rules = node.get("rules")
            if isinstance(rules, list):
                return rules
            stack.extend(node.values())
        elif isinstance(node, list):
            stack.extend(node)
    raise SystemExit("r2-latest-rules: no rules list in the current ruleset - refusing to replace it")


def ref_of(rule):
    return rule.get("ref") or rule.get("ID") or rule.get("id") or ""


def our_rules(version):
    out = []
    for name, template in FILES.items():
        target = f"https://{HOST}/{PRODUCT}/{version}/{template.format(version=version)}"
        out.append({
            "ref": f"{PRODUCT}_latest_{name.replace('-', '_')}",
            "description": f"FirstSynth latest {name}",
            "expression": f'http.host eq "{HOST}" and http.request.uri.path eq "/{PRODUCT}/latest/{name}"',
            "action": "redirect",
            "action_parameters": {
                "from_value": {
                    "status_code": 302,
                    "target_url": {"value": target},
                    "preserve_query_string": False,
                }
            },
        })
    return out


def merge(current, version):
    kept = [r for r in find_rules(current) if not ref_of(r).startswith(f"{PRODUCT}_latest_")]
    return {"rules": kept + our_rules(version)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--current", required=True,
                        help="the current ruleset as cf printed it ('-' for stdin)")
    parser.add_argument("--version", required=True, help="X.Y.Z, the version being published")
    parser.add_argument("--out", help="where to write the body (default: stdout)")
    args = parser.parse_args(argv)

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        raise SystemExit(f"r2-latest-rules: expected an X.Y.Z version, got {args.version}")
    text = sys.stdin.read() if args.current == "-" else open(args.current, encoding="utf-8").read()
    try:
        current = json.loads(text)
    except json.JSONDecodeError as error:
        raise SystemExit(f"r2-latest-rules: the current ruleset is not JSON ({error})")

    body = merge(current, args.version)
    kept = len(body["rules"]) - len(FILES)
    rendered = json.dumps(body, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        open(args.out, "w", encoding="utf-8").write(rendered)
        print(f"{args.out}: {kept} rule(s) of other products kept, {len(FILES)} for FirstSynth {args.version}",
              file=sys.stderr)
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
