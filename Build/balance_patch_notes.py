"""Build exact, auditable changes from captured live and approved manifests."""
import argparse
import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from generate_card_manifest import validate

ROOT = Path(__file__).resolve().parents[1]
# Presentation baselines captured from the immutable version's
# Public/Presentation/RiftArenaGeometry.h. They are not gameplay hash inputs.
CAPTURED_TILE_WORLD_SIZE_CM = {
    "7c25b46e8c6d7dd1326dee60960141da3774bcd1": 100,
}
# Lower costs/cooldowns are beneficial. Model scale is neutral presentation.
STATS = {
    "cost": ("Aether cost", "Aether", -1),
    "count": ("Units", "", 1), "hp": ("Body HP per unit", "HP", 1),
    "shieldHP": ("Shield per unit", "HP", 1),
    "damage": ("Damage per hit per unit", "damage", 1),
    "attackInterval": ("Attack interval", "s", -1),
    "attackFrequency": ("Attack frequency", "attacks/s", 1),
    "basicDPS": ("Basic DPS per unit", "DPS", 1),
    "groupHP": ("Full group body HP", "HP", 1),
    "groupDPS": ("Full group basic DPS", "DPS", 1),
    "moveSpeed": ("Movement speed", "tiles/s", 1),
    "effectiveChargedMoveSpeed": ("Charged movement speed", "tiles/s", 1),
    "range": ("Attack range", "tiles", 1),
    "projectileSpeed": ("Projectile speed", "tiles/s", 1),
    "splash": ("Splash radius", "tiles", 1), "lifetime": ("Lifetime", "s", 1),
    "spellRadius": ("Spell radius", "tiles", 1),
    "castDelay": ("Impact delay", "s", -1),
    "towerDamage": ("Structure impact damage", "damage", 1),
    "chargeDamage": ("Total charged hit", "damage", 1),
    "chargeMoveSeconds": ("Movement time before charge", "s", -1),
    "slowPct": ("Movement slow strength", "%", 1),
    "slowDuration": ("Slow duration", "s", 1),
    "auraDamage": ("Aura damage", "damage", 1),
    "auraInterval": ("Aura interval", "s", -1),
    "auraRadius": ("Aura radius", "tiles", 1),
    "stunDuration": ("Stun duration", "s", 1),
    "spawnInterval": ("Time per summoned troop", "s", -1),
    "dotDamage": ("Troop damage per DOT tick", "damage", 1),
    "dotStructureDamage": ("Structure damage per DOT tick", "damage", 1),
    "dotDuration": ("DOT duration", "s", 1),
    "dotInterval": ("DOT tick interval", "s", -1),
    "hpDecayPerSecond": ("Passive HP decay", "HP/s", -1),
    "initialAttackCooldown": ("First attack cooldown", "s", -1),
    "deathSpawnCount": ("Death-spawned children", "units", 1),
    "invisibilityDuration": ("Invisibility duration", "s", 1),
    "healthCloakThreshold": ("One-time cloak health threshold", "fraction of maximum HP", 0),
    "collisionRadius": ("Collision body radius", "tiles", 0),
    "initialDurability": ("Initial body and shield durability per unit", "HP", 1),
    "groupDurability": ("Full group body and shield durability", "HP", 1),
    "fullTroopExposure": ("Maximum full troop exposure", "damage", 1),
    "fullStructureExposure": ("Maximum full structure exposure", "damage", 1),
    "footprint": ("Building footprint", "tiles", 0),
    "collisionRadius": ("Gameplay collision radius", "tiles", 0),
    "rounds": ("Visual round count", "rounds", 0),
    "shockwaveSpeed": ("Wavefront speed", "tiles/s", 1),
    "scale": ("Visual model scale", "x", 0),
}


def derived(card):
    values = dict(card)
    values.setdefault("healthCloakThreshold", 0)
    interval = card["attackInterval"]
    if not card["spell"] and interval > 0:
        values.update(basicDPS=card["damage"] / interval,
                      attackFrequency=1 / interval,
                      groupHP=card["hp"] * card["count"],
                      groupDPS=card["damage"] / interval * card["count"],
                      initialDurability=card["hp"] + card.get("shieldHP", 0),
                      groupDurability=(card["hp"] + card.get("shieldHP", 0)) * card["count"])
    if card.get("chargeDamage", 0) > 0:
        values["effectiveChargedMoveSpeed"] = card.get("chargedMoveSpeed", 0) or card["moveSpeed"]
    if card["spell"]:
        ticks = card["dotDuration"] / card["dotInterval"] if card.get("dotDuration", 0) > 0 else 0
        values["fullTroopExposure"] = card["damage"] + card.get("dotDamage", 0) * ticks
        values["fullStructureExposure"] = card["towerDamage"] + card.get("dotStructureDamage", 0) * ticks
        if card["id"] == "thunderstorm":
            values["fullTroopExposure"] = card["damage"] * card["rounds"]
            values["fullStructureExposure"] = card["towerDamage"] * card["rounds"]
    return values


