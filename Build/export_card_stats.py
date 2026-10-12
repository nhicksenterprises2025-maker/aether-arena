"""Export Sheets-ready card and ability stats from actual native captured definitions.

Accepts an asset-import report, immutable Meta dataset, or CardDefinitionsJSON array.
No balance numbers are duplicated here. Basic DPS uses damage / attackInterval.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("input", type=Path)
parser.add_argument("--version", required=True)
parser.add_argument("--output-directory", type=Path, default=Path("Docs"))
args = parser.parse_args()
raw = args.input.read_bytes()
data = json.loads(raw.decode("utf-8-sig"))
cards = data if isinstance(data, list) else data.get("cards", data.get("cardSnapshot", data.get("cardDefinitions")))
if isinstance(data, dict) and data.get("status") == "pending":
    raise ValueError("Source decisions are pending; final numerical exports are blocked")
if not cards or len({c["id"] for c in cards}) != len(cards):
    raise ValueError("Expected a complete native card definition array")
entities = [] if isinstance(data, list) else data.get("entities", data.get("entitySnapshot", []))
rules = {} if isinstance(data, list) else data.get("rules", data.get("rulesSnapshot", {}))
if isinstance(data, dict) and data.get("schema") in (1, 2, 3) and data.get("balanceHash") and "cards" in data:
    from generate_card_manifest import validate
    validate(data)
    if data.get("version") != args.version:
        raise ValueError("Export version must match the approved canonical manifest")
if {c["id"] for c in cards} & {c["id"] for c in entities}:
    raise ValueError("Subordinate entities cannot be duplicated as playable cards")

def emit(name, columns, rows):
    target = args.output_directory / (name + "_" + args.version + ".tsv")
    stream = io.StringIO(newline="")
    writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
    writer.writerow(columns)
    writer.writerows(rows)
    target.write_text(stream.getvalue(), encoding="utf-8")
    return {"file": str(target).replace("\\", "/"), "sha256": hashlib.sha256(target.read_bytes()).hexdigest(), "rows": len(rows)}

def numeric(value):
    return format(float(value), ".9g")

def target_description(c):
    if "targetMask" in c:
        mask = int(c["targetMask"])
        return ", ".join(label for bit, label in ((1,"Ground troops"),(2,"Air troops"),(4,"Structures")) if mask & bit)
    # Historical snapshots predate explicit masks; these retain their captured
    # legacy targeting projections and are clearly identified by provenance.
    if c["spell"]:
        return "Ground/air troops only" if c["towerDamage"] == 0 else "Ground/air troops and structures"
    if c["structuresOnly"]:
        return "Structures only"
    return ("Ground/air troops" if c["canHitAir"] else "Ground troops") + (" only" if c["building"] else " and structures")

stats, abilities = [], []
for c in cards:
    spell, building = c["spell"], c["building"]
    targets = target_description(c)
    dps = c["damage"] / c["attackInterval"] if not spell else None
    stats.append([c["name"], "Spell" if spell else "Building" if building else "Troop", c["cost"],
                  0 if spell else c["count"], "N/A" if spell else numeric(c["hp"]), numeric(c["damage"]),
                  "N/A" if spell else numeric(c["attackInterval"]), "N/A" if spell else f"{dps:.2f}",
                  "N/A" if spell else f"{dps*c['count']:.2f}", "N/A" if spell else numeric(c["hp"]*c["count"]),
                  numeric(c["moveSpeed"]), "N/A" if spell else numeric(c["range"]), numeric(c["projectileSpeed"]),
                  "Spell" if spell else "Stationary" if building else "Flying" if c["flying"] else "Ground", targets,
                  numeric(c["splash"]), numeric(c["lifetime"]) if building else "N/A",
                  numeric(c["footprint"]) if building else "N/A", numeric(c["scale"]),
                  "N/A" if spell else numeric(c.get("shieldHP",0)),
                  "N/A" if spell else numeric(c["hp"]+c.get("shieldHP",0)),
                  numeric(c.get("collisionRadius",0)) if not spell else "N/A",
                  numeric(c.get("hpDecayPerSecond", 0)) if building else "N/A",
                  "Yes" if c.get("hardExpiry") else "No" if building and "hardExpiry" in c else "N/A",
                  numeric(c["initialAttackCooldown"]) if not spell and "initialAttackCooldown" in c else "N/A",
                  numeric(rules["frontSight"]) if not spell and "frontSight" in rules else "N/A",
                  numeric(rules["rearSight"]) if not spell and "rearSight" in rules else "N/A", c["id"]])
    def ability(title, values, details):
        abilities.append([c["name"], title, values, details])
    if c["count"] > 1 and not spell:
        ability("Split formation", f"{c['count']} members", "Lane comes from each member's resolved landing X; ground members use that side's bridge; a destroyed lane Guard opens the route to the Core")
    if c["chargeDamage"]:
        threshold = f">{numeric(c['chargeMoveSeconds'])} s accumulated movement" if "chargeMoveSeconds" in c else "threshold not captured in this historical definition"
        speed = f"; movement {numeric(c['moveSpeed'])} normal / {numeric(c['chargedMoveSpeed'] or c['moveSpeed'])} charged tiles/s" if "chargedMoveSpeed" in c else ""
        ability("Charged impact", f"{numeric(c['chargeDamage'])} TOTAL damage; {threshold}{speed}", "Replaces basic hit damage; never an additive bonus")
    if c.get("shockwaveSpeed",0):
        ability("Expanding shockwave", f"{numeric(c['damage'])} damage/target; {numeric(c['attackInterval'])} s interval; {numeric(c['range'])} tile radius; {numeric(c['shockwaveSpeed'])} tiles/s; {numeric(c['range']/c['shockwaveSpeed'])} s to outer edge", targets+"; one hit per target per wave as its front arrives")
    if c["slowPct"]:
        ability("Movement slow", f"{numeric(c['slowPct']*100)}%; {numeric(c['slowDuration'])} s", "Each shockwave hit refreshes the slow; no stacking; stronger slows retain their own expiry" if c.get("shockwaveSpeed",0) else "Each melee hit refreshes the slow; no stacking")
    if c["auraDamage"]:
        ability("Electric aura", f"{numeric(c['auraDamage'])} damage; {numeric(c['auraInterval'])} s interval; {numeric(c['auraRadius'])} tile radius; {numeric(c['stunDuration'])} s stun", "Hits ground and air troops; basic attack remains structures-only")
    if c["dotDamage"]:
        ticks = int(c["dotDuration"] / c["dotInterval"])
        lingering = c["dotDamage"] * ticks
        structure_tick = c.get("dotStructureDamage",0)
        ability("Lingering damage zone", f"Troops {numeric(c['dotDamage'])} / structures {numeric(structure_tick)} damage per tick; {numeric(c['dotInterval'])} s interval; {numeric(c['dotDuration'])} s duration", f"{ticks} ticks after initial impact while inside the zone; troop maximum {numeric(c['damage']+lingering)}; structure maximum {numeric(c['towerDamage']+structure_tick*ticks)} where eligible; separate damage packets, before mitigation/HP limits")
    if spell:
        ability("Area spell", f"{numeric(c['damage'])} troop damage; {numeric(c['towerDamage'])} structure damage; {numeric(c['spellRadius'])} tile radius; {numeric(c['castDelay'])} s cast delay", "Fixed target area; current enemy positions at impact" if c["castDelay"] else "Instant area impact")
    if c["rounds"]:
        ability("Visual burst", f"{c['rounds']} rounds", "Listed damage is applied once per eligible target, not once per visual round")
    if building:
        decay = f"; {numeric(c['hpDecayPerSecond'])} HP/s decay" if "hpDecayPerSecond" in c else ""
        first = f"; {numeric(c['initialAttackCooldown'])} s first cooldown" if "initialAttackCooldown" in c else ""
        expiry = "; expires at maximum lifetime even if HP remains" if c.get("hardExpiry") else "; no separate hard-expiry rule" if "hardExpiry" in c else ""
        ability("Defensive building", f"{numeric(c['lifetime'])} s maximum lifetime; {numeric(c['footprint'])} tile footprint{decay}{first}{expiry}", targets)
    if c.get("shieldHP"):
        ability("Separate shield", f"{numeric(c['hp'])} body HP + {numeric(c['shieldHP'])} shield HP", "Each damage packet consumes shield first; a protected shield-breaking packet discards its overflow. The next packet can hit body HP; no regeneration" if c.get("shieldOverflowProtected") else "Shield behavior follows the captured rule")
    if c.get("deathSpawnId"):
        ability("Death summons", f"{c['deathSpawnCount']} × {c['deathSpawnId']}", "Exactly one death transition; children cost zero Aether and occupy no additional hand/deck slots; contribution credited to parent")
    if c.get("invisibilityDuration"):
        threshold=c.get("healthCloakThreshold",0)
        scope = ("Once per deployment, this troop survives damage leaving it at or below " + numeric(threshold*100) + "% maximum HP") if threshold else "All friendly troops" if c.get("globalFriendlyInvisibility") else "Friendly troops inside the spell area at impact"
        locks = "; existing enemy locks drop" if c.get("breakExistingLocks") else "; existing locks are preserved"
        ability("Invisibility", f"{numeric(c['invisibilityDuration'])} simulation s", scope+locks+"; new enemy acquisition is prevented; committed hits, in-flight projectiles and area spells still resolve; no damage immunity")

args.output_directory.mkdir(parents=True, exist_ok=True)
outputs = [emit("CARD_STATS", ["Card", "Type", "Aether cost", "Units", "Body HP per unit", "Damage per hit", "Attack interval (s)", "Basic DPS per unit", "Full swarm basic DPS", "Full swarm body HP", "Movement speed (tiles/s)", "Attack range (tiles)", "Projectile speed (tiles/s)", "Movement", "Basic attack targets", "Splash radius (tiles)", "Lifetime (s)", "Building footprint (tiles)", "Base model scale", "Shield HP per unit", "Initial durability per unit", "Gameplay collision radius (tiles)", "Passive HP decay (HP/s)", "Hard expiry at maximum lifetime", "Initial attack cooldown (s)", "Front sight (tiles)", "Rear sight (tiles)", "Card ID"], stats),
           emit("ABILITY_STATS", ["Card", "Ability", "Values", "Behavior"], abilities)]
if entities:
    children = {c["id"]:c for c in entities}
    summon_rows = []
    for parent in cards:
        if not parent.get("deathSpawnCount", 0):
            continue
        child = children.get(parent.get("deathSpawnId"))
        if child is None:
            raise ValueError("Missing subordinate definition for " + parent["id"])
        count = parent["deathSpawnCount"]
        summon_rows.append([parent["name"],child["name"],count,numeric(child["hp"]),numeric(child["damage"]),numeric(child["attackInterval"]),f"{child['damage']/child['attackInterval']:.2f}",numeric(child["hp"]*count),f"{child['damage']/child['attackInterval']*count:.2f}",numeric(child["moveSpeed"]),numeric(child["range"]),target_description(child),numeric(child["scale"]),numeric(child.get("collisionRadius",0)),0,0,parent["id"],child["id"]])
    outputs.append(emit("SUMMON_STATS", ["Parent card", "Subordinate unit", "Children per death", "Body HP each", "Damage each", "Attack interval (s)", "Basic DPS each", "Group body HP", "Group basic DPS", "Movement (tiles/s)", "Range (tiles)", "Targets", "Model scale", "Collision radius (tiles)", "Additional Aether cost", "Additional deck slots", "Parent ID", "Entity ID"], summon_rows))
proof = {"version": args.version, "source": str(args.input).replace("\\", "/"), "sourceSHA256": hashlib.sha256(raw).hexdigest(), "balanceHash":None if isinstance(data,list) else data.get("balanceHash"),"cardCount": len(cards),"subordinateDefinitions":len(entities), "formula": "Basic DPS per member = damage / attack interval; full swarm basic DPS = per-member DPS * count; no travel, projectile or ability damage included", "outputs": outputs}
print(json.dumps(proof, indent=2))
