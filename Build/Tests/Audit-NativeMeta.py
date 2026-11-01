"""Read-only audit of a saved native Meta dataset; never modifies player/game files."""
import argparse
import collections
import datetime
import hashlib
import json
import math
import pathlib
import sys


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("dataset", type=pathlib.Path)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--log", type=pathlib.Path)
parser.add_argument("--export", type=pathlib.Path)
parser.add_argument("--compare", type=pathlib.Path)
parser.add_argument("--manifest", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2] / "Assets/Data/card_manifest.json", help="Matching versioned canonical manifest for datasets with a balanceHash.")
parser.add_argument("--target", type=int, default=10000)
parser.add_argument("--initial-seed", type=int, default=151515,
                    help="Initial native dataset seed (default: production QA seed151515).")
parser.add_argument("--require-complete", action="store_true",
                    help="Fail unless the target count, completion log and final export are all observed.")
args = parser.parse_args()
if args.target <= 0 or not 0 <= args.initial_seed <= 0xffffffff:
    parser.error("Target must be positive and initial seed must be an unsigned32-bit value.")
data = json.loads(args.dataset.read_text(encoding="utf-8-sig"))
errors = []
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        errors.append(message)


def near(actual, expected, message, absolute=1e-5):
    check(math.isclose(actual, expected, rel_tol=1e-9, abs_tol=absolute),
          f"{message}: {actual!r} vs {expected!r}")


def number(obj, key):
    return obj.get(key, 0)