def mechanic_changes(previous, card):
    def changed(*fields):
        return previous is None or any(previous.get(key) != card.get(key) for key in fields)
    lines = []
    if card.get("chargeDamage", 0) > 0 and changed("chargeMoveSeconds", "chargedMoveSpeed", "chargeDamage"):
        lines.append("Charge requires more than " + format(card["chargeMoveSeconds"], ".9g")
                     + " seconds of accumulated movement. Its total charged hit replaces the basic hit; it is not an additive bonus."
                     + " Charged movement uses " + format(card.get("chargedMoveSpeed", 0) or card["moveSpeed"], ".9g") + " tiles/s.")
    if card.get("hardExpiry") and changed("hardExpiry", "lifetime"):
        lines.append("Expires at " + format(card["lifetime"], ".9g") + " seconds even if body HP remains. Passive HP decay is separate.")
    if card.get("shieldHP", 0) > 0 and changed("shieldHP", "shieldOverflowProtected"):
        lines.append("Body HP and shield HP are separate. " + ("A shield-breaking damage event discards its overflow; the next event can damage the body." if card.get("shieldOverflowProtected") else "Shield-breaking damage can overflow into body HP.") + " No shield regeneration.")
    if card.get("deathSpawnCount", 0) > 0 and changed("deathSpawnId", "deathSpawnCount"):
        lines.append("On death, spawns exactly " + str(card["deathSpawnCount"]) + " subordinate units nearby. Children cost no additional Aether and occupy no hand or deck slot; their contribution is credited to the parent.")
    if card.get("spawnId") and changed("spawnId", "spawnInterval", "lifetime"):
        lines.append("Produces one " + card["spawnId"].replace("_", " ").title() + " member every " + format(card["spawnInterval"], ".9g") + " seconds, starting after one interval. Uses that troop's current stats; no additional Aether or deck slot. Stun pauses production, destruction stops it, and hard lifetime continues.")
    if card.get("invisibilityDuration", 0) > 0 and changed("invisibilityDuration", "globalFriendlyInvisibility", "breakExistingLocks", "healthCloakThreshold"):
        threshold = card.get("healthCloakThreshold", 0)
        scope = ("Once per deployment, this troop first surviving a hit at or below " + format(threshold * 100, ".9g") + "% maximum body HP") if threshold > 0 else "All friendly troops" if card.get("globalFriendlyInvisibility") else "Friendly troops inside the spell area at impact"
        locks = "Existing enemy target locks drop." if card.get("breakExistingLocks") else "Existing enemy target locks remain."
        lines.append(scope + (" becomes" if threshold > 0 else " become") + " invisible for " + format(card["invisibilityDuration"], ".9g") + " simulation seconds. " + locks + " New enemy acquisition is prevented. Committed hits, in-flight projectiles and area spells still resolve; invisibility grants no damage immunity.")
    spell_changed = card["spell"] and changed("damage", "towerDamage", "dotDamage", "dotStructureDamage",
                                             "dotDuration", "dotInterval", "spellRadius", "castDelay", "rounds", "targetMask")
    if spell_changed and card.get("dotDuration", 0) > 0:
        ticks = card["dotDuration"] / card["dotInterval"]
        lines.append("After impact, the affected area lasts " + format(card["dotDuration"], ".9g")
                     + " seconds and deals " + format(ticks, ".9g") + " DOT ticks, one every "
                     + format(card["dotInterval"], ".9g")
                     + (" second. Each tick deals " if card["dotInterval"] == 1 else " seconds. Each tick deals ")
                     + format(card["dotDamage"], ".9g") + " troop damage and "
                     + format(card.get("dotStructureDamage", 0), ".9g") + " structure damage.")
    if spell_changed and card["id"] == "thunderstorm":
        times = [card["castDelay"] + i*card["dotInterval"] for i in range(card["rounds"])]
        lines.append("Lightning strikes at " + ", ".join(format(t, ".9g") for t in times) + " seconds after cast. Each wave hits current enemies inside the fixed radius for " + format(card["damage"], ".9g") + " troop damage or " + format(card["towerDamage"], ".9g") + " structure damage and refreshes a " + format(card["stunDuration"], ".9g") + "-second stun.")
    elif spell_changed and card.get("rounds", 0) > 1:
        lines.append(format(card["rounds"], ".9g") + " visual rounds animate the burst. Each affected target receives one damage packet at impact: "
                     + format(card["damage"], ".9g") + " troop damage or "
                     + format(card["towerDamage"], ".9g") + " structure damage. Visual rounds do not multiply that damage.")
    if previous is not None and previous.get("targetMask") != card.get("targetMask"):
        mask = card["targetMask"]
        labels = [label for bit,label in ((1,"ground troops"),(2,"air troops"),(4,"structures")) if mask & bit]
        lines.append("Attack targets: " + ", ".join(labels) + ".")
    return lines


