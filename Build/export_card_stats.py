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
cards = data if isinstance(data, list) else data.get("cardSnapshot", data.get("cardDefinitions"))
if not cards or len({c["id"] for c in cards}) != len(cards):
    raise ValueError("Expected a complete native card definition array")

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

stats, abilities = [], []
for c in cards:
    spell, building = c["spell"], c["building"]
    targets = ("Troops only" if c["towerDamage"] == 0 else "Troops and structures") if spell else (
        "Ground/air troops only" if building else "Structures only" if c["structuresOnly"] else
        "Ground/air troops and structures" if c["canHitAir"] else "Ground troops and structures")
    dps = c["damage"] / c["attackInterval"] if not spell else None
    stats.append([c["name"], "Spell" if spell else "Building" if building else "Troop", c["cost"],
                  0 if spell else c["count"], "N/A" if spell else numeric(c["hp"]), numeric(c["damage"]),
                  "N/A" if spell else numeric(c["attackInterval"]), "N/A" if spell else f"{dps:.2f}",
                  "N/A" if spell else f"{dps*c['count']:.2f}", "N/A" if spell else numeric(c["hp"]*c["count"]),
                  numeric(c["moveSpeed"]), "N/A" if spell else numeric(c["range"]), numeric(c["projectileSpeed"]),
                  "Spell" if spell else "Stationary" if building else "Flying" if c["flying"] else "Ground", targets,
                  numeric(c["splash"]), numeric(c["lifetime"]) if building else "N/A",
                  numeric(c["footprint"]) if building else "N/A", numeric(c["scale"])])
    def ability(title, values, details):
        abilities.append([c["name"], title, values, details])
    if c["count"] > 1 and not spell:
        ability("Split formation", f"{c['count']} members", "Lane comes from each member's resolved landing X; ground members use that side's bridge; a destroyed lane Guard opens the route to the Core")
    if c["chargeDamage"]:
        ability("Charged impact", f"{numeric(c['chargeDamage'])} damage; >1.65 s movement", "Replaces basic hit damage on a charged impact")
    if c["slowPct"]:
        ability("Movement slow", f"{numeric(c['slowPct']*100)}%; {numeric(c['slowDuration'])} s", "Each melee hit refreshes the slow; no stacking")
    if c["auraDamage"]:
        ability("Electric aura", f"{numeric(c['auraDamage'])} damage; {numeric(c['auraInterval'])} s interval; {numeric(c['auraRadius'])} tile radius; {numeric(c['stunDuration'])} s stun", "Hits ground and air troops; basic attack remains structures-only")
    if c["dotDamage"]:
        ticks = int(c["dotDuration"] / c["dotInterval"])
        lingering = c["dotDamage"] * ticks
        ability("Lingering damage zone", f"{numeric(c['dotDamage'])} damage/tick; {numeric(c['dotInterval'])} s interval; {numeric(c['dotDuration'])} s duration", f"{ticks} ticks after initial impact; troop-only; {numeric(c['damage'])} initial + {numeric(lingering)} lingering = {numeric(c['damage']+lingering)} maximum before mitigation/HP limits")
    if spell:
        ability("Area spell", f"{numeric(c['damage'])} troop damage; {numeric(c['towerDamage'])} structure damage; {numeric(c['spellRadius'])} tile radius; {numeric(c['castDelay'])} s cast delay", "Fixed target area; current enemy positions at impact" if c["castDelay"] else "Instant area impact")
    if c["rounds"]:
        ability("Visual burst", f"{c['rounds']} rounds", "Listed damage is applied once per eligible target, not once per visual round")
    if building:
        ability("Defensive building", f"{numeric(c['lifetime'])} s lifetime; {numeric(c['footprint'])} tile footprint", "Ground and air troops only; expires after lifetime")

args.output_directory.mkdir(parents=True, exist_ok=True)
outputs = [emit("CARD_STATS", ["Card", "Type", "Aether cost", "Units", "HP per unit", "Damage per hit", "Attack interval (s)", "Basic DPS per unit", "Full swarm basic DPS", "Full swarm HP", "Movement speed (tiles/s)", "Attack range (tiles)", "Projectile speed (tiles/s)", "Movement", "Basic attack targets", "Splash radius (tiles)", "Lifetime (s)", "Building footprint (tiles)", "Base model scale"], stats),
           emit("ABILITY_STATS", ["Card", "Ability", "Values", "Behavior"], abilities)]
proof = {"version": args.version, "source": str(args.input).replace("\\", "/"), "sourceSHA256": hashlib.sha256(raw).hexdigest(), "cardCount": len(cards), "formula": "Basic DPS per member = damage / attack interval; full swarm basic DPS = per-member DPS * count; no travel, projectile or ability damage included", "outputs": outputs}
print(json.dumps(proof, indent=2))
