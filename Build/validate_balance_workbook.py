"""Read-only validation of the saved balance XLSX against canonical manifests.

Reads OpenXML directly. Does not author workbooks or claim Excel application
execution. Formula results are checked independently against source values;
artifact-tool's actual input-change/recalculate test is recorded by the builder.
"""
import argparse
import ast
import hashlib
import importlib.util
import json
import math
import re
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
REL_ID = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"
ERRORS = re.compile(r"#(?:REF!|DIV/0!|VALUE!|NAME\?|N/A|NUM!|NULL!|SPILL!|CALC!)")
SHEETS = ["Cards", "Abilities", "Spells", "Summons", "Changes", "Source"]
FIELDS = {
    "cost": "Aether cost", "count": "Unit count", "hp": "Body HP per unit", "shieldHP": "Shield HP per unit",
    "damage": "Basic damage per unit", "attackInterval": "Attack interval", "moveSpeed": "Movement speed",
    "range": "Attack range", "projectileSpeed": "Projectile speed", "shockwaveSpeed": "Shockwave speed",
    "splash": "Splash radius", "lifetime": "Lifetime", "footprint": "Building footprint",
    "collisionRadius": "Collision radius", "scale": "Model scale", "towerDamage": "Structure impact damage",
    "spellRadius": "Spell radius", "castDelay": "Impact delay", "chargeDamage": "Total charge damage",
    "chargeMoveSeconds": "Accumulated movement threshold", "chargedMoveSpeed": "Charged movement speed",
    "slowPct": "Movement slow strength", "slowDuration": "Movement slow duration", "auraDamage": "Aura damage",
    "auraInterval": "Aura interval", "auraRadius": "Aura radius", "stunDuration": "Stun duration",
    "dotDamage": "Troop DOT damage per tick", "dotStructureDamage": "Structure DOT damage per tick",
    "dotDuration": "DOT duration", "dotInterval": "DOT interval", "rounds": "Visual round count",
    "hpDecayPerSecond": "Building HP decay", "initialAttackCooldown": "Initial attack cooldown",
    "deathSpawnCount": "Death-spawn count", "invisibilityDuration": "Invisibility duration",
    "healthCloakThreshold": "Health-cloak HP threshold",
}
ARENA_STATS = {"arenaWidth": "Arena width", "arenaHeight": "Arena length", "bridgeCenterX": "Bridge center X", "guardDepth": "Guard tower depth", "coreDepth": "Core tower depth"}
MECHANIC_NAMES = {"targetMask": "Attack targets", "hardExpiry": "Expiry at maximum lifetime", "shieldOverflowProtected": "Shield overflow protection", "deathSpawnId": "Child definition", "globalFriendlyInvisibility": "Global friendly invisibility", "breakExistingLocks": "Break existing target locks"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def close(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-9)
    return a == b


def records(data):
    return {"cards": data, "entities": [], "rules": {}} if isinstance(data, list) else data


def targets(mask):
    return ", ".join(label for bit, label in [(1, "Ground troops"), (2, "air troops"), (4, "structures")] if mask & bit) or "None"


def mechanic_value(field, value):
    if field == "targetMask":
        return targets(value)
    if isinstance(value, bool):
        return "Yes" if value else "No"
    return value or "None"


class Xlsx:
    def __init__(self, filename):
        self.sheets = {}
        self.formulas = {}
        self.cache = {}
        self.errors = []
        self.panes = {}
        self.table_counts = {}
        with zipfile.ZipFile(filename) as z:
            strings = []
            if "xl/sharedStrings.xml" in z.namelist():
                strings = ["".join(node.itertext()) for node in ET.fromstring(z.read("xl/sharedStrings.xml"))]
            rels = {r.get("Id"): r.get("Target").lstrip("/") for r in ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))}
            self.names = [s.get("name") for s in ET.fromstring(z.read("xl/workbook.xml")).find("s:sheets", NS)]
            for s in ET.fromstring(z.read("xl/workbook.xml")).find("s:sheets", NS):
                name = s.get("name")
                target = rels[s.get(REL_ID)]
                target = target if target.startswith("xl/") else "xl/" + target
                tree = ET.fromstring(z.read(target))
                self.panes[name] = tree.find("s:sheetViews/s:sheetView/s:pane", NS)
                self.table_counts[name] = len(tree.findall("s:tableParts/s:tablePart", NS))
                values = {}
                for c in tree.findall("s:sheetData/s:row/s:c", NS):
                    address, kind = c.get("r"), c.get("t")
                    v, f = c.find("s:v", NS), c.find("s:f", NS)
                    raw = v.text if v is not None else None
                    if kind == "s":
                        value = strings[int(raw)] if raw is not None else None
                    elif kind == "inlineStr":
                        element = c.find("s:is", NS)
                        value = "".join(element.itertext()) if element is not None else ""
                    elif kind == "b":
                        value = raw == "1"
                    elif kind in ("str", "e"):
                        value = raw
                    else:
                        value = float(raw) if raw not in (None, "") else None
                    values[address] = value
                    if f is not None:
                        self.formulas[name, address] = f.text or ""
                    if kind == "e" or isinstance(value, str) and ERRORS.search(value):
                        self.errors.append({"sheet": name, "cell": address, "value": value})
                self.sheets[name] = values

    def raw(self, sheet, cell):
        return self.sheets[sheet].get(cell)

    def value(self, sheet, cell, active=None):
        key = (sheet, cell)
        if key in self.cache:
            return self.cache[key]
        if key not in self.formulas:
            return self.raw(sheet, cell)
        active = set() if active is None else active
        if key in active:
            raise ValueError(f"Circular formula: {sheet}!{cell}")
        active = active | {key}
        formula = self.formulas[key]
        # Only the simple scalar formulas authored by this builder are accepted.
        formula = re.sub(r"'((?:[^']|'')+)'!([A-Z]+[0-9]+)", lambda m: f"CELL({json.dumps(m[1].replace(chr(39)*2, chr(39)))},{json.dumps(m[2])})", formula)
        formula = re.sub(r"(?<![A-Za-z0-9_\"'])\b([A-Z]+[0-9]+)\b(?![\"'])", lambda m: f"CELL({json.dumps(sheet)},{json.dumps(m[1])})", formula)
        formula = re.sub(r"(?<![<>!=])=(?!=)", "==", formula)
        expr = ast.parse(formula, mode="eval").body

        def ev(node):
            if isinstance(node, ast.Constant):
                return node.value
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                if node.func.id == "CELL" and len(node.args) == 2:
                    return self.value(ev(node.args[0]), ev(node.args[1]), active)
                if node.func.id == "IF" and len(node.args) == 3:
                    return ev(node.args[1] if ev(node.args[0]) else node.args[2])
            if isinstance(node, ast.BinOp):
                a, b = ev(node.left), ev(node.right)
                if isinstance(node.op, ast.Add):
                    return a + b
                if isinstance(node.op, ast.Sub):
                    return a - b
                if isinstance(node.op, ast.Mult):
                    return a * b
                if isinstance(node.op, ast.Div):
                    return a / b
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
                return -ev(node.operand)
            if isinstance(node, ast.Compare) and len(node.ops) == 1 and isinstance(node.ops[0], ast.Eq):
                return ev(node.left) == ev(node.comparators[0])
            raise ValueError(f"Unsupported formula node in {sheet}!{cell}: {ast.dump(node)}")

        self.cache[key] = ev(expr)
        return self.cache[key]