def validate_resolution(after, resolution):
    definitions = {card["id"]:card for card in after["cards"] + after.get("entities", [])}
    for card_id, choice in resolution.get("resolvedChoices", {}).items():
        if card_id == "targetMasks":
            for target_id, mask in choice.items():
                if target_id not in definitions or definitions[target_id]["targetMask"] != mask:
                    raise ValueError("Approved targeting decision differs from canonical data: " + target_id)
        elif card_id in definitions and isinstance(choice, dict):
            for field, value in choice.items():
                if field in definitions[card_id] and definitions[card_id][field] != value:
                    raise ValueError("Approved source decision differs from canonical data: " + card_id + "." + field)
    if resolution.get("subordinateIds") is not None and set(resolution["subordinateIds"]) != {c["id"] for c in after.get("entities", [])}:
        raise ValueError("Approved subordinate roster differs from canonical data")


def numeric_change(key, old, new):
    label, unit, direction = STATS[key]
    delta = new - old
    return {"key": key, "label": label, "unit": unit, "old": old, "new": new,
            "delta": delta, "percent": 100 * delta / old if old else None,
            "zeroTransition": "ADDED" if old == 0 and new != 0 else "REMOVED" if new == 0 and old != 0 else "",
            "percentagePoints": 100 * delta if key == "slowPct" else None,
            "benefit": (1 if delta * direction > 0 else -1 if delta * direction < 0 else 0)}


