"""Generate native definitions from the canonical balance manifest.

The JSON is the editable numerical source. Generated C++ is checked in so a
normal Unreal/portable build needs no Python installation. Presentation metadata
and release approval are excluded from the balance hash.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "Assets/Data/card_manifest.json"
DEFAULT_OUTPUT = ROOT / "Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCardManifest.generated.inc"
DEFAULT_ARENA_OUTPUT = ROOT / "Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftArenaRules.generated.inc"
ARENA_DEFAULTS = dict(arenaWidth=30, arenaHeight=44, riverHalfWidth=1.65,
                      bridgeCenterX=7.2, bridgeWidth=4.2, coreDepth=17.3, guardDepth=13.4)
NUMBER_FIELDS = (
    "cost count hp damage attackInterval moveSpeed range scale projectileSpeed "
    "shockwaveSpeed splash lifetime footprint towerDamage spellRadius castDelay "
    "chargeDamage slowPct slowDuration auraDamage auraInterval auraRadius "
    "stunDuration dotDamage dotDuration dotInterval rounds collisionRadius targetMask"
).split()
BOOL_FIELDS = "flying canHitAir structuresOnly spell building".split()
STRING_FIELDS = "id name".split()
CARD_FIELDS = set(NUMBER_FIELDS + BOOL_FIELDS + STRING_FIELDS)
EXPANSION_NUMBER_FIELDS = ("chargeMoveSeconds chargedMoveSpeed hpDecayPerSecond initialAttackCooldown "
                           "shieldHP deathSpawnCount invisibilityDuration dotStructureDamage").split()
EXPANSION_BOOL_FIELDS = "hardExpiry shieldOverflowProtected globalFriendlyInvisibility breakExistingLocks".split()
EXPANSION_STRING_FIELDS = ["deathSpawnId"]


def fields_for(schema):
    if schema == 1:
        return NUMBER_FIELDS, BOOL_FIELDS, STRING_FIELDS
    if schema == 2:
        return (NUMBER_FIELDS + EXPANSION_NUMBER_FIELDS, BOOL_FIELDS + EXPANSION_BOOL_FIELDS,
                STRING_FIELDS + EXPANSION_STRING_FIELDS)
    raise ValueError("Expected balance schema 1 or 2")


def balance_hash(manifest):
    # Model scale is presentation after collision decoupling; illustrations,
    # names and approval metadata cannot invalidate historical match results.
    payload = {section: [{key: value for key, value in card.items()
                          if key not in ("name", "scale")}
                         for card in manifest.get(section, [])]
               for section in ("cards", "entities")}
    payload["rules"] = manifest.get("rules", {})
    def normalize(value):
        if isinstance(value, dict):
            return {key: normalize(item) for key, item in value.items()}
        if isinstance(value, list):
            return [normalize(item) for item in value]
        if isinstance(value, float) and value.is_integer():
            return int(value)
        return value
    payload = normalize(payload)
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def validate(manifest):
    numbers, booleans, strings = fields_for(manifest.get("schema"))
    expected_fields = set(numbers + booleans + strings)
    if not manifest.get("cards"):
        raise ValueError("Expected a nonempty playable roster")
    if manifest.get("balanceHash") != balance_hash(manifest):
        raise ValueError("Canonical balanceHash is stale")
    rules = manifest.get("rules", {})
    if set(rules) & set(ARENA_DEFAULTS):
        if not set(ARENA_DEFAULTS) <= set(rules):
            raise ValueError("Arena geometry must specify all dimensions and tower/bridge positions")
        for key in ARENA_DEFAULTS:
            value = rules[key]
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                raise ValueError(f"rules.{key}: expected a positive finite number")
        if any(rules[key] != int(rules[key]) or int(rules[key]) % 2 for key in ("arenaWidth", "arenaHeight")):
            raise ValueError("Arena dimensions must be positive even tile counts")
        if rules["bridgeCenterX"] + rules["bridgeWidth"] / 2 >= rules["arenaWidth"] / 2:
            raise ValueError("Bridges must fit inside the arena")
        if not rules["riverHalfWidth"] < rules["guardDepth"] < rules["coreDepth"] < rules["arenaHeight"] / 2 - 2:
            raise ValueError("River, Guard and Core positions must fit in order inside the arena")
    seen = set()
    for section in ("cards", "entities"):
        for card in manifest.get(section, []):
            if set(card) != expected_fields:
                raise ValueError(f"{card.get('id')}: missing/unknown fields: "
                                 f"{sorted(expected_fields - set(card))}/{sorted(set(card) - expected_fields)}")
            if not card["id"] or card["id"] in seen:
                raise ValueError("Missing or duplicate entity definition ID")
            seen.add(card["id"])
            for field in numbers:
                value = card[field]
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                    raise ValueError(f"{card['id']}.{field}: expected a finite nonnegative number")
            for field in booleans:
                if not isinstance(card[field], bool):
                    raise ValueError(f"{card['id']}.{field}: expected a boolean")
            for field in strings:
                if not isinstance(card[field], str):
                    raise ValueError(f"{card['id']}.{field}: expected a string")
            if card["count"] < 1 or card["count"] != int(card["count"]):
                raise ValueError("Combat entity count must be a positive integer")
            if card["targetMask"] != int(card["targetMask"]) or not 0 < card["targetMask"] <= 7:
                raise ValueError("Expected an explicit ground/air/structure target mask")
            if not card["spell"] and (card["attackInterval"] <= 0 or card["collisionRadius"] <= 0):
                raise ValueError("Combat entities need an attack interval and independent collision radius")
            if card["spell"] and card["collisionRadius"] != 0:
                raise ValueError("Spells cannot have a collision body")
            if section == "entities" and card["cost"] != 0:
                raise ValueError("Subordinate definitions cannot charge Aether")
            if card["cost"] != int(card["cost"]) or (section == "cards" and card["cost"] <= 0):
                raise ValueError("Playable Aether cost must be a positive integer")
            if manifest["schema"] == 2:
                if card["deathSpawnCount"] != int(card["deathSpawnCount"]):
                    raise ValueError("Death spawn count must be an integer")
                if bool(card["deathSpawnCount"]) != bool(card["deathSpawnId"]):
                    raise ValueError("Death spawn ID and count must be specified together")
                if card["shieldOverflowProtected"] and card["shieldHP"] <= 0:
                    raise ValueError("Shield overflow protection needs positive shield HP")
                if card["hardExpiry"] and (not card["building"] or card["lifetime"] <= 0):
                    raise ValueError("Hard expiry requires a building lifetime")
                if card["dotDamage"] > 0 and (card["dotInterval"] <= 0 or card["dotDuration"] <= 0):
                    raise ValueError("Damage zones need positive duration and tick interval")
                if card["dotStructureDamage"] > 0 and (not card["targetMask"] & 4 or card["dotDamage"] <= 0):
                    raise ValueError("Structure DOT requires structure eligibility and a damage zone")
                if card["invisibilityDuration"] > 0 and not card["spell"]:
                    raise ValueError("Invisibility application belongs to a spell")
    if manifest["schema"] == 2:
        children = {card["id"]: card for card in manifest.get("entities", [])}
        for card in manifest["cards"] + manifest.get("entities", []):
            if card["deathSpawnId"]:
                child = children.get(card["deathSpawnId"])
                if not child or child["spell"] or child["building"] or child["deathSpawnCount"]:
                    raise ValueError("Death summons must reference a nonrecursive subordinate troop")
    return manifest


def generate(manifest):
    numbers, booleans, strings = fields_for(manifest["schema"])
    lines = ["// Generated by Build/generate_card_manifest.py; edit Assets/Data/card_manifest.json.",
             "// Balance SHA256: " + manifest["balanceHash"], "namespace {"]
    for section, function in (("cards", "BuildManifestCards"), ("entities", "BuildManifestEntities")):
        lines.extend([f"std::vector<Card> {function}() {{", "    std::vector<Card> values;"])
        for card in manifest.get(section, []):
            lines.extend(["    {", "        Card card;"])
            for field in strings:
                lines.append(f"        card.{field} = {json.dumps(card[field], ensure_ascii=True)};")
            for field in numbers:
                lines.append(f"        card.{field} = {repr(card[field])};")
            for field in booleans:
                lines.append(f"        card.{field} = {'true' if card[field] else 'false'};")
            lines.extend(["        values.push_back(card);", "    }"])
        lines.extend(["    return values;", "}"])
    lines.extend(["} // namespace", f'const std::string& CardManifestHash() {{ static const std::string value = "{manifest["balanceHash"]}"; return value; }}',
                  f'const std::string& CardManifestVersion() {{ static const std::string value = "{manifest["version"]}"; return value; }}', ""])
    return "\n".join(lines)


def generate_arena(manifest):
    rules = {**ARENA_DEFAULTS, **manifest.get("rules", {})}
    lines = ["// Generated from Assets/Data/card_manifest.json; do not edit.",
             f"inline constexpr int HalfWidth = {int(rules['arenaWidth']) // 2}, HalfHeight = {int(rules['arenaHeight']) // 2};",
             "inline constexpr int Width = HalfWidth * 2, Height = HalfHeight * 2;",
             "inline constexpr double LastTileX = HalfWidth - .5, LastTileZ = HalfHeight - .5;",
             f"inline constexpr double RiverHalfWidth = {rules['riverHalfWidth']}, BridgeCenterX = {rules['bridgeCenterX']}, BridgeWidth = {rules['bridgeWidth']};",
             f"inline constexpr double CoreDepth = {rules['coreDepth']}, GuardDepth = {rules['guardDepth']}, GuardX = BridgeCenterX;",
             "inline constexpr double DeploymentMaxX = HalfWidth - 1.8;",
             "inline constexpr double PocketOuterX = HalfWidth - .8, PocketMaxDepth = GuardDepth - 3.15;",
             f"inline constexpr int NavigationRevision = {int(rules.get('navigationRevision', 6))}, CombatRevision = {int(rules.get('combatRevision', 1))};", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--arena-output", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--require-approved", action="store_true")
    args = parser.parse_args()
    manifest = validate(json.loads(args.manifest.read_text(encoding="utf-8-sig")))
    if args.require_approved and manifest.get("status") != "approved":
        raise ValueError("Balance/source conflicts are pending; release generation is blocked")
    source = generate(manifest)
    arena_source = generate_arena(manifest)
    arena_output = args.arena_output or (DEFAULT_ARENA_OUTPUT if args.output.resolve() == DEFAULT_OUTPUT.resolve()
                                          else args.output.parent / "RiftArenaRules.generated.inc")
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != source:
            raise ValueError("Native definitions are not synchronized with the canonical manifest")
        if not arena_output.is_file() or arena_output.read_text(encoding="utf-8") != arena_source:
            raise ValueError("Native arena rules are not synchronized with the canonical manifest")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(source, encoding="utf-8", newline="\n")
        arena_output.parent.mkdir(parents=True, exist_ok=True)
        arena_output.write_text(arena_source, encoding="utf-8", newline="\n")
    print(json.dumps({"passed": True, "operation": "check" if args.check else "generate",
                      "status": manifest["status"], "version": manifest["version"],
                      "playableCards": len(manifest["cards"]), "subordinateDefinitions": len(manifest.get("entities", [])),
                      "balanceHash": manifest["balanceHash"]}))


if __name__ == "__main__":
    main()
