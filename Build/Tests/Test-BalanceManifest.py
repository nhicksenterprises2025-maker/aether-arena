"""Arithmetic and provenance regressions; synthetic fixtures never enter game data."""
import copy
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Build"))
from generate_card_manifest import balance_hash, generate, generate_arena, validate
from balance_patch_notes import build_notes, numeric_change


def near(actual, expected):
    assert math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-12), (actual, expected)


def main():
    current = json.loads((ROOT / "Assets/Data/card_manifest.json").read_text())
    baseline = json.loads((ROOT / "Assets/Data/Baselines/1.5.0.json").read_text())
    validate(current); validate(baseline)
    expanded = json.loads((ROOT / "Assets/Data/Baselines/1.5.0-expanded-schema.json").read_text())
    validate(expanded)
    assert expanded["schema"] == 2 and expanded["derivedFrom"]["balanceHash"] == baseline["balanceHash"]
    for before, derivative in zip(baseline["cards"], expanded["cards"]):
        assert all(derivative[key] == value for key, value in before.items())
    assert len(current["cards"]) == 22 and len(current["entities"]) == 1
    assert current["rules"]["arenaWidth"] == 22 and current["rules"]["arenaHeight"] == 40
    release_baseline = json.loads((ROOT / current["baseline"]).read_text())
    assert current["cards"] == release_baseline["cards"]
    dart = next(card for card in current["cards"] if card["id"] == "dart_gun")
    assert (dart["cost"], dart["damage"], dart["attackInterval"], dart["range"]) == (3, 50, .4, 8.5)
    near(dart["damage"] / dart["attackInterval"], 125)
    assert current["entities"] == release_baseline["entities"]
    assert current["rules"] == dict(release_baseline["rules"], navigationRevision=13, guardCenterX=6.76)
    assert current["balanceHash"] != release_baseline["balanceHash"]
    assert (ROOT / "Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftArenaRules.generated.inc").read_text() == generate_arena(current)
    assert (ROOT / "Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCardManifest.generated.inc").read_text() == generate(current)
    visual = copy.deepcopy(current)
    visual["cards"][0].update(name="Synthetic renamed illustration", scale=3)
    visual.update(version="synthetic", status="synthetic")
    assert balance_hash(visual) == current["balanceHash"]
    mechanics = copy.deepcopy(current); mechanics["cards"][0]["collisionRadius"] += .01
    assert balance_hash(mechanics) != current["balanceHash"]
    mutations = [
        lambda m: m["cards"][0].update(shieldHP=-1),
        lambda m: m["cards"][0].update(shieldOverflowProtected=True),
        lambda m: m["cards"][0].update(hardExpiry=True),
        lambda m: m["cards"][0].update(deathSpawnCount=4),
        lambda m: m["cards"][0].update(deathSpawnCount=4, deathSpawnId="missing_child"),
        lambda m: m["cards"][0].update(deathSpawnCount=1.5, deathSpawnId="bone_warden_skeleton"),
        lambda m: m["entities"][0].update(cost=1),
        lambda m: m["entities"][0].update(deathSpawnCount=1, deathSpawnId="bone_warden_skeleton"),
        lambda m: m["cards"][0].update(invisibilityDuration=1),
        lambda m: m["cards"][0].update(dotStructureDamage=15),
        lambda m: m["cards"][0].update(chargeMoveSeconds="1.5"),
        lambda m: m["cards"][0].update(breakExistingLocks=1),
        lambda m: m["rules"].update(arenaWidth=25),
        lambda m: m["rules"].update(arenaHeight=0),
        lambda m: m["rules"].update(bridgeCenterX=11),
        lambda m: m["rules"].update(guardCenterX=True),
        lambda m: m["rules"].update(guardCenterX=0),
        lambda m: m["rules"].update(guardCenterX=float("inf")),
        lambda m: m["rules"].update(guardCenterX=11),
        lambda m: m["rules"].update(coreDepth=m["rules"]["arenaHeight"] / 2 - 1),
        lambda m: m["rules"].pop("guardDepth")]
    for mutate in mutations:
        fixture = copy.deepcopy(current);mutate(fixture)
        try:
            fixture["balanceHash"] = balance_hash(fixture)
            validate(fixture)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid expansion definition passed validation")
    near(numeric_change("hp", 840, 822)["percent"], -2.142857142857143)
    near(numeric_change("damage", 96, 93)["percent"], -3.125)
    near(numeric_change("range", 6.5, 6)["percent"], -7.6923076923076925)
    near(numeric_change("attackInterval", 1.1, .9)["percent"], -18.181818181818187)
    near(numeric_change("attackFrequency", 1 / 1.1, 1 / .9)["percent"], 22.222222222222243)
    assert numeric_change("attackInterval", 1.1, .9)["benefit"] == 1
    assert numeric_change("shieldHP", 0, 310)["percent"] is None
    assert numeric_change("shieldHP", 0, 310)["zeroTransition"] == "ADDED"
    assert numeric_change("shieldHP", 310, 0)["zeroTransition"] == "REMOVED"
    near(numeric_change("slowPct", .1, .3)["percentagePoints"], 20)
    before = copy.deepcopy(baseline); after = copy.deepcopy(before)
    after.update(status="approved", version="synthetic-fixture-only")
    card = after["cards"][0]; old = before["cards"][0]
    old.update(damage=96, attackInterval=1.1); card.update(damage=93, attackInterval=.9)
    before["balanceHash"] = balance_hash(before); after["balanceHash"] = balance_hash(after)
    notes = build_notes(before, after, {"status": "approved", "unresolved": False})
    assert len(notes["panels"]) == 1
    rows = {row["key"]: row for row in notes["panels"][0]["changes"]}
    near(rows["basicDPS"]["percent"], 100 * ((93 / .9) / (96 / 1.1) - 1))
    assert notes["panels"][0]["label"] == "MIXED"
    after["status"] = "pending"
    try:
        build_notes(before, after, {"status": "approved", "unresolved": False})
    except ValueError:
        pass
    else:
        raise AssertionError("Pending decisions generated final patch notes")
    print(json.dumps({"passed": True, "fixtureOnly": True, "checks": 18,
                      "schemaMutationCases": len(mutations), "derivativeCapturedFieldsUnchanged": True,
                      "baselineVersion": baseline["version"], "baselineHash": baseline["balanceHash"],
                      "currentStatus": current["status"], "currentHash": current["balanceHash"]}))


if __name__ == "__main__":
    main()