def build_notes(before, after, resolution):
    for manifest in (before, after):
        validate(manifest)
    if (after.get("status") != "approved" or resolution.get("status") != "approved"
            or resolution.get("unresolved") is not False):
        raise ValueError("Confirmed scope and source decisions are required for final patch notes")
    validate_resolution(after, resolution)
    old_cards = {card["id"]: card for card in before["cards"]}
    current_ids = {card["id"] for card in after["cards"]}
    if not set(old_cards) <= current_ids:
        raise ValueError("An existing playable card was removed")
    if resolution.get("newPlayableIds") is not None and set(resolution["newPlayableIds"]) != current_ids - set(old_cards):
        raise ValueError("Approved new-card roster differs from canonical data")
    panels = []
    for card in after["cards"]:
        panel = {"id": card["id"], "name": card["name"], "cost": card["cost"],
                 "changes": [], "mechanics": [], "children": []}
        previous = old_cards.get(card["id"])
        if previous is None:
            panel.update(label="NEW CARD", definition=derived(card))
        else:
            old, new = derived(previous), derived(card)
            for key in STATS:
                if key in old and key in new and not math.isclose(old[key], new[key], rel_tol=1e-12, abs_tol=1e-12):
                    # Group values are secondary, relevant only to multi-unit cards.
                    if key.startswith("group") and max(previous["count"], card["count"]) <= 1:
                        continue
                    if key in ("initialDurability", "groupDurability") and max(previous.get("shieldHP", 0), card.get("shieldHP", 0)) <= 0:
                        continue
                    if key.startswith("full") and max(previous.get("dotDamage", 0), card.get("dotDamage", 0), previous.get("dotStructureDamage", 0), card.get("dotStructureDamage", 0)) <= 0:
                        continue
                    change = numeric_change(key, old[key], new[key])
                    if key == "damage" and card["spell"]:
                        change["label"] = "Initial troop impact damage"
                    panel["changes"].append(change)
            signs = {change["benefit"] for change in panel["changes"]} - {0}
            panel["label"] = "BUFF" if signs == {1} else "NERF" if signs == {-1} else "MIXED" if signs else "VISUAL"
        panel["mechanics"] = mechanic_changes(previous, card)
        if previous is None or panel["changes"] or panel["mechanics"]:
            panel["mechanics"] += resolution.get("cardMechanicNotes", {}).get(card["id"], [])
        if previous is not None and not panel["changes"] and panel["mechanics"]:
            panel["label"] = "MIXED"
        child_id = card.get("deathSpawnId")
        if child_id:
            child = next((entity for entity in after.get("entities", []) if entity["id"] == child_id), None)
            if child is None:
                raise ValueError("Missing subordinate definition")
            panel["children"].append({"definition": derived(child), "count": card["deathSpawnCount"]})
        if previous is None or panel["changes"] or panel["mechanics"]:
            panels.append(panel)
    mechanics = list(resolution.get("mechanicNotes", []))
    arena = resolution.get("resolvedChoices", {}).get("arena", {})
    if arena:
        rules = after.get("rules", {})
        for field in ("arenaWidth", "arenaHeight"):
            if rules.get(field) != arena.get(field):
                raise ValueError("Approved arena decision differs from canonical data: " + field)
        mechanics.append("Arena: " + format(rules["arenaHeight"], ".9g") + " tiles long and "
                         + format(rules["arenaWidth"], ".9g") + " tiles across. Guard Towers remain centered on their bridge lanes.")
        if "worldUnitsPerTile" in arena:
            new_size = arena["worldUnitsPerTile"]
            old_size = before.get("presentation", {}).get("worldUnitsPerTile")
            if old_size is None:
                old_size = CAPTURED_TILE_WORLD_SIZE_CM.get(before.get("sourceCommit"))
            if (isinstance(old_size, bool) or not isinstance(old_size, (int, float))
                    or not math.isfinite(old_size) or old_size <= 0
                    or isinstance(new_size, bool) or not isinstance(new_size, (int, float))
                    or not math.isfinite(new_size) or new_size <= 0):
                raise ValueError("Tile world-size notes require a captured positive presentation baseline and approved size")
            percent = 100 * (new_size - old_size) / old_size
            mechanics.append("Each tile is " + format(new_size, ".9g") + " cm per side ("
                             + format(old_size, ".9g") + " -> " + format(new_size, ".9g") + " cm, "
                             + format(percent, "+.9g") + "% side length).")
    if before.get("rules", {}).get("targetHardLock") != after.get("rules", {}).get("targetHardLock"):
        mechanics.append("Troops commit to a Crown Tower on their first authoritative attack. Shadow Surge can drop enemy locks; already committed damage and launched projectiles still resolve.")
    return {"schema": 1, "version": after["version"], "status": "approved",
            "baselineVersion": before["version"], "baselineHash": before["balanceHash"],
            "balanceHash": after["balanceHash"], "panels": panels,
            "mechanics": mechanics,
            "derivedMetrics": "Basic DPS = damage / attack interval. Group DPS assumes continuous attacks by every member. Travel, charge, aura, splash target count, shield and death summons are separate."}


def serialize_notes(notes):
    content = json.dumps(notes, ensure_ascii=True, separators=(",", ":"), allow_nan=False)
    return (json.dumps(notes, indent=2, allow_nan=False) + "\n",
            '// Generated from captured live and approved canonical definitions.\n'
            'static const TCHAR* RiftPatchNotesJSON = TEXT(R"RIFTPATCH('
            + content + ')RIFTPATCH");\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, default=ROOT / "Assets/Data/Baselines/1.5.0-expanded-schema.json")
    parser.add_argument("--after", type=Path, default=ROOT / "Assets/Data/card_manifest.json")
    parser.add_argument("--resolution", type=Path, default=ROOT / "Assets/Data/source_resolution.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpp-output", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="Read-only exact comparison against freshly derived approved notes.")
    args = parser.parse_args()
    notes = build_notes(*(json.loads(path.read_text(encoding="utf-8-sig"))
                          for path in (args.before, args.after, args.resolution)))
    json_text, cpp_text = serialize_notes(notes)
    if args.check:
        for path, expected in ((args.output, json_text), (args.cpp_output, cpp_text)):
            if path is not None and (not path.is_file() or path.read_text(encoding="utf-8-sig") != expected):
                raise ValueError("Approved patch notes are missing or stale: " + str(path))
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json_text, encoding="utf-8")
        if args.cpp_output:
            args.cpp_output.parent.mkdir(parents=True, exist_ok=True)
            args.cpp_output.write_text(cpp_text, encoding="utf-8")
    print(json.dumps({"passed": True, "operation": "check" if args.check else "generate",
                      "version": notes["version"], "balanceHash": notes["balanceHash"],
                      "panels": len(notes["panels"])}))


if __name__ == "__main__":
    main()
