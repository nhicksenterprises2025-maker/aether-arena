"""Compare actual native imports with the sealed first-patch card definitions."""
import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD_IDS = {
    "ironclad", "ember_archer", "twin_blades", "boulderback", "arc_mage",
    "rambeast", "sky_manta", "vampire_bats", "frost_fang", "storm_raven",
    "meteor_shards", "archer_tower", "bullet_burst", "nova_flask",
    "mini_stampede", "stampede",
}
PULVERIZER = {
    "id": "pulverizer", "name": "Pulverizer", "cost": 3, "count": 1,
    "hp": 700, "damage": 65, "attackInterval": .9, "moveSpeed": 0,
    "range": 4, "scale": .95, "projectileSpeed": 0, "shockwaveSpeed": 8,
    "splash": 4, "lifetime": 45, "footprint": 1.65, "towerDamage": 0,
    "spellRadius": 0, "castDelay": 0, "chargeDamage": 0, "slowPct": .1,
    "slowDuration": 1.5, "auraDamage": 0, "auraInterval": 0, "auraRadius": 0,
    "stunDuration": 0, "dotDamage": 0, "dotDuration": 0, "dotInterval": 0,
    "rounds": 0, "flying": False, "canHitAir": False, "structuresOnly": False,
    "spell": False, "building": True,
}


def resolve(value):
    path = (ROOT / value).resolve()
    path.relative_to(ROOT)
    return path


def pin(path):
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def same_value(actual, expected):
    if isinstance(expected, bool):
        return type(actual) is bool and actual == expected
    if isinstance(expected, (int, float)):
        return isinstance(actual, (int, float)) and not isinstance(actual, bool) and actual == expected
    return type(actual) is type(expected) and actual == expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", default="Artifacts/QA/unreal_asset_import.json")
    parser.add_argument("--baseline", default="Artifacts/QA/History-1.4.0/unreal_asset_import.json")
    parser.add_argument("--sealed", default="Docs/QA/balance-patch-1.4.0.json")
    parser.add_argument("--output", default="Artifacts/QA/Pulverizer/native-stat-preservation.json")
    args = parser.parse_args()
    paths = [resolve(args.baseline), resolve(args.current), resolve(args.sealed), Path(__file__).resolve()]
    before = [pin(path) for path in paths]
    baseline, current, sealed = [json.loads(path.read_text(encoding="utf-8-sig")) for path in paths[:3]]
    output = resolve(args.output)
    if output.exists():
        raise SystemExit("Output already exists; choose a new --output to preserve prior evidence.")
    checks, failures = [], []

    def check(condition, label):
        checks.append({"check": label, "passed": bool(condition)})
        if not condition:
            failures.append(label)

    check(sealed.get("passed") is True and sealed.get("version") == "1.4.0", "Baseline QA is the sealed passed 1.4.0 report")
    baseline_pins = [item for item in sealed.get("inputs", []) if item.get("path") == "Artifacts/QA/unreal_asset_import.json"]
    check(len(baseline_pins) == 1, "Sealed QA contains one authoritative native import identity")
    if len(baseline_pins) == 1:
        accepted = baseline_pins[0]
        check(before[0]["sha256"] == accepted.get("sha256") and before[0]["bytes"] == accepted.get("bytes"),
              "Archived baseline bytes exactly match the original sealed import")
    for report, label in [(baseline, "Baseline"), (current, "Current")]:
        check(report.get("passed") is True and report.get("errors") == [], label + " is an actual successful native import")
    old_rows, new_rows = baseline["cardDefinitions"], current["cardDefinitions"]
    old = {row["id"]: row for row in old_rows}
    new = {row["id"]: row for row in new_rows}
    check(len(old_rows) == len(old) == 16 and set(old) == OLD_IDS, "Baseline contains the sixteen unique published cards")
    check(len(new_rows) == len(new) == 17 and set(new) == OLD_IDS | {"pulverizer"}, "Current export adds only the one Pulverizer")
    check([row["id"] for row in new_rows if row["id"] != "pulverizer"] == [row["id"] for row in old_rows],
          "Every old card retains its native catalog order")
    for card_id in sorted(OLD_IDS):
        if card_id not in old or card_id not in new:
            continue
        check(set(new[card_id]) == set(old[card_id]) | {"shockwaveSpeed"}, card_id + " retains its complete original exported fields")
        for field, value in old[card_id].items():
            actual = new[card_id].get(field)
            check(same_value(actual, value), card_id + "." + field + " unchanged")
        check(same_value(new[card_id].get("shockwaveSpeed"), 0), card_id + " gains no shockwave attack")
    if "pulverizer" in new:
        check(set(new["pulverizer"]) == set(PULVERIZER), "Pulverizer has exactly the complete expected definition fields")
        for field, value in PULVERIZER.items():
            actual = new["pulverizer"].get(field)
            check(same_value(actual, value), "pulverizer." + field + " matches the requested contract")
        check(round(new["pulverizer"]["damage"] / new["pulverizer"]["attackInterval"], 2) == 72.22, "Pulverizer basic DPS is derived from damage and interval")
    after = [pin(path) for path in paths]
    check(before == after, "All baseline, current, sealed QA and verifier inputs remain unchanged")
    report = {"schema": "rift.pulverizer.native-stat-preservation.v1", "utc": datetime.now(timezone.utc).isoformat(),
              "passed": not failures, "checks": len(checks), "failures": failures,
              "priorCards": 16, "addedCards": ["pulverizer"], "inputs": before, "results": checks,
              "scope": "Actual native-export definitions only; gameplay, asset, visual and Windows checks have separate evidence."}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "checks": len(checks), "failures": failures,
                      "report": output.relative_to(ROOT).as_posix()}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
