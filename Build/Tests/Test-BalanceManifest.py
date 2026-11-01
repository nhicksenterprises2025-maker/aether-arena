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
    assert len(current["cards"]) == 24 and len(current["entities"]) == 1
    assert current["rules"]["arenaWidth"] == 22 and current["rules"]["arenaHeight"] == 40
    release_baseline = json.loads((ROOT / current["baseline"]).read_text())
    by_id = {card["id"]:card for card in current["cards"]}
    for before in release_baseline["cards"] + release_baseline["entities"]:
        after = by_id.get(before["id"]) or next(c for c in current["entities"] if c["id"] == before["id"])
        expected = dict(before, spawnId="", spawnInterval=0)
        if before["id"] in ("mini_stampede", "stampede"):
            expected.update(hp=133.2, damage=24)
            near(after["hp"] / before["hp"], 1.2)
            near(after["damage"] / before["damage"], 1.2)
        if before["id"] == "mini_stampede": expected["cost"] = 4
        assert after == expected, before["id"]
    storm, box = by_id["thunderstorm"], by_id["blade_box"]
    assert (storm["cost"],storm["spellRadius"],storm["damage"],storm["towerDamage"],storm["rounds"],storm["castDelay"],storm["dotInterval"],storm["stunDuration"]) == (3,3,65,33,3,.45,.45,.25)
    assert (box["cost"],box["hp"],box["lifetime"],box["spawnId"],box["spawnInterval"],box["hardExpiry"],box["hpDecayPerSecond"]) == (5,500,30,"twin_blades",3,True,0)
    dart = next(card for card in current["cards"] if card["id"] == "dart_gun")
    assert (dart["cost"], dart["damage"], dart["attackInterval"], dart["range"]) == (3, 50, .4, 8.5)
    near(dart["damage"] / dart["attackInterval"], 125)
    assert all(current["rules"][key] == (5 if key == "combatRevision" else value) for key,value in release_baseline["rules"].items())
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
        lambda m: m["rules"].pop("guardDepth"),
        lambda m: m["cards"][-1].update(spawnId="missing_child"),
        lambda m: m["cards"][-1].update(spawnId="blade_box"),
        lambda m: m["cards"][-1].update(spawnId="meteor_shards"),
        lambda m: m["cards"][-1].update(spawnInterval=0),
        lambda m: m["cards"][-1].update(spawnInterval=31),
        lambda m: m["cards"][-1].update(hardExpiry=False),
        lambda m: m["cards"][-2].update(rounds=2.5),
        lambda m: m["cards"][-2].update(dotInterval=0)]
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
