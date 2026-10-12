/**
 * Export the approved canonical balance manifest as a formula-driven workbook.
 * Uses the bundled artifact-tool runtime, never game/repository dependencies.
 *
 * Before the first workbook authoring call, run the Spreadsheets skill's
 * mark_artifact_operation_started.mjs once. Create a task-specific node_modules
 * junction to the path returned by load_workspace_dependencies, then pass its
 * parent as --runtime-directory. --self-test needs no spreadsheet dependency.
 */
import fs from "node:fs/promises";
import path from "node:path";
import { createHash } from "node:crypto";
import { createRequire } from "node:module";
import { pathToFileURL, fileURLToPath } from "node:url";

const FIELD_UNITS = {
  cost: "Aether", count: "units", hp: "HP per unit", shieldHP: "shield HP per unit",
  damage: "damage per hit per unit", attackInterval: "s", moveSpeed: "tiles/s",
  range: "tiles", projectileSpeed: "tiles/s", shockwaveSpeed: "tiles/s",
  splash: "tiles", lifetime: "s", footprint: "tiles", collisionRadius: "tiles",
  scale: "model scale", towerDamage: "damage per structure", spellRadius: "tiles",
  castDelay: "s", chargeDamage: "total charged hit damage", chargeMoveSeconds: "s",
  chargedMoveSpeed: "tiles/s", slowPct: "fraction", slowDuration: "s",
  auraDamage: "damage per pulse", auraInterval: "s", auraRadius: "tiles",
  stunDuration: "s", dotDamage: "damage per troop tick", dotStructureDamage: "damage per structure tick",
  dotDuration: "s", dotInterval: "s", rounds: "visual rounds", hpDecayPerSecond: "HP/s",
  initialAttackCooldown: "s", deathSpawnCount: "children", invisibilityDuration: "s",
  healthCloakThreshold: "fraction of maximum body HP",
  targetMask: "bits: ground troop 1, air troop 2, structure 4",
};
const STAT_NAMES = {
  cost: "Aether cost", count: "Unit count", hp: "Body HP per unit", shieldHP: "Shield HP per unit",
  damage: "Basic damage per unit", attackInterval: "Attack interval", moveSpeed: "Movement speed",
  range: "Attack range", projectileSpeed: "Projectile speed", shockwaveSpeed: "Shockwave speed",
  splash: "Splash radius", lifetime: "Lifetime", footprint: "Building footprint",
  collisionRadius: "Collision radius", scale: "Model scale", towerDamage: "Structure impact damage",
  spellRadius: "Spell radius", castDelay: "Impact delay", chargeDamage: "Total charge damage",
  chargeMoveSeconds: "Accumulated movement threshold", chargedMoveSpeed: "Charged movement speed",
  slowPct: "Movement slow strength", slowDuration: "Movement slow duration", auraDamage: "Aura damage",
  auraInterval: "Aura interval", auraRadius: "Aura radius", stunDuration: "Stun duration",
  dotDamage: "Troop DOT damage per tick", dotStructureDamage: "Structure DOT damage per tick",
  dotDuration: "DOT duration", dotInterval: "DOT interval", rounds: "Visual round count",
  hpDecayPerSecond: "Building HP decay", initialAttackCooldown: "Initial attack cooldown",
  deathSpawnCount: "Death-spawn count", invisibilityDuration: "Invisibility duration",
  healthCloakThreshold: "Health-cloak HP threshold",
  targetMask: "Target mask",
};
const LABEL_FIELDS = ["targetMask", "hardExpiry", "shieldOverflowProtected", "deathSpawnId", "globalFriendlyInvisibility", "breakExistingLocks"];
const MECHANIC_NAMES = { targetMask: "Attack targets", hardExpiry: "Expiry at maximum lifetime", shieldOverflowProtected: "Shield overflow protection", deathSpawnId: "Child definition", globalFriendlyInvisibility: "Global friendly invisibility", breakExistingLocks: "Break existing target locks" };
const ARENA_STATS = { arenaWidth: "Arena width", arenaHeight: "Arena length", bridgeCenterX: "Bridge center X", guardDepth: "Guard tower depth", coreDepth: "Core tower depth" };
const ERROR_RE = "#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!";
const sha = data => createHash("sha256").update(data).digest("hex");
const col = n => { let s = ""; for (let x = n; x; x = Math.floor((x - 1) / 26)) s = String.fromCharCode(65 + (x - 1) % 26) + s; return s; };
const cell = (sheet, address) => `'${sheet.replaceAll("'", "''")}'!${address}`;
const ref = (sheet, address) => `=${cell(sheet, address)}`;
const close = (a, b) => Math.abs(a - b) <= 1e-10 * Math.max(1, Math.abs(a), Math.abs(b));
const mechanicValue = (key, value) => key === "targetMask" ? targetLabel(value) : typeof value === "boolean" ? value ? "Yes" : "No" : value || "None";