def finite(value, path):
    if isinstance(value, dict):
        for key, child in value.items():
            finite(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            finite(child, f"{path}[{index}]")
    elif isinstance(value, (float, int)):
        check(math.isfinite(value), f"Nonfinite number: {path}")


def wilson(score, count):
    if count <= 0:
        return 0, 100
    probability = score / count
    z2 = 1.96 ** 2
    denominator = 1 + z2 / count
    center = (probability + z2 / (2 * count)) / denominator
    half = 1.96 * math.sqrt((probability * (1 - probability) + z2 / (4 * count)) / count) / denominator
    return 100 * max(0, center - half), 100 * min(1, center + half)


finite(data, "dataset")
definitions = {card["id"]: card for card in data["cardSnapshot"]}
entity_definitions = {card["id"]: card for card in data.get("entitySnapshot", [])}
styles = ["beatdown", "aggro", "control", "cycle", "split", "spell_cycle", "counter"]
revision = data["telemetryRevision"]
legacy_exposure = []
check(data["schemaVersion"] == 1 and data["model"] == "rift-native-1" and
      data["deckPolicy"] == "native-observed-2" and revision in (2, 3, 4, 5),
      "Unexpected dataset identity")
navigation_revision = data["rulesSnapshot"].get("navigationRevision", 1)
combat_revision = data["rulesSnapshot"].get("combatRevision", 0)
alignment_rule = "guard centers at bridge centers; cores at arena center"
alignment_present = "guardBridgeAlignment" in data["rulesSnapshot"]
aligned_guards = alignment_present and data["rulesSnapshot"]["guardBridgeAlignment"] == alignment_rule
fingerprint_policy_revision = data.get("fingerprintPolicyRevision", 1) if "balanceHash" in data else 0
if "balanceHash" in data:
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
    from generate_card_manifest import balance_hash
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    check(manifest["balanceHash"] == balance_hash(manifest), "Canonical manifest hash is stale")
    check(data["balanceHash"] == manifest["balanceHash"], "Dataset requires its exact matching versioned balance manifest")
    expected_roster = {card["id"] for card in manifest["cards"]}
    check(set(definitions) == expected_roster and len(data["cardSnapshot"]) == len(expected_roster), "Captured playable roster differs from canonical definitions")
    check(data["aiStyles"] == styles, "AI style identity mismatch")
    for expected in manifest["cards"]:
        captured = definitions.get(expected["id"], {})
        for key, value in expected.items():
            check(key in captured, f"Missing immutable canonical field: {expected['id']}.{key}")
            if isinstance(value, bool):
                check(captured.get(key) == value, f"Captured flag mismatch: {expected['id']}.{key}")
            elif isinstance(value, (int, float)):
                near(captured.get(key, -1), value, f"Captured numerical definition: {expected['id']}.{key}")
            else:
                check(captured.get(key) == value, f"Captured identity mismatch: {expected['id']}.{key}")
    if revision >= 4:
        check(manifest.get("schema") in (2, 3, 4), "Expansion telemetry requires its matching schema2/3/4 mechanics manifest")
        expected_entities = {card["id"]: card for card in manifest.get("entities", [])}
        check(set(entity_definitions) == set(expected_entities) and len(data.get("entitySnapshot", [])) == len(expected_entities),
              "Captured subordinate definitions differ from matching canonical manifest")
        check(not set(entity_definitions) & set(definitions), "Subordinates cannot occupy the playable roster")
        for identity, expected in expected_entities.items():
            captured = entity_definitions.get(identity, {})
            for key, value in expected.items():
                check(key in captured and captured.get(key) == value, f"Captured subordinate field mismatch: {identity}.{key}")
        for identity, definition in definitions.items():
            child = entity_definitions.get(definition.get("deathSpawnId"))
            if definition.get("deathSpawnCount", 0):
                check(child is not None and child.get("cost") == 0 and not child.get("spell") and not child.get("building"),
                      f"Invalid death subordinate contract: {identity}")
    expected_rules = {"arenaWidth":30, "arenaHeight":44, "riverHalfWidth":1.65, "bridgeCenterX":7.2,
        "bridgeWidth":4.2, "coreDepth":17.3, "guardDepth":13.4, "guardX":7.2, "pocketOuterX":14.2,
        "pocketMaxDepth":10.25, "regulationSeconds":180, "overtimeSeconds":120, "aetherInterval":2.8,
        "doubleAetherAt":120, "tripleAetherAt":240, "tiebreakerDrainPerSecond":180, "openingAether":5,
        "maximumAether":10, "collisionSkin":.02, "structureClearance":.22,
        "coreActivation":"friendly Guard Tower destroyed", "spellImpact":"fixed target area; current enemy positions at impact; exact per-card delays captured in cardSnapshot"}
    matching_rules = manifest["rules"]
    check(not isinstance(fingerprint_policy_revision, bool) and fingerprint_policy_revision in (1, 2),
          "Unsupported canonical fingerprint policy revision")
    if fingerprint_policy_revision == 2:
        check(revision in (3, 4, 5), "Policy2 requires captured telemetry3,4 or5")
        # Read the matching versioned manifest, never today's arena globals.
        # Policy1 archives had implicit30x44 geometry; policy2 captures it explicitly.
        for key in ("arenaWidth", "arenaHeight", "riverHalfWidth", "bridgeCenterX", "bridgeWidth", "coreDepth", "guardDepth"):
            check(key in matching_rules, f"Dynamic arena policy requires canonical geometry field: {key}")
        valid_rules = dict(matching_rules)
        for key in ("arenaWidth", "arenaHeight", "riverHalfWidth", "bridgeCenterX", "bridgeWidth", "coreDepth", "guardDepth", "guardX", "guardCenterX", "pocketOuterX", "pocketMaxDepth"):
            if key not in valid_rules:
                continue
            value = valid_rules[key]
            valid = isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0
            if valid and key in ("arenaWidth", "arenaHeight"):
                valid = value == int(value) and value % 2 == 0
            check(valid, f"Invalid canonical arena geometry: {key}")
            if not valid:
                del valid_rules[key]
        expected_rules.update({key: value for key, value in valid_rules.items() if key != "guardCenterX"})
        expected_rules["guardX"] = valid_rules.get("guardCenterX", valid_rules.get("guardX", expected_rules["bridgeCenterX"]))
        if "guardCenterX" in valid_rules:
            alignment_rule = "guard centers one tile inward from bridge centers; cores at arena center"
            near(expected_rules["guardX"], expected_rules["bridgeCenterX"] - 1, "Guard Towers exactly one tile inward")
        expected_rules["pocketOuterX"] = valid_rules.get("pocketOuterX", expected_rules["arenaWidth"] / 2 - .8)
        expected_rules["pocketMaxDepth"] = valid_rules.get("pocketMaxDepth", expected_rules["guardDepth"] - 3.15)
        policy = ("rift-native-1|native-observed-2|ai-v15-port-2|fingerprint-policy-2|"
                  f"telemetry-{revision}|"
                  f"arena{expected_rules['arenaWidth']:.9g}x{expected_rules['arenaHeight']:.9g}|"
                  f"river{expected_rules['riverHalfWidth']:.9g}|bridges{expected_rules['bridgeCenterX']:.9g},{expected_rules['bridgeWidth']:.9g}|"
                  "phase180,120|aether2.8,120,240|drain180|coreGuardOnly|"
                  f"pocket2,{expected_rules['pocketOuterX']:.9g},2.25,{expected_rules['pocketMaxDepth']:.9g}|"
                  f"towers{expected_rules['guardX']:.9g},{expected_rules['guardDepth']:.9g},{expected_rules['coreDepth']:.9g}|"
                  "layered-swept-disc-1|collisionSkin0.02|structureClearance0.22|")
        canonical_navigation = matching_rules.get("navigationRevision", 0)
        check(isinstance(canonical_navigation, int) and not isinstance(canonical_navigation, bool) and canonical_navigation >= 8,
              "Dynamic arena policy requires its resized navigation revision")
    else:
        # Preserve exact policy1 bytes for previously captured canonical cohorts.
        policy = ("rift-native-1|native-observed-2|ai-v15-port-2|telemetry-3|arena30x44|river1.65|bridges7.2,4.2|phase180,120|aether2.8,120,240|drain180|coreGuardOnly|pocket2,14.2,2.25,10.25|towers7.2,13.4,17.3|layered-swept-disc-1|collisionSkin.02|structureClearance.22|")
        expected_rules.update(matching_rules)
        check(matching_rules.get("navigationRevision", 0) == 7, "Policy1 requires its historical navigation revision")
    fingerprint = hashlib.md5((policy + manifest["balanceHash"]).encode("ascii")).hexdigest()
    check(fingerprint == data["fingerprint"], "Canonical gameplay/policy fingerprint mismatch")
    expected_rules["guardBridgeAlignment"] = alignment_rule
    for key, value in expected_rules.items():
        if isinstance(value, (int,float)):
            near(data["rulesSnapshot"].get(key,-1), value, f"Captured immutable rule: {key}")
        else:
            check(data["rulesSnapshot"].get(key) == value, f"Captured immutable rule: {key}")
else:
    check(revision in (2, 3), "Legacy no-hash datasets cannot claim expansion telemetry4")
    canonical = ("rift-native-1|native-observed-2|ai-v15-port-2|nav-grid-a-star-1|telemetry-2|"
                 "arena28x42|river1.65|bridges7.2,4.2|sight8,5|phase180,120|aether2.8,120,240|"
                 "drain180|coreGuardOnly|hardlockAtRange|pocket2,13.2,2.25,9.25")
    canonical = canonical.replace("telemetry-2", f"telemetry-{revision}")
    navigation_revision = data["rulesSnapshot"].get("navigationRevision", 1)
    check(not isinstance(navigation_revision, bool) and navigation_revision in (1, 2, 3, 4, 5, 6),
          "Unsupported captured navigation revision")
    check(not alignment_present or (navigation_revision == 6 and aligned_guards),
          "Unsupported captured tower/bridge alignment rule")
    historical_roster = {"ironclad", "ember_archer", "twin_blades", "boulderback", "arc_mage",
                         "rambeast", "sky_manta", "vampire_bats", "frost_fang", "storm_raven",
                         "meteor_shards", "archer_tower", "bullet_burst", "nova_flask"}
    combat_revision = data["rulesSnapshot"].get("combatRevision", 0)
    check(not isinstance(combat_revision, bool) and combat_revision in (0, 1), "Unsupported combat revision")
    expected_roster = historical_roster | {"mini_stampede", "stampede"} if navigation_revision >= 6 else historical_roster
    if combat_revision == 1:
        check(navigation_revision == 6 and aligned_guards, "Unsupported shockwave/navigation combination")
        expected_roster |= {"pulverizer"}
    check(set(definitions) == expected_roster and len(data["cardSnapshot"]) == len(expected_roster) and
          data["aiStyles"] == styles, "Captured revision roster/style identity mismatch")
    canonical = canonical.replace("nav-grid-a-star-1", f"nav-grid-a-star-{navigation_revision}")
    if navigation_revision >= 3:
        canonical = canonical.replace("arena28x42", "arena30x44").replace(
            "pocket2,13.2,2.25,9.25", "pocket2,14.2,2.25,10.25|towers8.2,13.4,17.3|source-side-bridge-1|same-lane-guard-else-core-1")
    if navigation_revision >= 4:
        canonical += "|layered-swept-disc-1|crowd-steering-1"
    if navigation_revision >= 5:
        canonical += "|visible-pocket-connectors-1"
    if navigation_revision >= 6:
        canonical += "|per-member-landing-lane-1"
        if aligned_guards:
            canonical = canonical.replace("|towers8.2,13.4,17.3|", "|towers7.2,13.4,17.3|")
            canonical += "|tower-bridge-alignment-1"
    timed_spells = any("castDelay" in card for card in data["cardSnapshot"])
    if timed_spells:
        check(all("castDelay" in card for card in data["cardSnapshot"]),
              "Incomplete immutable spell timing snapshot")
        canonical += "|spell-impact-fixed-point-1"
    if combat_revision == 1:
        canonical += "|combat-1|strict-card-detection-1|traveling-ground-shockwave-1"
    fingerprint_fields = ("cost count hp damage attackInterval moveSpeed range scale projectileSpeed splash lifetime "
                          "footprint towerDamage spellRadius chargeDamage slowPct slowDuration auraDamage "
                          "auraInterval auraRadius stunDuration dotDamage dotDuration dotInterval rounds "
                          "flying canHitAir structuresOnly spell building").split()
    if combat_revision == 1:
        fingerprint_fields.insert(fingerprint_fields.index("projectileSpeed") + 1, "shockwaveSpeed")
        for card in data["cardSnapshot"]:
            near(card.get("shockwaveSpeed", -1), 8 if card["id"] == "pulverizer" else 0, f"Captured shockwave speed: {card['id']}")
    if timed_spells:
        fingerprint_fields.insert(fingerprint_fields.index("spellRadius") + 1, "castDelay")
        for card in data["cardSnapshot"]:
            near(card.get("castDelay", -1), {"meteor_shards": .75, "bullet_burst": .30}.get(card["id"], 0),
                 f"Captured cast delay: {card['id']}")
    for card in data["cardSnapshot"]:
        canonical += card["id"]
        for key in fingerprint_fields:
            check(key in card, f"Missing immutable card field: {card['id']}.{key}")
            canonical += f"|{key}={float(card.get(key, 0)):.9g}"
    fingerprint = hashlib.md5(canonical.encode("ascii")).hexdigest()
    check(fingerprint == data["fingerprint"], "Immutable card/rule fingerprint mismatch")
    expected_rules = {"arenaWidth": 28, "arenaHeight": 42, "riverHalfWidth": 1.65,
                      "bridgeCenterX": 7.2, "bridgeWidth": 4.2, "frontSight": 8, "rearSight": 5,
                      "regulationSeconds": 180, "overtimeSeconds": 120, "aetherInterval": 2.8,
                      "doubleAetherAt": 120, "tripleAetherAt": 240, "tiebreakerDrainPerSecond": 180,
                      "openingAether": 5, "maximumAether": 10,
                      "coreActivation": "friendly Guard Tower destroyed",
                      "targetHardLock": "at Crown Tower attack range",
                      "navigation": "card-aware ground grid A-star with bridges; flying ignores obstacles"}
    if navigation_revision >= 2:
        expected_rules["navigationRevision"] = navigation_revision
        expected_rules["navigation"] += "; clear ground spawns and continuous segment clearance"
    if navigation_revision >= 3:
        expected_rules.update(arenaWidth=30, arenaHeight=44, coreDepth=17.3, guardDepth=13.4,
                              guardX=8.2, pocketOuterX=14.2, pocketMaxDepth=10.25)
        expected_rules["navigation"] += "; source-side bridge commitment; same-lane Guard else Core"
    if navigation_revision >= 4:
        expected_rules["navigation"] = "card-aware ground grid A-star with bridges; flying ignores ground obstacles; clear layered body spawns and swept disc clearance; source-side bridge commitment; same-lane Guard else Core; dynamic crowd costs and local steering"
        expected_rules.update(collisionSkin=.02, structureClearance=.22,
                              collision="ground troops and structures share a layer; flying troops share an air layer; both teams collide; spells have no body")
    if navigation_revision >= 5:
        expected_rules["navigation"] += "; visible continuous pocket connectors"
    if navigation_revision >= 6:
        expected_rules["navigation"] += "; per-member landing lanes and radius-aware swarm formations"
        if aligned_guards:
            expected_rules.update(guardX=7.2, guardBridgeAlignment=alignment_rule)
            expected_rules["navigation"] += "; Guard Towers aligned with bridge centers"
    if combat_revision == 1:
        expected_rules.update(combatRevision=1,
            detection="strict center distance: 8 tiles forward, 5 rear; eligible targets only; Crown advance and engaged Crown hard locks preserved",
            shockwave="expanding ground troop-only front; one hit per wave; source death does not cancel; per-card slow refresh, strongest active effect wins")
    for key, value in expected_rules.items():
        if isinstance(value, (int, float)):
            near(data["rulesSnapshot"].get(key, -1), value, f"Captured rule mismatch: {key}")
        else:
            check(data["rulesSnapshot"].get(key) == value, f"Captured rule mismatch: {key}")
    if timed_spells:
        check(data["rulesSnapshot"].get("spellImpact") ==
              "fixed target area; current enemy positions at impact; Meteor Shards 0.75s, Bullet Burst 0.30s; Nova Flask instant",
              "Captured spell impact rule mismatch")
games, attempts = data["games"], data["economyChecks"]
check(games == int(games) and games >= 0, "Invalid games counter")
check(attempts == games + data["invalid"], "Attempt/valid/invalid counters disagree")
check(data["economyInvalid"] == 0 and data["invalid"] == 0, "Worker reported invalid matches")
check(0 <= data["maximumEconomyResidual"] < .05, "Worker economy residual outside contract")
check(0 <= data["duration"] <= 420 * games, "Aggregate duration outside terminal bound")
seed = args.initial_seed
expected_styles = collections.Counter()
for _ in range(int(attempts)):
    expected_styles[styles[seed % 7]] += 1
    expected_styles[styles[(seed // 7) % 7]] += 1
    seed = (seed * 1664525 + 1013904223) & 0xffffffff
check(seed == data["seed"], "Saved RNG seed does not match complete seeded sequence")
buckets = data["buckets"]
overall = buckets["all|all"]
near(overall["n"], 2 * games, "Overall deck observations")
near(overall["score"], games, "Paired match scores")
near(overall["duration"], 2 * data["duration"], "Paired duration totals")
check(overall["draws"] % 2 == 0, "Draw side count must be even")
for style in styles:
    near(buckets[f"{style}|all"]["n"], expected_styles[style], f"Seeded style frequency {style}")

count_fields = ("appearances mirror cleanN plays spawns surviving kills deaths pulls connected targets "
                "openingPlays firstPlays overtimePlays stunned dotTicks openingEligible").split()
status_fields = ("slowTime slowTrackedSeconds stunTime stunTrackedSeconds").split()
special_fields = ("initialDamage dotDamage auraDamage zoneOccupancy zoneSeconds buildingLifetime "
                  "buildingCapacity prevented").split()
if revision >= 4:
    count_fields += "childSpawns childDeaths childSurviving".split()
    special_fields += "shieldDamage shieldDamageTaken shieldAbsorbed overflowPrevented".split()
for bucket_id, bucket in buckets.items():
    n, cards = bucket["n"], bucket["cards"]
    check(n >= 0 and 0 <= bucket["score"] <= n, f"{bucket_id}: invalid score/sample range")
    near(sum(card["appearances"] for card in cards.values()), 8 * n, f"{bucket_id}: eight-card decks")
    near(sum(number(card, "spent") for card in cards.values()), bucket["spent"], f"{bucket_id}: paid-spend accounting")
    near(sum(number(card, "openingEligible") for card in cards.values()), 4 * n, f"{bucket_id}: opening-hand eligibility")
    near(sum(number(card, "firstPlays") for card in cards.values()), n, f"{bucket_id}: one first play per side")
    near(sum(number(card, "n") for card in bucket["opponentStyles"].values()), n, f"{bucket_id}: opponent styles")
    near(sum(number(card, "score") for card in bucket["opponentStyles"].values()), bucket["score"],
         f"{bucket_id}: opponent style scores")
    for card_id, stats in cards.items():
        definition = definitions[card_id]
        prefix = f"{bucket_id}/{card_id}"
        for key, value in stats.items():
            if key not in ("placementX", "placementZ"):
                check(value >= -1e-7, f"{prefix}: negative {key}")
        for key in count_fields:
            value = number(stats, key)
            check(value == int(value), f"{prefix}: fractional count {key}")
        ap, clean, mirror = (number(stats, key) for key in ("appearances", "cleanN", "mirror"))
        plays, spawns = (number(stats, key) for key in ("plays", "spawns"))
        near(ap, clean + mirror, f"{prefix}: clean/mirror decomposition")
        check(0 <= number(stats, "score") <= clean and ap <= n, f"{prefix}: clean score range")
        near(number(stats, "spent"), plays * definition["cost"], f"{prefix}: cost per paid cast")
        near(spawns, 0 if definition["spell"] else plays * definition["count"], f"{prefix}: swarm member spawns")
        near(spawns, number(stats, "deaths") + number(stats, "surviving"), f"{prefix}: deaths/survivors")
        if revision >= 4:
            children = number(stats, "childSpawns")
            near(children, number(stats, "childDeaths") + number(stats, "childSurviving"), f"{prefix}: child deaths/survivors")
            child_capacity = definition.get("deathSpawnCount", 0) * number(stats, "deaths")
            if definition.get("spawnId"):
                child_capacity += math.floor(definition["lifetime"] / definition["spawnInterval"] + 1e-9) * plays
            check(children <= child_capacity + 1e-5, f"{prefix}: children exceed actual paid-parent production/deaths")
            shield_capacity = definition.get("shieldHP", 0) * spawns
            child = definitions.get(definition.get("spawnId")) or entity_definitions.get(definition.get("deathSpawnId"), {})
            shield_capacity += child.get("shieldHP", 0) * children
            check(number(stats, "shieldDamageTaken") <= shield_capacity + 1e-5, f"{prefix}: shield loss exceeds actual shields")
            near(number(stats, "shieldAbsorbed"), number(stats, "shieldDamageTaken") + number(stats, "overflowPrevented"),
                 f"{prefix}: real shield loss and discarded overflow decomposition")
            if not definition.get("shieldOverflowProtected"):
                near(number(stats, "overflowPrevented"), 0, f"{prefix}: no protected overflow rule")
        check(number(stats, "openingPlays") <= number(stats, "openingEligible") <= ap,
              f"{prefix}: opening denominator range")
        check(number(stats, "firstPlays") <= ap and number(stats, "overtimePlays") <= plays and
              number(stats, "connected") <= plays, f"{prefix}: per-cast event bounds")
        if "balanceHash" in data:
            last_tile_x, last_tile_z = expected_rules["arenaWidth"] / 2 - .5, expected_rules["arenaHeight"] / 2 - .5
        else:
            # Retain the original pre-canonical revision4/5 deployment convention.
            expanded_placement = navigation_revision == 3 or navigation_revision >= 6
            last_tile_x, last_tile_z = (14.5, 21.5) if expanded_placement else (13.5, 20.5)
        check(abs(number(stats, "placementX")) <= last_tile_x * plays + 1e-5 and
              abs(number(stats, "placementZ")) <= last_tile_z * plays + 1e-5, f"{prefix}: paid placement bounds")
        check(number(stats, "slowTime") <= number(stats, "slowTrackedSeconds") + 1e-5 and
              number(stats, "stunTime") <= number(stats, "stunTrackedSeconds") + 1e-5,
              f"{prefix}: credited status exceeds affected exposure")
        child_spawns = number(stats, "childSpawns") if revision >= 4 else 0
        child = definitions.get(definition.get("spawnId")) or entity_definitions.get(definition.get("deathSpawnId"), {})
        check(number(stats, "lifetime") <= 420 * (spawns + child_spawns) + 1e-5 and
              number(stats, "damageTaken") <= definition["hp"] * spawns + child.get("hp", 0) * child_spawns + 1e-5,
              f"{prefix}: member HP/lifetime limits")
        if definition["structuresOnly"]:
            # Raven's basic attacks target structures; its explicit aura hits troops.
            near(number(stats, "troopDamage"), number(stats, "auraDamage"),
                 f"{prefix}: basic structure-only targeting with troop aura exception")
        if definition["spell"]:
            for key in ("spawns", "surviving", "deaths", "lifetime", "damageTaken"):
                near(number(stats, key), 0, f"{prefix}: spell has no spawned member {key}")
            near(number(stats, "initialDamage") + number(stats, "dotDamage"),
                 number(stats, "troopDamage") + number(stats, "towerDamage") + number(stats, "buildingDamage"),
                 f"{prefix}: initial/DOT decomposition")
        if card_id == "meteor_shards":
            if not int(definition.get("targetMask", 3)) & 4:
                near(number(stats, "towerDamage") + number(stats, "buildingDamage"), 0, f"{prefix}: troop-only spell")
            exposure_capacity = 5 * plays
            if revision == 2:
                # Historical revision2 was found to credit one pre-birth tick.
                if number(stats, "zoneSeconds") > exposure_capacity + 1e-5:
                    legacy_exposure.append({"bucket": bucket_id, "plays": plays,
                                            "excessSeconds": number(stats, "zoneSeconds") - exposure_capacity})
                exposure_capacity += plays / 60
            check(number(stats, "dotTicks") <= 5 * plays and number(stats, "zoneSeconds") <= exposure_capacity + 1e-5,
                  f"{prefix}: inclusive five-tick/five-second bounds")
        else:
            for key in ("dotDamage", "dotTicks", "zoneSeconds", "zoneOccupancy"):
                near(number(stats, key), 0, f"{prefix}: no DOT/zone {key}")
        if definition["slowPct"] <= 0:
            near(number(stats, "slowTime") + number(stats, "slowTrackedSeconds"), 0, f"{prefix}: no slow source")
        if card_id == "pulverizer":
            near(number(stats, "towerDamage") + number(stats, "buildingDamage"), 0, f"{prefix}: ground troop-only shockwave")
        if definition.get("stunDuration", 0) <= 0:
            near(number(stats, "stunTime") + number(stats, "stunTrackedSeconds") +
                 number(stats, "stunned"), 0, f"{prefix}: no stun source")
        if definition.get("auraDamage", 0) <= 0:
            near(number(stats, "auraDamage"), 0, f"{prefix}: no aura source")
        else:
            check(number(stats, "auraDamage") <= number(stats, "troopDamage"), f"{prefix}: troop-only aura")
        if definition["building"]:
            near(number(stats, "buildingCapacity"), definition["lifetime"] * spawns, f"{prefix}: building capacity")
            check(number(stats, "buildingLifetime") <= number(stats, "buildingCapacity") + 1e-5,
                  f"{prefix}: lifetime utilization")
            body_damage = number(stats, "damageTaken")
            if revision >= 4:
                body_damage = sum(number(body, "damageTaken") for body in bucket.get("entities", {}).values()
                                  if body.get("parentCardId") == card_id and not body.get("subordinate"))
            near(number(stats, "prevented"), body_damage, f"{prefix}: building body damage interception")
        else:
            near(sum(number(stats, key) for key in ("buildingLifetime", "buildingCapacity", "prevented", "pulls")),
                 0, f"{prefix}: no building attribution")
    if revision >= 4:
        observed = bucket.get("entities", {})
        check("entities" in bucket, f"{bucket_id}: missing separate entity observations")
        all_entities = {**definitions, **entity_definitions}
        for identity, stats in observed.items():
            prefix = f"{bucket_id}/entity/{identity}"
            card_id = stats.get("cardId") if revision >= 5 else identity
            definition = all_entities.get(card_id)
            check(definition is not None and stats.get("cardId") == card_id, f"{prefix}: recorded entity identity")
            if definition is None:
                continue
            parent = stats.get("parentCardId")
            subordinate = parent != card_id if revision >= 5 else identity in entity_definitions
            expected_key = parent + '/' + card_id if revision >= 5 and subordinate and isinstance(parent,str) and isinstance(card_id,str) else card_id
            check(identity == expected_key, f"{prefix}: distinct paid/summoned observation key")
            check(stats.get("subordinate") is subordinate and parent in definitions and
                  (card_id in (definitions[parent].get("deathSpawnId"),definitions[parent].get("spawnId")) if subordinate else parent == card_id),
                  f"{prefix}: parent attribution")
            near(number(stats, "plays") + number(stats, "spent"), 0, f"{prefix}: bodies do not create paid casts or spend")
            near(number(stats, "observations"), number(stats, "spawns"), f"{prefix}: one stable body per observation")
            near(number(stats, "spawns"), number(stats, "deaths") + number(stats, "surviving"), f"{prefix}: body outcomes")
            check(number(stats, "damageTaken") <= definition["hp"] * number(stats, "spawns") + 1e-5,
                  f"{prefix}: body HP budget")
            near(number(stats, "prevented"), number(stats, "damageTaken") if definition["building"] else 0,
                 f"{prefix}: only building bodies intercept damage")
            check(number(stats, "shieldDamageTaken") <= definition.get("shieldHP", 0) * number(stats, "spawns") + 1e-5,
                  f"{prefix}: separate shield HP budget")
            near(number(stats, "shieldAbsorbed"), number(stats, "shieldDamageTaken") + number(stats, "overflowPrevented"),
                 f"{prefix}: shield absorption")
        body_rollup_fields = ("troopDamage towerDamage buildingDamage damageTaken kills killValue lifetime initialDamage dotDamage "
                              "auraDamage overkill prevented buildingLifetime shieldDamage shieldDamageTaken shieldAbsorbed overflowPrevented").split()
        for identity, parent in cards.items():
            if definitions[identity]["spell"]:
                continue
            bodies = [row for row in observed.values() if row.get("parentCardId") == identity]
            paid = [row for row in bodies if not row.get("subordinate")]
            children = [row for row in bodies if row.get("subordinate")]
            for field in ("spawns", "deaths", "surviving"):
                near(number(parent, field), sum(number(row, field) for row in paid), f"{bucket_id}/{identity}: paid body rollup {field}")
                child_field = {"spawns":"childSpawns", "deaths":"childDeaths", "surviving":"childSurviving"}[field]
                near(number(parent, child_field), sum(number(row, field) for row in children), f"{bucket_id}/{identity}: child rollup {field}")
            for field in body_rollup_fields:
                near(number(parent, field), sum(number(row, field) for row in bodies), f"{bucket_id}/{identity}: entity-to-parent rollup {field}")
    pairs = bucket["pairs"]
    near(sum(pair["appearances"] for pair in pairs.values()), 28 * n, f"{bucket_id}: unordered deck pairs")
    card_pair_appearances = collections.Counter()
    for pair_id, pair in pairs.items():
        a, b = pair_id.split("+")
        check(a < b and a in definitions and b in definitions, f"{bucket_id}: pair identity {pair_id}")
        near(pair["appearances"], number(pair, "n") + number(pair, "mirrors"), f"{bucket_id}/{pair_id}: mirrors")
        check(0 <= number(pair, "score") <= number(pair, "n"), f"{bucket_id}/{pair_id}: pair score range")
        card_pair_appearances[a] += pair["appearances"]
        card_pair_appearances[b] += pair["appearances"]
    for card_id, stats in cards.items():
        near(card_pair_appearances[card_id], 7 * stats["appearances"], f"{bucket_id}/{card_id}: seven deck partners")


def audit_rows(rows, observations, raw=None, prefix="rows"):
    check(len(rows) == len(expected_roster) and {row["id"] for row in rows} == expected_roster,
          f"{prefix}: complete captured revision roster")
    near(sum(row["pickRate"] for row in rows), 800, f"{prefix}: deck pick percentages")
    for row in rows:
        card_id, count = row["id"], row["cleanN"]
        ap = row["appearances"]
        score = (row["rawWinRate"] or 0) * count / 100
        near(ap, count + row["mirrorExclusions"], f"{prefix}/{card_id}: mirror exclusions")
        near(row["pickRate"], 100 * ap / observations, f"{prefix}/{card_id}: pick denominator")
        near(row["adjustedWinRate"], 100 * (score + 12) / (count + 24), f"{prefix}/{card_id}: Bayesian adjustment")
        low, high = wilson(score, count)
        near(row["ciLow"], low, f"{prefix}/{card_id}: Wilson low")
        near(row["ciHigh"], high, f"{prefix}/{card_id}: Wilson high")
        for key in ("survivalRate", "openingHandPlayRate", "firstPlayRate", "otPlayRate", "connectionRate",
                    "lifetimeUtilization", "slowUptime", "stunUptime"):
            value = row.get(key)
            check(value is None or -.00001 <= value <= 100.00001, f"{prefix}/{card_id}: bounded rate {key}")
        if raw:
            stats = raw[card_id]
            near(count, number(stats, "cleanN"), f"{prefix}/{card_id}: exported clean samples")
            near(ap, number(stats, "appearances"), f"{prefix}/{card_id}: exported deck observations")
            near(score, number(stats, "score"), f"{prefix}/{card_id}: exported outcome score")
            for key, numerator, denominator, scale in (
                    ("usesPerGame", "plays", "appearances", 1),
                    ("aetherPerGame", "spent", "appearances", 1),
                    ("troopDamagePerGame", "troopDamage", "appearances", 1),
                    ("towerDamagePerGame", "towerDamage", "appearances", 1),
                    ("buildingDamagePerGame", "buildingDamage", "appearances", 1),
                    ("damageTakenPerGame", "damageTaken", "appearances", 1),
                    ("killsPerGame", "kills", "appearances", 1),
                    ("deathsPerGame", "deaths", "appearances", 1),
                    ("crownContribution", "crownContribution", "appearances", 1),
                    ("damagePreventedPerGame", "prevented", "appearances", 1),
                    ("pullsPerGame", "pulls", "appearances", 1),
                    ("auraDamagePerGame", "auraDamage", "appearances", 1),
                    ("unitsStunnedPerGame", "stunned", "appearances", 1),
                    ("initialDamagePerGame", "initialDamage", "appearances", 1),
                    ("dotDamagePerGame", "dotDamage", "appearances", 1),
                    ("averagePlacementX", "placementX", "plays", 1),
                    ("averagePlacementZ", "placementZ", "plays", 1),
                    ("averageLifetime", "lifetime", "observedBodies", 1),
                    ("averageKillValue", "killValue", "kills", 1),
                    ("towerDamagePerAether", "towerDamage", "spent", 1),
                    ("survivalRate", "surviving", "spawns", 100),
                    ("openingHandPlayRate", "openingPlays", "openingEligible", 100),
                    ("firstPlayRate", "firstPlays", "appearances", 100),
                    ("otPlayRate", "overtimePlays", "plays", 100),
                    ("connectionRate", "connected", "plays", 100),
                    ("lifetimeUtilization", "buildingLifetime", "buildingCapacity", 100),
                    ("targetsPerCast", "targets", "plays", 1),
                    ("spellAetherValuePerCast", "spellValue", "plays", 1),
                    ("overkillPerCast", "overkill", "plays", 1),
                    ("slowUptime", "slowTime", "slowTrackedSeconds", 100),
                    ("stunUptime", "stunTime", "stunTrackedSeconds", 100),
                    ("zoneOccupancy", "zoneOccupancy", "zoneSeconds", 1),
                    ("dotTicksPerCast", "dotTicks", "plays", 1)):
                den = number(stats, "spawns") + number(stats, "childSpawns") if denominator == "observedBodies" else number(stats, denominator)
                if den:
                    near(row[key], scale * number(stats, numerator) / den, f"{prefix}/{card_id}: {key}")
                else:
                    check(row[key] is None, f"{prefix}/{card_id}: undefined {key} must remain null")
            if revision >= 4:
                for key, field in (("shieldDamagePerGame","shieldDamage"), ("shieldDamageTakenPerGame","shieldDamageTaken"),
                                   ("shieldAbsorbedPerGame","shieldAbsorbed"), ("overflowPreventedPerGame","overflowPrevented"),
                                   ("childrenSpawnedPerGame","childSpawns"), ("childrenDiedPerGame","childDeaths"),
                                   ("childrenSurvivedPerGame","childSurviving")):
                    check(key in row, f"{prefix}/{card_id}: missing expansion metric {key}")
                    if ap:
                        near(row.get(key,-1), number(stats,field)/ap, f"{prefix}/{card_id}: {key}")
                    else:
                        check(row.get(key,"missing") is None, f"{prefix}/{card_id}: unavailable {key}")
            damage = sum(number(stats, field) for field in ("troopDamage", "towerDamage", "buildingDamage"))
            for key, denominator in (("damagePerAether", "spent"), ("damagePerCast", "plays")):
                den = number(stats, "spawns") + number(stats, "childSpawns") if denominator == "observedBodies" else number(stats, denominator)
                if den:
                    near(row[key], damage / den, f"{prefix}/{card_id}: {key}")
                else:
                    check(row[key] is None, f"{prefix}/{card_id}: undefined {key} must remain null")


previous = -1
for point in data["checkpoints"]:
    check(previous < point["games"] <= games, "Checkpoint games not increasing")
    audit_rows(point["cards"], point["games"] * 2, prefix=f"checkpoint{point['games']}")
    previous = point["games"]
export_validated = False
if args.export and args.export.exists():
    export = json.loads(args.export.read_text(encoding="utf-8-sig"))
    check(export["model"] == data["model"] and export["fingerprint"] == data["fingerprint"], "Export identity")
    audit_rows(export["rows"], overall["n"], overall["cards"], "finalExport")
    export_validated = True

comparison = None
if args.compare:
    baseline = json.loads(args.compare.read_text(encoding="utf-8-sig"))
    baseline_revision = baseline["telemetryRevision"]
    check(revision == 3 and baseline_revision in (2, 3), "Comparison requires revision3 vs revision2/3")
    compared_fields = 0

    def compare_values(actual, expected, path):
        global compared_fields
        if baseline_revision == 2 and (path.endswith(".zoneSeconds") or path.endswith(".zoneOccupancy")):
            check(actual <= expected + 1e-5, f"{path}: corrected exposure must not increase")
            return
        if isinstance(expected, dict):
            check(actual.keys() == expected.keys(), f"{path}: aggregate field set changed")
            for key in expected.keys() & actual.keys():
                compare_values(actual[key], expected[key], f"{path}.{key}")
        elif isinstance(expected, list):
            check(actual == expected, f"{path}: immutable contract changed")
            compared_fields += 1
        elif isinstance(expected, (int, float)):
            near(actual, expected, f"{path}: retained gameplay/economy aggregate")
            compared_fields += 1
        else:
            check(actual == expected, f"{path}: identity changed")
            compared_fields += 1

    comparison_start_errors = len(errors)
    for key in ("games", "invalid", "seed", "duration", "economyChecks", "economyInvalid",
                "maximumEconomyResidual", "cardSnapshot", "rulesSnapshot", "aiStyles", "buckets"):
        compare_values(data[key], baseline[key], key)
    for bucket_id, bucket in buckets.items():
        current_meteor = bucket["cards"].get("meteor_shards")
        old_meteor = baseline["buckets"][bucket_id]["cards"].get("meteor_shards")
        if current_meteor and old_meteor:
            removed_time = number(old_meteor, "zoneSeconds") - number(current_meteor, "zoneSeconds")
            check(-1e-5 <= removed_time <= number(current_meteor, "plays") / 60 + 1e-5,
                  f"{bucket_id}: correction removes at most one pre-birth tick per Meteor cast")
    comparison = {"baseline": str(args.compare.resolve()), "baselineFingerprint": baseline["fingerprint"],
                  "baselineTelemetryRevision": baseline_revision,
                  "comparedFields": compared_fields, "failedChecks": len(errors) - comparison_start_errors,
                  "scope": ("All retained game outcomes, durations, seeded populations, paid economy, damage, member/cast counts, statuses and aggregate slices; only corrected zone exposure/occupancy differ."
                            if baseline_revision == 2 else
                            "All retained gameplay/economy/status aggregates and slices, including zone exposure/occupancy, compared against a telemetry3 cohort.")}

# Every style slice must partition its matching all-style slice, including cards and pairs.
for bucket_id, bucket in buckets.items():
    if not bucket_id.startswith("all|"):
        continue
    archetype = bucket_id.split("|", 1)[1]
    slices = [buckets[f"{style}|{archetype}"] for style in styles if f"{style}|{archetype}" in buckets]
    for key in ("n", "score", "draws", "duration", "spent", "leaked", "crowns"):
        near(bucket[key], sum(number(child, key) for child in slices), f"{bucket_id}: style partition {key}")
    for category in ("cards", "pairs"):
        for identity, stats in bucket[category].items():
            for key, value in stats.items():
                near(value, sum(number(child[category].get(identity, {}), key) for child in slices),
                     f"{bucket_id}/{identity}: style partition {key}")
    if revision >= 4:
        for identity, stats in bucket.get("entities", {}).items():
            for key, value in stats.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    near(value, sum(number(child.get("entities", {}).get(identity, {}), key) for child in slices),
                         f"{bucket_id}/entity/{identity}: style partition {key}")

log_completed = False
if args.log and args.log.exists():
    log_completed = f"Native Meta validation completed: {args.target} actual games." in args.log.read_text(
        encoding="utf-8-sig", errors="replace")
if args.require_complete:
    check(games == args.target and log_completed and export_validated,
          "Complete target cohort, native completion log and validated final export were not all observed")
summary_cards = []
for card_id, stats in overall["cards"].items():
    clean, score = number(stats, "cleanN"), number(stats, "score")
    low, high = wilson(score, clean)
    summary_cards.append({"id": card_id, "deckAppearances": stats["appearances"], "cleanN": clean,
                          "mirrorExclusions": number(stats, "mirror"),
                          "pickRate": 100 * stats["appearances"] / overall["n"],
                          "adjustedWinRate": 100 * (score + 12) / (clean + 24),
                          "ciLow": low, "ciHigh": high, "plays": number(stats, "plays")})
summary_cards.sort(key=lambda row: row["adjustedWinRate"], reverse=True)
report = {
    "auditedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "dataset": str(args.dataset.resolve()), "fingerprint": fingerprint, "telemetryRevision": revision,
    "navigationRevision": navigation_revision, "capturedCardCount": len(definitions),
    "capturedSubordinateCount": len(entity_definitions),
    "fingerprintPolicyRevision": fingerprint_policy_revision, "balanceHash": data.get("balanceHash"),
    "matchingManifest": str(args.manifest.resolve()) if "balanceHash" in data else None,
    "capturedArena": {key:data["rulesSnapshot"].get(key) for key in ("arenaWidth", "arenaHeight", "bridgeCenterX", "guardDepth", "coreDepth")},
    "alignedGuards": aligned_guards,
    "initialSeed": args.initial_seed,
    "observedGames": games, "target": args.target,
    "completeObserved": games == args.target and log_completed and export_validated,
    "attempts": attempts, "invalid": data["invalid"], "economyInvalid": data["economyInvalid"],
    "maximumEconomyResidual": data["maximumEconomyResidual"], "checks": checks,
    "failedChecks": len(errors), "errors": errors[:200], "buckets": len(buckets),
    "checkpoints": len(data["checkpoints"]), "finalExportValidated": export_validated,
    "deckObservations": overall["n"], "simulatedSeconds": data["duration"],
    "activeSimulationWallSeconds": data["simulationWallSeconds"],
    "meanSimulatedMatchSeconds": data["duration"] / games if games else 0,
    "meanActiveSimulationWallSeconds": data["simulationWallSeconds"] / attempts if attempts else 0,
    "drawMatches": overall["draws"] / 2,
    "totalPaidAether": overall["spent"], "totalLeakedAether": overall["leaked"],
    "totalPaidCasts": sum(number(card, "plays") for card in overall["cards"].values()),
    "totalSpawnedMembers": sum(number(card, "spawns") for card in overall["cards"].values()),
    "totalChildMembers": sum(number(card, "childSpawns") for card in overall["cards"].values()),
    "entityObservations": overall.get("entities") if revision >= 4 else None,
    "styleObservations": dict(expected_styles), "cards": summary_cards,
    "specialTelemetry": {key: {field: number(overall["cards"][key], field)
                                for field in status_fields + special_fields + ["plays", "spawns", "pulls", "dotTicks"]}
                         for key in ("frost_fang", "storm_raven", "meteor_shards", "archer_tower", "pulverizer") if key in overall["cards"]},
    "legacyExposureLimitations": legacy_exposure,
    "gameplayAggregateComparison": comparison,
    "limits": ["Aggregate dataset retains no complete per-match snapshots/events; per-match economy checks are worker evidence.",
               "Card/pair win rates are associations in generated decks, not isolated card strength or causal balance proof.",
               "Mirror-excluded card observations still share matches/decks; Wilson intervals are descriptive, not independent experimental confidence.",
               "Archetype labels overlap and their pick percentages need not sum to 100%.",
               "Active simulation time sums core construction/Step/event-drain calls; excludes pause/throttle waits and aggregation/JSON/persistence overhead. It is not a frame-rate, UI-rate, or elapsed-runtime measurement."]
}
if revision == 2:
    report["limits"].append("Telemetry revision2 credits up to one fixed tick before an AI Meteor cast to both zoneSeconds and occupancy. Revision3 fixes the overlap without changing gameplay.")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
print(json.dumps({key: report[key] for key in ("observedGames", "completeObserved", "checks", "failedChecks",
                                             "maximumEconomyResidual", "buckets", "checkpoints")}, indent=2))
if errors:
    print("\n".join(errors[:30]))
raise SystemExit(1 if errors else 0)