def validate(args):
    failures, checks = [], 0

    def check(label, passed, **detail):
        nonlocal checks
        checks += 1
        if not passed:
            failures.append({"check": label, **detail})

    manifest_bytes, baseline_bytes, workbook_bytes = args.manifest.read_bytes(), args.baseline.read_bytes(), args.workbook.read_bytes()
    m, b = json.loads(manifest_bytes.decode("utf-8-sig")), records(json.loads(baseline_bytes.decode("utf-8-sig")))
    check("Canonical approval", m.get("status") == "approved" or args.fixture and m.get("status") == "fixture")
    check("Canonical schema", m.get("schema") in (1, 2, 3))
    check("Preserve every existing playable card", {c["id"] for c in b["cards"]} <= {c["id"] for c in m["cards"]})
    # Import the actual project hash convention; do not duplicate serialization.
    spec = importlib.util.spec_from_file_location("rift_balance_manifest", Path(__file__).with_name("generate_card_manifest.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    check("Canonical balance hash", m.get("balanceHash") == module.balance_hash(m))
    if m.get("schema", 0) >= 3:
        for c in m["cards"] + m.get("entities", []):
            threshold = c.get("healthCloakThreshold")
            check("Typed bounded health-cloak fraction", type(threshold) in (int, float) and math.isfinite(threshold) and 0 <= threshold <= 1, card=c["id"])
            if threshold:
                check("Health cloak is a surviving troop effect", not c["spell"] and not c["building"] and c["invisibilityDuration"] > 0, card=c["id"])
    wb = Xlsx(args.workbook)
    check("Sheet order and names", wb.names == SHEETS, actual=wb.names)
    check("No saved formula errors", not wb.errors, errors=wb.errors)
    for sheet in SHEETS:
        check(f"{sheet} table", wb.table_counts.get(sheet, 0) >= 1)
        pane = wb.panes.get(sheet)
        check(f"{sheet} frozen headers and IDs", pane is not None and float(pane.get("xSplit", 0)) >= 2 and float(pane.get("ySplit", 0)) >= 5)
    for addr, value in {"E2": m["version"], "E3": m["balanceHash"], "E4": sha(manifest_bytes), "E5": sha(baseline_bytes)}.items():
        check(f"Provenance {addr}", wb.raw("Source", addr) == value)
    source_index = {}
    for address in wb.sheets.get("Source", {}):
        if not re.fullmatch(r"A[0-9]+", address) or int(address[1:]) < 10:
            continue
        r = address[1:]
        key = (wb.raw("Source", f"A{r}"), wb.raw("Source", f"B{r}"), wb.raw("Source", f"D{r}"))
        check("Unique source field", key not in source_index, key=key)
        source_index[key] = (f"E{r}", wb.raw("Source", f"E{r}"))
    expected_source = {}
    for phase, manifest in [("Pre", b), ("Post", m)]:
        for c in manifest["cards"] + manifest.get("entities", []):
            if phase == "Pre" and m.get("schema", 0) >= 3 and "healthCloakThreshold" not in c:
                expected_source[phase, c["id"], "healthCloakThreshold"] = 0
            for field, value in c.items():
                if isinstance(value, (str, int, float, bool)) or value is None:
                    expected_source[phase, c["id"], field] = "n.a." if value is None else value
        for field, value in manifest.get("rules", {}).items():
            if isinstance(value, (str, int, float, bool)) or value is None:
                expected_source[phase, "@rules", field] = "n.a." if value is None else value
    check("Complete original pre/post input coverage", set(source_index) == set(expected_source))
    if m.get("schema", 0) >= 3 and any("healthCloakThreshold" not in c for c in b["cards"] + b.get("entities", [])):
        check("Only documented historical threshold default", wb.raw("Source", "E8") == "Absent healthCloakThreshold = 0 (no HP trigger); other inputs are unchanged.")
    for key, expected in expected_source.items():
        # OpenXML represents an empty text input as an empty cell. It does not
        # convert an actual numeric zero or a missing required value to zero.
        check("Source input parity", key in source_index and (close(source_index[key][1], expected) or expected == "" and source_index[key][1] is None), key=key)

    def cv(sheet, cell, expected):
        try:
            actual = wb.value(sheet, cell)
            check(f"{sheet}!{cell} value", close(actual, expected), expected=expected, actual=actual)
        except Exception as error:
            check(f"{sheet}!{cell} formula", False, error=str(error))

    def require_formula(sheet, cell):
        check(f"{sheet}!{cell} live formula", (sheet, cell) in wb.formulas)

    check("Playable roster count", [wb.raw("Cards", f"A{r}") for r in range(6, len(m["cards"]) + 6)] == [c["id"] for c in m["cards"]])
    for i, c in enumerate(m["cards"], 6):
        cv("Cards", f"B{i}", c["name"])
        cv("Cards", f"D{i}", c["cost"])
        cv("Cards", f"T{i}", targets(c["targetMask"]))
        for field, column in {"count": "E", "hp": "F", "shieldHP": "G", "damage": "I", "attackInterval": "J", "moveSpeed": "O", "range": "P", "projectileSpeed": "Q", "splash": "R", "scale": "S", "collisionRadius": "V", "lifetime": "W", "footprint": "X", "hpDecayPerSecond": "Y", "initialAttackCooldown": "Z"}.items():
            expected = c.get(field, "n.a.")
            if c["spell"] and field in ["count", "hp", "shieldHP", "attackInterval", "moveSpeed", "range", "collisionRadius", "lifetime", "footprint", "hpDecayPerSecond", "initialAttackCooldown"]:
                expected = "n.a."
            cv("Cards", f"{column}{i}", expected)
        for field, column in [("frontSight", "AA"), ("rearSight", "AB")]:
            cv("Cards", f"{column}{i}", "n.a." if c["spell"] else m.get("rules", {}).get(field, "n.a."))
        if not c["spell"]:
            for column, expected in {"H": c["hp"] + c.get("shieldHP", 0), "K": c["damage"] / c["attackInterval"], "L": c["hp"] * c["count"], "M": (c["hp"] + c.get("shieldHP", 0)) * c["count"], "N": c["damage"] / c["attackInterval"] * c["count"]}.items():
                require_formula("Cards", f"{column}{i}")
                cv("Cards", f"{column}{i}", expected)
        else:
            for column in ["H", "K", "L", "M", "N"]:
                cv("Cards", f"{column}{i}", "n.a.")
    spells = [c for c in m["cards"] if c["spell"]]
    for i, c in enumerate(spells, 6):
        cv("Spells", f"A{i}", c["id"])
        for field, column in {"cost": "C", "castDelay": "D", "spellRadius": "E", "damage": "F", "towerDamage": "G", "dotDamage": "H", "dotStructureDamage": "I", "dotInterval": "J", "dotDuration": "K", "rounds": "O"}.items():
            cv("Spells", f"{column}{i}", c.get(field, "n.a."))
        ticks = c["dotDuration"] / c["dotInterval"] if c["dotDuration"] else 0
        for column, value in {"L": ticks, "M": c["damage"] + c.get("dotDamage", 0) * ticks, "N": c["towerDamage"] + c.get("dotStructureDamage", 0) * ticks}.items():
            require_formula("Spells", f"{column}{i}")
            cv("Spells", f"{column}{i}", value)
    expected_abilities = {}
    for c in m["cards"]:
        def ability(metric, value):
            expected_abilities[c["id"], metric] = value
        if c.get("chargeDamage", 0):
            for metric, field in [("Total charged-hit damage", "chargeDamage"), ("Accumulated movement threshold", "chargeMoveSeconds"), ("Charged movement speed", "chargedMoveSpeed")]:
                ability(metric, c.get(field, "n.a."))
        if c.get("slowPct", 0):
            ability("Slow strength", c["slowPct"])
            ability("Slow duration", c["slowDuration"])
        if c.get("auraDamage", 0):
            for metric, field in [("Damage per pulse", "auraDamage"), ("Pulse interval", "auraInterval"), ("Pulse radius", "auraRadius"), ("Stun duration", "stunDuration")]:
                ability(metric, c[field])
            ability("Theoretical per-target aura DPS", c["auraDamage"] / c["auraInterval"])
        if c.get("shockwaveSpeed", 0):
            ability("Wavefront speed", c["shockwaveSpeed"])
            ability("Maximum wave radius", c["range"])
            ability("Travel to outer edge", c["range"] / c["shockwaveSpeed"])
        if c.get("shieldHP", 0):
            ability("Initial shield HP", c["shieldHP"])
            ability("Overflow protection", "Yes" if c["shieldOverflowProtected"] else "No")
        if c.get("deathSpawnCount", 0):
            ability("Child count", c["deathSpawnCount"])
            ability("Child definition", c["deathSpawnId"])
        if c["building"]:
            for metric, field in [("HP decay per second", "hpDecayPerSecond"), ("Maximum lifetime", "lifetime"), ("Initial attack cooldown", "initialAttackCooldown")]:
                ability(metric, c.get(field, "n.a."))
            if "hardExpiry" in c:
                ability("Expiry at maximum lifetime", "Yes" if c["hardExpiry"] else "No")
        if c.get("invisibilityDuration", 0):
            ability("Duration", c["invisibilityDuration"])
            if c.get("healthCloakThreshold", 0):
                ability("Maximum HP threshold", c["healthCloakThreshold"])
                ability("Trigger body HP", c["hp"] * c["healthCloakThreshold"])
                ability("Scope", "Self")
            else:
                ability("Global friendly scope", "Yes" if c["globalFriendlyInvisibility"] else "No")
            ability("Break existing target locks", "Yes" if c["breakExistingLocks"] else "No")
    seen_abilities = set()
    for address in wb.sheets["Abilities"]:
        if not re.fullmatch(r"A[0-9]+", address) or int(address[1:]) < 6:
            continue
        row = address[1:]
        key = (wb.raw("Abilities", f"A{row}"), wb.raw("Abilities", f"D{row}"))
        if key[0] not in {c["id"] for c in m["cards"]}:
            continue
        check("Ability metric has approved source", key in expected_abilities, key=key)
        check("Unique ability metric", key not in seen_abilities, key=key)
        seen_abilities.add(key)
        if key in expected_abilities:
            cv("Abilities", f"E{row}", expected_abilities[key])
            if key[1] == "Trigger body HP":
                require_formula("Abilities", f"E{row}")
                check("Self-cloak ability identity", wb.raw("Abilities", f"C{row}") == "Health-triggered cloak")
            if key[1] == "Maximum HP threshold":
                check("Surviving-packet once-only self-cloak behavior", wb.raw("Abilities", f"G{row}") == "Once per deployment, after surviving a damage packet at or below this fraction of maximum body HP.")
    check("Complete ability coverage", seen_abilities == set(expected_abilities))
    parents = [c for c in m["cards"] if c.get("deathSpawnCount", 0)]
    check("Subordinates are not playable cards", not ({c["id"] for c in m["cards"]} & {c["id"] for c in m["entities"]}))
    for i, p in enumerate(parents, 6):
        e = next(c for c in m["entities"] if c["id"] == p["deathSpawnId"])
        cv("Summons", f"A{i}", p["id"])
        cv("Summons", f"C{i}", e["id"])
        cv("Summons", f"E{i}", p["deathSpawnCount"])
        for field, column in {"hp": "F", "damage": "G", "attackInterval": "H", "moveSpeed": "L", "range": "M", "splash": "N", "scale": "O", "collisionRadius": "Q"}.items():
            cv("Summons", f"{column}{i}", e.get(field, "n.a."))
        for column, expected in {"I": e["damage"] / e["attackInterval"], "J": p["deathSpawnCount"] * e["hp"], "K": p["deathSpawnCount"] * e["damage"] / e["attackInterval"]}.items():
            require_formula("Summons", f"{column}{i}")
            cv("Summons", f"{column}{i}", expected)
    # Every numeric changed-stat row must show exact before/after and percentages.
    numeric_changes = {}
    mechanic_changes = {}
    by_id = {c["id"]: c for c in m["cards"]}
    for c in b["cards"]:
        if c["id"] not in by_id:
            continue
        new = by_id[c["id"]]
        if m.get("schema", 0) >= 3 and "healthCloakThreshold" not in c:
            c = {**c, "healthCloakThreshold": 0}
        for field, title in FIELDS.items():
            if field == "chargedMoveSpeed":
                continue
            if isinstance(c.get(field), (int, float)) and not isinstance(c.get(field), bool) and isinstance(new.get(field), (int, float)) and not close(c[field], new[field]):
                numeric_changes[c["id"], title] = (c[field], new[field], field in ("slowPct", "healthCloakThreshold"))
        if not c["spell"] and not new["spell"]:
            if c.get("chargeDamage", 0) > 0 and new.get("chargeDamage", 0) > 0 and "chargedMoveSpeed" in c and "chargedMoveSpeed" in new:
                old_speed, new_speed = c["chargedMoveSpeed"] or c["moveSpeed"], new["chargedMoveSpeed"] or new["moveSpeed"]
                if not close(old_speed, new_speed):
                    numeric_changes[c["id"], "Charged movement speed"] = (old_speed, new_speed, False)
            old_dps, new_dps = c["damage"] / c["attackInterval"], new["damage"] / new["attackInterval"]
            if not close(old_dps, new_dps):
                numeric_changes[c["id"], "Basic DPS each"] = (old_dps, new_dps, False)
            if not close(c["attackInterval"], new["attackInterval"]):
                numeric_changes[c["id"], "Attack frequency"] = (1 / c["attackInterval"], 1 / new["attackInterval"], False)
            if not close(c["hp"] * c["count"], new["hp"] * new["count"]):
                numeric_changes[c["id"], "Group body HP"] = (c["hp"] * c["count"], new["hp"] * new["count"], False)
            if not close(old_dps * c["count"], new_dps * new["count"]):
                numeric_changes[c["id"], "Group basic DPS"] = (old_dps * c["count"], new_dps * new["count"], False)
        for field in MECHANIC_NAMES:
            if field in c and field in new and c[field] != new[field]:
                mechanic_changes[c["id"], MECHANIC_NAMES[field]] = (mechanic_value(field, c[field]), mechanic_value(field, new[field]))
    if m.get("schema", 0) >= 3:
        for field, title in ARENA_STATS.items():
            before, after = b.get("rules", {}).get(field), m.get("rules", {}).get(field)
            if isinstance(before, (int, float)) and isinstance(after, (int, float)) and not close(before, after):
                numeric_changes["@rules", title] = (before, after, False)
    seen_changes = set()
    seen_mechanics = set()
    for addr in wb.sheets["Changes"]:
        if not re.fullmatch(r"A[0-9]+", addr) or int(addr[1:]) < 6:
            continue
        r = addr[1:]
        key = (wb.raw("Changes", f"A{r}"), wb.raw("Changes", f"C{r}"))
        if wb.raw("Changes", f"J{r}") in ("CHANGED", "ADDED", "REMOVED"):
            check("Changed rows contain only actual changes", key in numeric_changes or key in mechanic_changes, key=key)
        if key in mechanic_changes:
            check("Unique changed-mechanic row", key not in seen_mechanics, key=key)
            seen_mechanics.add(key)
            before, after = mechanic_changes[key]
            for column, expected in {"D":before, "E":after, "F":"n.a.", "G":"n.a.", "H":"n.a.", "I":"mechanic", "J":"CHANGED"}.items():
                cv("Changes", f"{column}{r}", expected)
        if key not in numeric_changes:
            continue
        check("Unique changed-stat row", key not in seen_changes, key=key)
        seen_changes.add(key)
        before, after, pp = numeric_changes[key]
        for column, expected in {"D": before, "E": after, "F": after - before, "G": "n.a." if before == 0 else (after - before) / before, "H": (after - before) * 100 if pp else "n.a."}.items():
            cv("Changes", f"{column}{r}", expected)
        cv("Changes", f"J{r}", "ADDED" if before == 0 and after else "REMOVED" if after == 0 and before else "CHANGED")
    check("All and only actual numeric stat changes", seen_changes == set(numeric_changes))
    check("All and only actual mechanic changes", seen_mechanics == set(mechanic_changes))
    for (sheet, address), formula in wb.formulas.items():
        try:
            actual = wb.value(sheet, address)
            cached = wb.raw(sheet, address)
            check("Saved formula cached value matches independent evaluation", cached is not None and close(actual, cached), sheet=sheet, cell=address, cached=cached, evaluated=actual)
            check("No source formulas depend on outputs", sheet != "Source", sheet=sheet, cell=address)
        except Exception as error:
            check("Saved scalar formula evaluation", False, sheet=sheet, cell=address, error=str(error))
    if args.builder_proof:
        proof = json.loads(args.builder_proof.read_text(encoding="utf-8-sig"))
        check("Builder output byte pin", proof.get("outputSHA256") == sha(workbook_bytes) and proof.get("outputBytes") == len(workbook_bytes))
        check("Builder source and version pins", proof.get("balanceHash") == m["balanceHash"] and proof.get("manifestSHA256") == sha(manifest_bytes) and proof.get("baselineSHA256") == sha(baseline_bytes))
        check("Actual artifact-tool input-change recalculation", proof.get("inputChangeRecalculationPassed") is True and proof.get("restoredInput") is True)
        if any(c.get("healthCloakThreshold", 0) for c in m["cards"]):
            check("Actual health-cloak input-change recalculation", proof.get("healthCloakInputChangeRecalculationPassed") is True)
    return {"schemaVersion": 1, "passed": not failures, "fixture": args.fixture, "checks": checks, "failures": failures, "version": m["version"], "balanceHash": m["balanceHash"], "manifestFileSHA256": sha(manifest_bytes), "baselineFileSHA256": sha(baseline_bytes), "workbookSHA256": sha(workbook_bytes), "workbookBytes": len(workbook_bytes), "cards": len(m["cards"]), "subordinates": len(m["entities"]), "formulaCells": len(wb.formulas), "engineLimit": "OpenXML and independent scalar formula validation; native Excel UI execution was not performed."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--workbook", type=Path, required=True)
    parser.add_argument("--builder-proof", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--fixture", action="store_true")
    args = parser.parse_args()
    proof = validate(args)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in proof.items() if k != "failures"}, indent=2))
    if proof["failures"]:
        print(json.dumps(proof["failures"][:20], indent=2))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