export function targetLabel(mask) {
  if (!Number.isInteger(mask) || mask < 0 || mask > 7) throw new Error(`Invalid target mask ${mask}`);
  const labels = [];
  if (mask & 1) labels.push("Ground troops");
  if (mask & 2) labels.push("air troops");
  if (mask & 4) labels.push("structures");
  return labels.length ? labels.join(", ") : "None";
}
export function numericDelta(oldValue, newValue) {
  return { delta: newValue - oldValue, relative: oldValue === 0 ? null : (newValue - oldValue) / oldValue,
    status: oldValue === 0 && newValue !== 0 ? "ADDED" : newValue === 0 && oldValue !== 0 ? "REMOVED" : "CHANGED" };
}
export function manifestRecords(data) {
  return Array.isArray(data) ? { cards: data, entities: [], rules: {} } : data;
}
export function baselineProjection(record, schema) {
  // Schema 2 had no health-triggered cloak. This one documented compatibility
  // default preserves absent historical stats instead of filling them generally.
  return schema >= 3 && record.healthCloakThreshold === undefined ? { ...record, healthCloakThreshold: 0 } : record;
}
function validateManifest(m, fixture) {
  if (!m || ![1, 2, 3].includes(m.schema) || !Array.isArray(m.cards) || !Array.isArray(m.entities)) throw new Error("Expected supported canonical schema with cards and entities arrays");
  if (m.status !== "approved" && !(fixture && m.status === "fixture")) throw new Error("Production export requires an approved manifest; pending source conflicts cannot produce a final workbook");
  if (fixture && m.status !== "fixture") throw new Error("Fixture export requires a clearly labeled synthetic manifest");
  if (typeof m.version !== "string" || !m.version.trim()) throw new Error("Manifest version is required");
  if (!/^[a-f0-9]{64}$/.test(m.balanceHash ?? "")) throw new Error("Canonical balanceHash SHA256 is required");
  const ids = new Set();
  for (const c of [...m.cards, ...m.entities]) {
    if (typeof c.id !== "string" || typeof c.name !== "string" || ids.has(c.id)) throw new Error(`Missing or duplicate entity/card ID: ${c.id}`);
    ids.add(c.id);
    for (const k of ["count", "hp", "damage", "attackInterval", "moveSpeed", "range", "targetMask"]) {
      if (typeof c[k] !== "number" || !Number.isFinite(c[k]) || c[k] < 0) throw new Error(`Missing/invalid ${c.id}.${k}`);
    }
    targetLabel(c.targetMask);
    for (const k of Object.keys(FIELD_UNITS)) if (c[k] !== undefined && (typeof c[k] !== "number" || !Number.isFinite(c[k]) || c[k] < 0)) throw new Error(`Numeric input must remain typed: ${c.id}.${k}`);
    if (m.schema >= 3 && (typeof c.healthCloakThreshold !== "number" || c.healthCloakThreshold > 1)) throw new Error(`Invalid health-cloak threshold for ${c.id}`);
    if (c.healthCloakThreshold > 0 && (c.spell || c.building || !(c.invisibilityDuration > 0))) throw new Error(`Health cloak requires a troop and positive duration for ${c.id}`);
    if (!Number.isInteger(c.count) || c.count < 1) throw new Error(`Entity count must be a positive integer for ${c.id}`);
    if (!c.spell && c.attackInterval <= 0) throw new Error(`Attack interval must be positive for ${c.id}`);
    if (!c.spell && !(c.collisionRadius > 0)) throw new Error(`Independent collision radius missing for ${c.id}`);
    if (c.auraDamage > 0 && !(c.auraInterval > 0)) throw new Error(`Aura interval missing for ${c.id}`);
    if (c.shieldHP > 0 && typeof c.shieldOverflowProtected !== "boolean") throw new Error(`Shield overflow rule missing for ${c.id}`);
    for (const key of ["hardExpiry", "shieldOverflowProtected", "globalFriendlyInvisibility", "breakExistingLocks"]) if (c[key] !== undefined && typeof c[key] !== "boolean") throw new Error(`Invalid ${c.id}.${key} flag`);
    if (c.deathSpawnId !== undefined && typeof c.deathSpawnId !== "string") throw new Error(`Invalid child identifier for ${c.id}`);
    if (c.deathSpawnCount !== undefined && !Number.isInteger(c.deathSpawnCount)) throw new Error(`Fractional child count for ${c.id}`);
    for (const [k, v] of Object.entries(c)) if (typeof v === "number" && !Number.isFinite(v)) throw new Error(`Non-finite ${c.id}.${k}`);
    if ((c.dotDamage > 0 || c.dotStructureDamage > 0) && !(c.dotInterval > 0)) throw new Error(`DOT interval missing for ${c.id}`);
    if (c.dotDuration > 0 && c.dotInterval > 0 && !close(c.dotDuration / c.dotInterval, Math.round(c.dotDuration / c.dotInterval))) throw new Error(`DOT duration must contain an integral tick count for ${c.id}`);
  }
  for (const c of m.cards) if (c.deathSpawnCount > 0 && !m.entities.some(e => e.id === c.deathSpawnId)) throw new Error(`Child definition missing for ${c.id}`);
  for (const c of m.entities) if (!m.cards.some(p => p.deathSpawnId === c.id && p.deathSpawnCount > 0)) throw new Error(`Subordinate ${c.id} has no playable parent`);
}

export function selfTest() {
  const checks = [];
  const check = (label, passed) => { checks.push({ label, passed }); if (!passed) throw new Error(`Fixture failed: ${label}`); };
  check("840 to 822 HP", close(numericDelta(840, 822).relative, -18 / 840));
  check("96 to 93 damage", close(numericDelta(96, 93).relative, -3 / 96));
  check("6.5 to 6 range", close(numericDelta(6.5, 6).relative, -.5 / 6.5));
  check("Interval reduction differs from frequency increase", close(numericDelta(1.1, .9).relative, -2 / 11) && close(numericDelta(1 / 1.1, 1 / .9).relative, 2 / 9));
  check("DPS combines damage and interval", close(numericDelta(96 / 1.1, 93 / .9).relative, (93 / .9) / (96 / 1.1) - 1));
  check("Zero old value is ADDED without percentage", numericDelta(0, 310).status === "ADDED" && numericDelta(0, 310).relative === null);
  check("Removed value", numericDelta(10, 0).status === "REMOVED" && numericDelta(10, 0).relative === -1);
  check("Five-unit group totals", 111 * 5 === 555 && close(20 / .9 * 5, 111.11111111111111));
  check("Children remain separate", 50 * 4 === 200 && 36 / .75 * 4 === 192);
  check("Shield/body split", 1000 + 310 === 1310 && 1000 !== 1310);
  check("Target mask explicit ground only", targetLabel(1) === "Ground troops");
  check("Target mask explicit structures", targetLabel(4) === "structures");
  check("Wave travel", 4 / 8 === .5);
  check("Approved DOT exposure", 262 + 40 * 5 === 462 && 143 + 15 * 5 === 218);
  check("Only absent historical health trigger defaults to zero", baselineProjection({ id: "fixture", hp: 445 }, 3).healthCloakThreshold === 0 && baselineProjection({ id: "fixture", hp: 445 }, 3).shieldHP === undefined);
  check("HP trigger derives from maximum body HP", 445 * .5 === 222.5);
  const cloakFixture = { schema: 3, status: "fixture", version: "Fixture3", balanceHash: "a".repeat(64), cards: [{ id: "fixture", name: "Fixture", count: 1, hp: 445, damage: 95, attackInterval: 1.05, moveSpeed: 1.85, range: 1.4, targetMask: 7, collisionRadius: .484, healthCloakThreshold: .5, invisibilityDuration: 1 }], entities: [] };
  validateManifest(cloakFixture, true); check("Valid schema 3 health cloak", true);
  for (const [label, value] of [["Reject threshold above maximum HP", 1.01], ["Reject missing schema 3 threshold", undefined]]) {
    const invalid = structuredClone(cloakFixture); invalid.cards[0].healthCloakThreshold = value;
    let rejected = false; try { validateManifest(invalid, true); } catch { rejected = true; } check(label, rejected);
  }
  return { kind: "synthetic arithmetic fixtures", passed: true, checks };
}

async function build(args) {
  const manifestBytes = await fs.readFile(args.manifest), baselineBytes = await fs.readFile(args.baseline);
  const manifest = JSON.parse(manifestBytes.toString("utf8").replace(/^\uFEFF/, ""));
  const capturedBaseline = manifestRecords(JSON.parse(baselineBytes.toString("utf8").replace(/^\uFEFF/, "")));
  validateManifest(manifest, args.fixture);
  const baseline = { ...capturedBaseline, cards: capturedBaseline.cards.map(c => baselineProjection(c, manifest.schema)), entities: (capturedBaseline.entities ?? []).map(c => baselineProjection(c, manifest.schema)) };
  if (!Array.isArray(baseline.cards) || new Set(baseline.cards.map(c => c.id)).size !== baseline.cards.length) throw new Error("Expected unique PRE-PATCH captured card definitions");
  if (baseline.cards.some(c => !manifest.cards.some(p => p.id === c.id))) throw new Error("The post-patch roster omits an existing playable card");
  const output = path.resolve(args.output), qaDir = path.resolve(args.qaDirectory);
  if (!args.force) { try { await fs.stat(output); throw new Error(`Output already exists: ${output}`); } catch (e) { if (e.code !== "ENOENT") throw e; } }
  await fs.mkdir(qaDir, { recursive: true });
  const runtimeRequire = createRequire(path.join(path.resolve(args.runtimeDirectory), "package.json"));
  const { Workbook, SpreadsheetFile } = await import(pathToFileURL(runtimeRequire.resolve("@oai/artifact-tool")).href);
  const workbook = Workbook.create();
  const sheets = Object.fromEntries(["Cards", "Abilities", "Spells", "Summons", "Changes", "Source"].map(name => [name, workbook.worksheets.add(name)]));
  const pins = { manifestSHA256: sha(manifestBytes), baselineSHA256: sha(baselineBytes), balanceHash: manifest.balanceHash, version: manifest.version };
  const source = sheets.Source;
  source.getRange("A2").values = [["Balance reference"]];
  source.getRange("D2:E7").values = [["Patch version", manifest.version], ["Balance manifest SHA256", pins.balanceHash], ["Manifest file SHA256", pins.manifestSHA256], ["Pre-patch file SHA256", pins.baselineSHA256], ["Source", path.basename(args.manifest)], ["Pre-patch source", path.basename(args.baseline)]];
  if (manifest.schema >= 3 && [...capturedBaseline.cards, ...(capturedBaseline.entities ?? [])].some(c => c.healthCloakThreshold === undefined)) source.getRange("D8:E8").values = [["Historical schema default", "Absent healthCloakThreshold = 0 (no HP trigger); other inputs are unchanged."]];
  source.getRange("A9:F9").values = [["Phase", "Record", "Name", "Field", "Value", "Unit"]];
  const sourceRows = [], index = new Map();
  function append(phase, record, name, field, value) {
    if (["string", "number", "boolean"].includes(typeof value) || value === null) {
      const row = 10 + sourceRows.length;
      index.set(`${phase}|${record}|${field}`, `E${row}`);
      sourceRows.push([phase, record, name, field, value ?? "n.a.", FIELD_UNITS[field] ?? ""]);
    }
  }
  for (const [phase, m] of [["Pre", baseline], ["Post", manifest]]) {
    for (const c of [...m.cards, ...(m.entities ?? [])]) for (const [key, value] of Object.entries(c)) append(phase, c.id, c.name, key, value);
    for (const [key, value] of Object.entries(m.rules ?? {})) append(phase, "@rules", "Shared rules", key, value);
  }
  if (sourceRows.length) source.getRange(`A10:F${9 + sourceRows.length}`).values = sourceRows;
  const sourceCell = (phase, id, field) => { const address = index.get(`${phase}|${id}|${field}`); return address ? cell("Source", address) : null; };
  const link = (id, field, phase = "Post") => { const address = sourceCell(phase, id, field); return address ? `=${address}` : null; };
  const formulaChecks = [];
  function setFormula(sheet, address, formula, expected, label) {
    sheets[sheet].getRange(address).formulas = [[formula]];
    formulaChecks.push({ sheet, cell: address, formula, expected, label });
  }
  function setLink(sheet, address, id, field, phase = "Post") {
    const formula = link(id, field, phase);
    if (formula) setFormula(sheet, address, formula, (phase === "Post" ? [...manifest.cards, ...manifest.entities] : [...baseline.cards, ...(baseline.entities ?? [])]).find(c => c.id === id)?.[field], `${id}.${field}`);
    else sheets[sheet].getRange(address).values = [["n.a."]];
  }
  function header(name, title, headers) {
    const sheet = sheets[name];
    sheet.getRange("A2").values = [[title]];
    sheet.getRange("A3").values = [["Patch version"]]; sheet.getRange("B3").formulas = [[ref("Source", "E2")]];
    sheet.getRange(`A5:${col(headers.length)}5`).values = [headers];
  }
  const cardHeaders = ["Card ID", "Card", "Type", "Cost", "Units", "Body HP each", "Shield HP each", "Initial durability each", "Damage each", "Interval (s)", "Basic DPS each", "Group body HP", "Group initial durability", "Group basic DPS", "Move (tiles/s)", "Range (tiles)", "Projectile (tiles/s)", "Splash (tiles)", "Model scale", "Attack targets", "Movement", "Collision radius", "Lifetime (s)", "Footprint (tiles)", "Decay (HP/s)", "First cooldown (s)"];
  cardHeaders.push("Front sight (tiles)", "Rear sight (tiles)");
  header("Cards", "Card stats", cardHeaders);
  const cardRow = new Map();
  manifest.cards.forEach((c, i) => {
    const r = i + 6; cardRow.set(c.id, r);
    sheets.Cards.getRange(`A${r}:AB${r}`).values = [[c.id, c.name, c.spell ? "Spell" : c.building ? "Building" : "Troop", null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, targetLabel(c.targetMask), c.spell ? "Spell" : c.building ? "Stationary" : c.flying ? "Flying" : "Ground", null, null, null, null, null, null, null]];
    for (const [column, field] of [["AA", "frontSight"], ["AB", "rearSight"]]) {
      const sourceAddress = sourceCell("Post", "@rules", field);
      if (c.spell || !sourceAddress) sheets.Cards.getRange(`${column}${r}`).values = [["n.a."]];
      else setFormula("Cards", `${column}${r}`, `=${sourceAddress}`, manifest.rules[field], `${c.id} ${field}`);
    }
    for (const [field, column] of Object.entries({ cost: "D", count: "E", hp: "F", shieldHP: "G", damage: "I", attackInterval: "J", moveSpeed: "O", range: "P", projectileSpeed: "Q", splash: "R", scale: "S", collisionRadius: "V", lifetime: "W", footprint: "X", hpDecayPerSecond: "Y", initialAttackCooldown: "Z" })) {
      if (c.spell && ["count", "hp", "shieldHP", "attackInterval", "moveSpeed", "range", "collisionRadius", "lifetime", "footprint", "hpDecayPerSecond", "initialAttackCooldown"].includes(field)) sheets.Cards.getRange(`${column}${r}`).values = [["n.a."]];
      else setLink("Cards", `${column}${r}`, c.id, field);
    }
    if (c.spell) for (const column of ["H", "K", "L", "M", "N"]) sheets.Cards.getRange(`${column}${r}`).values = [["n.a."]];
    else {
      // A missing optional shield is explicitly inapplicable, not an invented shield.
      setFormula("Cards", `H${r}`, c.shieldHP === undefined ? `=F${r}` : `=F${r}+G${r}`, c.hp + (c.shieldHP ?? 0), `${c.id} initial durability`);
      setFormula("Cards", `K${r}`, `=I${r}/J${r}`, c.damage / c.attackInterval, `${c.id} basic DPS`);
      setFormula("Cards", `L${r}`, `=E${r}*F${r}`, c.count * c.hp, `${c.id} group body HP`);
      setFormula("Cards", `M${r}`, `=E${r}*H${r}`, c.count * (c.hp + (c.shieldHP ?? 0)), `${c.id} group initial durability`);
      setFormula("Cards", `N${r}`, `=E${r}*K${r}`, c.count * c.damage / c.attackInterval, `${c.id} group basic DPS`);
    }
  });
  header("Abilities", "Abilities", ["Card ID", "Card", "Ability", "Metric", "Value", "Unit", "Behavior"]);
  let ar = 6;
  const abilityRows = [];
  function ability(c, kind, field, description, behavior = "", formula = null, expected = null, unit = null) {
    const r = ar++; sheets.Abilities.getRange(`A${r}:G${r}`).values = [[c.id, c.name, kind, description, null, unit ?? FIELD_UNITS[field] ?? "", behavior]];
    if (formula) setFormula("Abilities", `E${r}`, formula, expected, `${c.id} ${description}`);
    else if (typeof c[field] === "boolean") setFormula("Abilities", `E${r}`, `=IF(${sourceCell("Post", c.id, field)},"Yes","No")`, c[field] ? "Yes" : "No", `${c.id} ${description}`);
    else setLink("Abilities", `E${r}`, c.id, field);
    abilityRows.push({ card: c.id, kind, field, row: r });
    if (["slowPct", "healthCloakThreshold"].includes(field)) sheets.Abilities.getRange(`E${r}`).setNumberFormat("0.00%");
    else if (typeof c[field] === "number" || typeof expected === "number") sheets.Abilities.getRange(`E${r}`).setNumberFormat(Number.isInteger(c[field] ?? expected) ? "#,##0" : "0.00");
    if (["slowPct", "healthCloakThreshold"].includes(field)) sheets.Abilities.getRange(`F${r}`).values = [["%"]];
    return r;
  }
  for (const c of manifest.cards) {
    const cr = cardRow.get(c.id);
    if (c.chargeDamage > 0) {
      ability(c, "Charge", "chargeDamage", "Total charged-hit damage", "Replaces the basic hit; it is not additive damage.");
      ability(c, "Charge", "chargeMoveSeconds", "Accumulated movement threshold", "Requires more than the listed movement time.");
      ability(c, "Charge", "chargedMoveSpeed", "Charged movement speed");
    }
    if (c.slowPct > 0) { ability(c, "Movement slow", "slowPct", "Slow strength", "Movement only; refresh without stacking. Stronger effects retain their own expiry."); ability(c, "Movement slow", "slowDuration", "Slow duration"); }
    if (c.auraDamage > 0) {
      const d = ability(c, "Aura", "auraDamage", "Damage per pulse", "Separate from the basic structure attack; enemy ground/air troops only.");
      const t = ability(c, "Aura", "auraInterval", "Pulse interval", "First pulse after one interval from spawn.");
      ability(c, "Aura", null, "Theoretical per-target aura DPS", "Assumes every pulse reaches an eligible target.", `=E${d}/E${t}`, c.auraDamage / c.auraInterval, "damage/s");
      ability(c, "Aura", "auraRadius", "Pulse radius"); ability(c, "Aura", "stunDuration", "Stun duration");
    }
    if (c.shockwaveSpeed > 0) {
      const speed = ability(c, "Shockwave", "shockwaveSpeed", "Wavefront speed", "Swept radial front; one hit per eligible target per wave.");
      ability(c, "Shockwave", "range", "Maximum wave radius", targetLabel(c.targetMask), ref("Cards", `P${cr}`), c.range);
      ability(c, "Shockwave", null, "Travel to outer edge", "Travel time is separate from the attack interval.", `=${cell("Cards", `P${cr}`)}/E${speed}`, c.range / c.shockwaveSpeed, "s");
    }
    if (c.shieldHP > 0) { ability(c, "Shield", "shieldHP", "Initial shield HP", "Body HP is separate from shield HP.", ref("Cards", `G${cr}`), c.shieldHP); ability(c, "Shield", "shieldOverflowProtected", "Overflow protection", "A shield-breaking damage event does not overflow into body HP when enabled."); }
    if (c.deathSpawnCount > 0) { ability(c, "Death summons", "deathSpawnCount", "Child count", "Authoritative death event; no extra Aether payment or card cast."); ability(c, "Death summons", "deathSpawnId", "Child definition", "Children are subordinate entities, not deck cards."); }
    if (c.building) { ability(c, "Building", "hpDecayPerSecond", "HP decay per second"); ability(c, "Building", "lifetime", "Maximum lifetime", "Passive HP loss and the explicit expiry rule are separate."); if (c.hardExpiry !== undefined) ability(c, "Building", "hardExpiry", "Expiry at maximum lifetime", "When enabled, the building expires at maximum lifetime even if body HP remains."); ability(c, "Building", "initialAttackCooldown", "Initial attack cooldown"); }
    if (c.invisibilityDuration > 0) {
      const kind = c.healthCloakThreshold > 0 ? "Health-triggered cloak" : "Invisibility";
      if (c.healthCloakThreshold > 0) {
        const threshold = ability(c, kind, "healthCloakThreshold", "Maximum HP threshold", "Once per deployment, after surviving a damage packet at or below this fraction of maximum body HP.");
        ability(c, kind, null, "Trigger body HP", "Derived from maximum body HP; shield HP is excluded.", `=${cell("Cards", `F${cr}`)}*E${threshold}`, c.hp * c.healthCloakThreshold, "HP");
        ability(c, kind, null, "Scope", "This troop only.", '=\"Self\"', "Self", "");
      } else ability(c, kind, "globalFriendlyInvisibility", "Global friendly scope", "No means friendly troops inside the spell area at impact.");
      ability(c, kind, "invisibilityDuration", "Duration", "Simulation time; no damage immunity.");
      ability(c, kind, "breakExistingLocks", "Break existing target locks", "Enemy acquisition is prevented while cloaked. Committed hits, in-flight projectiles and area spells still resolve.");
    }
  }
  header("Spells", "Spell impact and exposure", ["Card ID", "Spell", "Cost", "Delay (s)", "Radius (tiles)", "Troop impact", "Structure impact", "Troop tick", "Structure tick", "Tick interval (s)", "DOT duration (s)", "Ticks", "Full troop exposure", "Full structure exposure", "Visual rounds", "Targets"]);
  const spells = manifest.cards.filter(c => c.spell);
  spells.forEach((c, i) => {
    const r = i + 6; sheets.Spells.getRange(`A${r}:P${r}`).values = [[c.id, c.name, null, null, null, null, null, null, null, null, null, null, null, null, null, targetLabel(c.targetMask)]];
    for (const [field, column] of Object.entries({ cost: "C", castDelay: "D", spellRadius: "E", damage: "F", towerDamage: "G", dotDamage: "H", dotStructureDamage: "I", dotInterval: "J", dotDuration: "K", rounds: "O" })) setLink("Spells", `${column}${r}`, c.id, field);
    const ticks = c.dotDuration > 0 ? c.dotDuration / c.dotInterval : 0;
    setFormula("Spells", `L${r}`, `=IF(K${r}=0,0,K${r}/J${r})`, ticks, `${c.id} tick count`);
    setFormula("Spells", `M${r}`, `=F${r}+H${r}*L${r}`, c.damage + (c.dotDamage ?? 0) * ticks, `${c.id} full troop exposure`);
    setFormula("Spells", `N${r}`, `=G${r}+I${r}*L${r}`, c.towerDamage + (c.dotStructureDamage ?? 0) * ticks, `${c.id} full structure exposure`);
  });
  header("Summons", "Subordinate unit stats", ["Parent ID", "Parent", "Entity ID", "Entity", "Children", "HP each", "Damage each", "Interval (s)", "Basic DPS each", "Group HP", "Group basic DPS", "Move (tiles/s)", "Range (tiles)", "Splash (tiles)", "Scale", "Targets", "Collision radius"]);
  const parents = manifest.cards.filter(c => c.deathSpawnCount > 0);
  parents.forEach((p, i) => {
    const e = manifest.entities.find(e => e.id === p.deathSpawnId), r = i + 6;
    sheets.Summons.getRange(`A${r}:Q${r}`).values = [[p.id, p.name, e.id, e.name, null, null, null, null, null, null, null, null, null, null, null, targetLabel(e.targetMask), null]];
    setLink("Summons", `E${r}`, p.id, "deathSpawnCount");
    for (const [field, column] of Object.entries({ hp: "F", damage: "G", attackInterval: "H", moveSpeed: "L", range: "M", splash: "N", scale: "O", collisionRadius: "Q" })) setLink("Summons", `${column}${r}`, e.id, field);
    setFormula("Summons", `I${r}`, `=G${r}/H${r}`, e.damage / e.attackInterval, `${e.id} basic DPS`);
    setFormula("Summons", `J${r}`, `=E${r}*F${r}`, p.deathSpawnCount * e.hp, `${e.id} child group HP`);
    setFormula("Summons", `K${r}`, `=E${r}*I${r}`, p.deathSpawnCount * e.damage / e.attackInterval, `${e.id} child group DPS`);
  });
  header("Changes", "Changed stats from the live baseline", ["Card ID", "Card", "Stat", "Before", "After", "Delta", "Relative change", "Percentage points", "Unit", "Change"]);
  let chr = 6; const changes = [];
  function change(c, key, title, before, after, oldFormula, newFormula, unit = FIELD_UNITS[key] ?? "", special = false) {
    const r = chr++; const delta = numericDelta(before, after);
    sheets.Changes.getRange(`A${r}:J${r}`).values = [[c.id, c.name, title, null, null, null, null, special ? null : "n.a.", unit, delta.status]];
    setFormula("Changes", `D${r}`, oldFormula, before, `${c.id} old ${title}`);
    setFormula("Changes", `E${r}`, newFormula, after, `${c.id} new ${title}`);
    setFormula("Changes", `F${r}`, `=E${r}-D${r}`, delta.delta, `${c.id} delta ${title}`);
    setFormula("Changes", `G${r}`, `=IF(D${r}=0,"n.a.",F${r}/D${r})`, delta.relative ?? "n.a.", `${c.id} relative ${title}`);
    if (special) setFormula("Changes", `H${r}`, `=F${r}*100`, delta.delta * 100, `${c.id} percentage points ${title}`);
    sheets.Changes.getRange(`D${r}:F${r}`).setNumberFormat(special ? "0.00%" : Number.isInteger(before) && Number.isInteger(after) ? "#,##0" : "0.00");
    if (special) sheets.Changes.getRange(`I${r}`).values = [["%"]];
    changes.push({ id: c.id, field: key, row: r, old: before, new: after });
  }
  for (const c of manifest.cards) {
    const old = baseline.cards.find(b => b.id === c.id);
    if (!old) continue;
    for (const key of Object.keys(STAT_NAMES)) if (!["targetMask", "chargedMoveSpeed"].includes(key) && typeof old[key] === "number" && typeof c[key] === "number" && !close(old[key], c[key])) change(c, key, STAT_NAMES[key], old[key], c[key], link(c.id, key, "Pre"), link(c.id, key), FIELD_UNITS[key], ["slowPct", "healthCloakThreshold"].includes(key));
    if (!c.spell && !old.spell) {
      if (old.chargeDamage > 0 && c.chargeDamage > 0 && old.chargedMoveSpeed !== undefined && c.chargedMoveSpeed !== undefined) {
        const oldSpeed = old.chargedMoveSpeed || old.moveSpeed, newSpeed = c.chargedMoveSpeed || c.moveSpeed;
        if (!close(oldSpeed, newSpeed)) change(c, "effectiveChargedMoveSpeed", "Charged movement speed", oldSpeed, newSpeed,
          `=IF(${sourceCell("Pre", c.id, "chargedMoveSpeed")}=0,${sourceCell("Pre", c.id, "moveSpeed")},${sourceCell("Pre", c.id, "chargedMoveSpeed")})`,
          `=IF(${sourceCell("Post", c.id, "chargedMoveSpeed")}=0,${sourceCell("Post", c.id, "moveSpeed")},${sourceCell("Post", c.id, "chargedMoveSpeed")})`, "tiles/s");
      }
      const oldDps = old.damage / old.attackInterval, newDps = c.damage / c.attackInterval;
      if (!close(oldDps, newDps)) change(c, "derivedDPS", "Basic DPS each", oldDps, newDps, `=${sourceCell("Pre", c.id, "damage")}/${sourceCell("Pre", c.id, "attackInterval")}`, ref("Cards", `K${cardRow.get(c.id)}`), "damage/s");
      if (!close(old.attackInterval, c.attackInterval)) change(c, "attackFrequency", "Attack frequency", 1 / old.attackInterval, 1 / c.attackInterval, `=1/${sourceCell("Pre", c.id, "attackInterval")}`, `=1/${sourceCell("Post", c.id, "attackInterval")}`, "attacks/s");
      if (!close(old.hp * old.count, c.hp * c.count)) change(c, "groupBodyHP", "Group body HP", old.hp * old.count, c.hp * c.count, `=${sourceCell("Pre", c.id, "hp")}*${sourceCell("Pre", c.id, "count")}`, ref("Cards", `L${cardRow.get(c.id)}`), "HP");
      if (!close(oldDps * old.count, newDps * c.count)) change(c, "groupDPS", "Group basic DPS", oldDps * old.count, newDps * c.count, `=${sourceCell("Pre", c.id, "damage")}/${sourceCell("Pre", c.id, "attackInterval")}*${sourceCell("Pre", c.id, "count")}`, ref("Cards", `N${cardRow.get(c.id)}`), "damage/s");
    }
    for (const key of LABEL_FIELDS) if (old[key] !== undefined && c[key] !== undefined && old[key] !== c[key]) {
      const r = chr++; sheets.Changes.getRange(`A${r}:J${r}`).values = [[c.id, c.name, MECHANIC_NAMES[key], mechanicValue(key, old[key]), mechanicValue(key, c[key]), "n.a.", "n.a.", "n.a.", "mechanic", "CHANGED"]];
      changes.push({ id: c.id, field: key, row: r, old: old[key], new: c[key] });
    }
  }
  if (manifest.schema >= 3) for (const [field, title] of Object.entries(ARENA_STATS)) {
    const before = baseline.rules?.[field], after = manifest.rules?.[field];
    if (typeof before === "number" && typeof after === "number" && !close(before, after)) change({ id: "@rules", name: "Shared rules" }, field, title, before, after, link("@rules", field, "Pre"), link("@rules", field), "tiles");
  }
  const newCards = manifest.cards.filter(c => !baseline.cards.some(b => b.id === c.id));
  if (!changes.length) sheets.Changes.getRange("A6").values = [["No existing-card stat changes."]];
  const newStart = Math.max(chr + 2, 9);
  sheets.Changes.getRange(`A${newStart}:D${newStart}`).values = [["New card ID", "New card", "Cost", "Type"]];
  newCards.forEach((c, i) => { const r = newStart + i + 1; sheets.Changes.getRange(`A${r}:D${r}`).values = [[c.id, c.name, null, c.spell ? "Spell" : c.building ? "Building" : "Troop"]]; setFormula("Changes", `C${r}`, ref("Cards", `D${cardRow.get(c.id)}`), c.cost, `${c.id} new card cost`); });
  if (!newCards.length) sheets.Changes.getRange(`A${newStart + 1}`).values = [["No new playable cards."]];

  const lastRows = { Cards: manifest.cards.length + 5, Abilities: Math.max(6, ar - 1), Spells: Math.max(6, spells.length + 5), Summons: Math.max(6, parents.length + 5), Changes: Math.max(newStart + Math.max(1, newCards.length), chr - 1), Source: sourceRows.length + 9 };
  const widths = { Cards: [21, 22, 12, 9, 9, 12, 12, 15, 12, 12, 13, 13, 16, 14, 14, 14, 15, 13, 12, 39, 13, 14, 13, 15, 14, 16], Abilities: [21, 22, 20, 34, 24, 28, 88], Spells: [21, 22, 9, 12, 13, 14, 16, 13, 15, 15, 15, 9, 17, 19, 14, 39], Summons: [21, 22, 28, 24, 10, 11, 12, 12, 13, 12, 15, 14, 13, 13, 10, 39, 15], Changes: [21, 22, 34, 16, 16, 16, 18, 19, 28, 12], Source: [10, 28, 24, 31, 72, 48] };
  widths.Cards.push(15, 15);
  for (const [name, sheet] of Object.entries(sheets)) {
    const endCol = col(widths[name].length), last = lastRows[name];
    sheet.showGridLines = false;
    sheet.getRange(`A1:${endCol}${last}`).format.font = { name: "Arial", size: 11, color: "#222B33" };
    sheet.getRange(`A1:${endCol}${last}`).format.verticalAlignment = "center";
    sheet.getRange(`A1:${endCol}${last}`).format.rowHeight = 23;
    widths[name].forEach((width, i) => { sheet.getRange(`${col(i + 1)}1:${col(i + 1)}${last}`).format.columnWidth = width; });
    const hr = name === "Source" ? 9 : 5;
    sheet.getRange(`A${hr}:${endCol}${hr}`).format = { fill: "#273D4B", font: { name: "Arial", size: 11, bold: true, color: "#FFFFFF" }, wrapText: true, rowHeight: 39, horizontalAlignment: "center", verticalAlignment: "center" };
    sheet.getRange("A2").format.font = { name: "Arial", size: 16, bold: true, color: "#172F40" };
    sheet.getRange(`A${hr + 1}:${endCol}${last}`).format.borders = { insideHorizontal: { style: "thin", color: "#E6E9EB" } };
    sheet.freezePanes.freezeRows(hr); sheet.freezePanes.freezeColumns(name === "Source" ? 3 : 2);
    if (last > hr) { const table = sheet.tables.add(`A${hr}:${endCol}${name === "Changes" ? Math.max(6, chr - 1) : last}`, true, `Balance${name}`); table.style = "TableStyleLight1"; }
    if (name !== "Source") sheet.tabColor = name === "Cards" ? "#273D4B" : "#6D8798";
  }
  sheets.Source.getRange(`E10:E${lastRows.Source}`).format.horizontalAlignment = "right";
  for (let i = 0; i < sourceRows.length; i++) {
    const value = sourceRows[i][4], r = i + 10;
    if (typeof value !== "string") continue;
    sheets.Source.getRange(`E${r}`).format.horizontalAlignment = "left";
    sheets.Source.getRange(`E${r}`).format.wrapText = true;
    if (value.length > 70) sheets.Source.getRange(`A${r}:F${r}`).format.rowHeight = 23 * Math.ceil(value.length / 70);
  }
  sheets.Source.getRange("A3").format.font = { name: "Arial", size: 11, color: "#222B33" };
  sheets.Source.getRange("A2:F7").format.rowHeight = 25;
  // Metadata uses the same wide value column as the source table; no merged
  // input cells or clipped SHA strings are needed.
  sheets.Source.getRange(`F10:F${lastRows.Source}`).format.horizontalAlignment = "center";
  sheets.Abilities.getRange(`F6:F${lastRows.Abilities}`).format.horizontalAlignment = "center";
  sheets.Abilities.getRange(`G6:G${lastRows.Abilities}`).format.wrapText = true;
  for (const { row } of abilityRows) {
    const behavior = sheets.Abilities.getRange(`G${row}`).values[0][0];
    if (typeof behavior === "string" && behavior.length > 85) sheets.Abilities.getRange(`A${row}:G${row}`).format.rowHeight = 23 * Math.ceil(behavior.length / 85);
  }
  for (const [name, columns] of Object.entries({ Cards: ["D", "E", "F", "G", "H", "I", "L", "M"], Spells: ["C", "F", "G", "H", "I", "L", "M", "N", "O"], Summons: ["E", "F", "G", "J"] })) for (const c of columns) sheets[name].getRange(`${c}6:${c}${lastRows[name]}`).setNumberFormat("#,##0");
  for (const [name, columns] of Object.entries({ Cards: ["J", "K", "N", "O", "P", "Q", "R", "S", "V", "W", "X", "Y", "Z"], Spells: ["D", "E", "J", "K"], Summons: ["H", "I", "K", "L", "M", "N", "O", "Q"] })) for (const c of columns) sheets[name].getRange(`${c}6:${c}${lastRows[name]}`).setNumberFormat("0.00");
  sheets.Changes.getRange(`G6:G${Math.max(6, chr - 1)}`).setNumberFormat("0.00%");
  sheets.Changes.getRange(`H6:H${Math.max(6, chr - 1)}`).setNumberFormat("0.00");
  sheets.Changes.getRange(`D6:F${Math.max(6, chr - 1)}`).format.horizontalAlignment = "right";
  for (const [name, ranges] of Object.entries({ Cards: ["D6:S", "V6:AB"], Spells: ["C6:O"], Summons: ["E6:O", "Q6:Q"], Changes: ["D6:H"], Abilities: ["E6:E"] })) for (const range of ranges) sheets[name].getRange(`${range}${lastRows[name]}`).format.horizontalAlignment = "right";
  if (!spells.length) sheets.Spells.getRange("A6").values = [["No playable spells."]];
  if (!parents.length) sheets.Summons.getRange("A6").values = [["No subordinate death-spawn units."]];
  if (!abilityRows.length) sheets.Abilities.getRange("A6").values = [["No special abilities."]];
  workbook.recalculate();
  const mismatches = [];
  for (const q of formulaChecks) {
    const actual = sheets[q.sheet].getRange(q.cell).values[0][0];
    if (typeof q.expected === "number" ? typeof actual !== "number" || !close(actual, q.expected) : actual !== q.expected) mismatches.push({ ...q, actual });
  }
  if (mismatches.length) throw new Error(`Recalculation parity failed: ${JSON.stringify(mismatches.slice(0, 12))}`);
  for (const name of Object.keys(sheets)) {
    const inspection = await workbook.inspect({ kind: "table", range: `${name}!A${name === "Source" ? 9 : 5}:${col(Math.min(14, widths[name].length))}${Math.min(lastRows[name], 15)}`, include: "values,formulas", tableMaxRows: 12, tableMaxCols: 14, maxChars: 12000 });
    await fs.writeFile(path.join(qaDir, `${name.toLowerCase()}-inspection.ndjson`), inspection.ndjson);
  }
  const scan = await workbook.inspect({ kind: "match", searchTerm: ERROR_RE, options: { useRegex: true, maxResults: 300 }, summary: "Balance workbook formula scan", maxChars: 12000 });
  await fs.writeFile(path.join(qaDir, "formula-scan.ndjson"), scan.ndjson);
  if (new RegExp(ERROR_RE).test(scan.ndjson)) throw new Error("Formula errors found; see formula-scan.ndjson");
  // Verify live dependencies with a temporary input change, then restore it.
  const probe = manifest.cards.find(c => !c.spell);
  const sourceDamage = source.getRange(index.get(`Post|${probe.id}|damage`));
  const originalDamage = sourceDamage.values[0][0], probeRow = cardRow.get(probe.id);
  sourceDamage.values = [[originalDamage + 7]]; workbook.recalculate();
  const probeValue = sheets.Cards.getRange(`K${probeRow}`).values[0][0];
  if (!close(probeValue, (originalDamage + 7) / probe.attackInterval)) throw new Error("Input-change recalculation failed");
  if (!close(sheets.Cards.getRange(`N${probeRow}`).values[0][0], (originalDamage + 7) / probe.attackInterval * probe.count)) throw new Error("Group DPS recalculation failed");
  sourceDamage.values = [[originalDamage]]; workbook.recalculate();
  if (!close(sheets.Cards.getRange(`K${probeRow}`).values[0][0], originalDamage / probe.attackInterval)) throw new Error("Input restoration failed");
  const sourceCount = source.getRange(index.get(`Post|${probe.id}|count`)), originalCount = sourceCount.values[0][0];
  sourceCount.values = [[originalCount + 1]]; workbook.recalculate();
  if (!close(sheets.Cards.getRange(`L${probeRow}`).values[0][0], (originalCount + 1) * probe.hp) || !close(sheets.Cards.getRange(`N${probeRow}`).values[0][0], (originalCount + 1) * probe.damage / probe.attackInterval)) throw new Error("Swarm count recalculation failed");
  sourceCount.values = [[originalCount]];
  let healthCloakInputChangeRecalculationPassed = false;
  const healthProbe = manifest.cards.find(c => c.healthCloakThreshold > 0);
  if (healthProbe) {
    const input = source.getRange(index.get(`Post|${healthProbe.id}|healthCloakThreshold`)), original = input.values[0][0];
    const hpRow = abilityRows.find(r => r.card === healthProbe.id && r.kind === "Health-triggered cloak" && r.field === null && sheets.Abilities.getRange(`D${r.row}`).values[0][0] === "Trigger body HP").row;
    input.values = [[original / 2]]; workbook.recalculate();
    if (!close(sheets.Abilities.getRange(`E${hpRow}`).values[0][0], healthProbe.hp * original / 2)) throw new Error("Health-cloak input-change recalculation failed");
    input.values = [[original]]; workbook.recalculate();
    if (!close(sheets.Abilities.getRange(`E${hpRow}`).values[0][0], healthProbe.hp * original)) throw new Error("Health-cloak restoration failed");
    healthCloakInputChangeRecalculationPassed = true;
  }
  const dotProbe = spells.find(c => c.dotDuration > 0);
  if (dotProbe) {
    const duration = source.getRange(index.get(`Post|${dotProbe.id}|dotDuration`)), originalDuration = duration.values[0][0], sr = spells.indexOf(dotProbe) + 6;
    duration.values = [[originalDuration + dotProbe.dotInterval]]; workbook.recalculate();
    if (!close(sheets.Spells.getRange(`L${sr}`).values[0][0], originalDuration / dotProbe.dotInterval + 1) || !close(sheets.Spells.getRange(`M${sr}`).values[0][0], dotProbe.damage + dotProbe.dotDamage * (originalDuration / dotProbe.dotInterval + 1))) throw new Error("DOT exposure recalculation failed");
    duration.values = [[originalDuration]];
  }
  workbook.recalculate();
  const review = [];
  for (const [name, last] of Object.entries(lastRows)) {
    const ranges = name === "Cards" ? [`A1:N${Math.min(last, 15)}`, `O5:AB${Math.min(last, 15)}`] : name === "Spells" ? [`A1:H${Math.min(last, 15)}`, `I5:P${Math.min(last, 15)}`] : name === "Summons" ? [`A1:I${Math.min(last, 15)}`, `J5:Q${Math.min(last, 15)}`] : [`A1:${col(widths[name].length)}${Math.min(last, 18)}`];
    if (name === "Cards" && last > 15) ranges.push(`A16:N${last}`, `O16:AB${last}`);
    if (["Abilities", "Changes", "Source"].includes(name) && last > 18) ranges.push(`A${Math.max(6, last - 13)}:${col(widths[name].length)}${last}`);
    for (let i = 0; i < ranges.length; i++) {
      const preview = await workbook.render({ sheetName: name, range: ranges[i], scale: 1.5, format: "png" });
      const filename = `${name.toLowerCase()}-${i + 1}.png`;
      await fs.writeFile(path.join(qaDir, filename), new Uint8Array(await preview.arrayBuffer()));
      review.push({ sheet: name, range: ranges[i], preview: filename });
    }
  }
  const xlsx = await SpreadsheetFile.exportXlsx(workbook);
  await fs.mkdir(path.dirname(output), { recursive: true }); await xlsx.save(output);
  const outputBytes = await fs.readFile(output);
  const proof = { schemaVersion: 1, fixture: !!args.fixture, status: "awaiting-visual-review", ...pins, output: path.basename(output), outputSHA256: sha(outputBytes), outputBytes: outputBytes.length, cardCount: manifest.cards.length, entityCount: manifest.entities.length, newCardIds: newCards.map(c => c.id), sheets: Object.keys(sheets), formulaChecks: formulaChecks.length, formulaParityPassed: true, inputChangeRecalculationPassed: true, healthCloakInputChangeRecalculationPassed, restoredInput: true, previews: review, changes };
  await fs.writeFile(path.join(qaDir, "workbook-verification.json"), JSON.stringify(proof, null, 2) + "\n");
  console.log(JSON.stringify({ status: proof.status, output, cardCount: proof.cardCount, formulaChecks: proof.formulaChecks, qaDirectory: qaDir }));
}

function parseArgs(argv) {
  const args = {}; const names = { "--manifest": "manifest", "--baseline": "baseline", "--output": "output", "--qa-directory": "qaDirectory", "--runtime-directory": "runtimeDirectory" };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--self-test") args.selfTest = true;
    else if (argv[i] === "--fixture") args.fixture = true;
    else if (argv[i] === "--force") args.force = true;
    else if (names[argv[i]] && argv[i + 1]) args[names[argv[i]]] = argv[++i];
    else throw new Error(`Unknown or incomplete argument: ${argv[i]}`);
  }
  if (!args.selfTest && Object.values(names).some(name => !args[name])) throw new Error("Required: --manifest --baseline --output --qa-directory --runtime-directory");
  return args;
}
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) {
  try { const args = parseArgs(process.argv.slice(2)); if (args.selfTest) console.log(JSON.stringify(selfTest(), null, 2)); else await build(args); }
  catch (e) { console.error(e.stack ?? e.message); process.exitCode = 1; }
}
