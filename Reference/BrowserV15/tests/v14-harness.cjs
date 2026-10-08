const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = process.env.RIFT_GAME_ROOT || process.env.RIFT_PROJECT_ROOT
  || (fs.existsSync(path.resolve(__dirname, '../src/game.js')) ? path.resolve(__dirname, '..') : path.resolve(__dirname, '../rift_crown_arena_v14'));
const source = fs.readFileSync(path.join(root, 'src/game.js'), 'utf8');
function range(from, to) {
  const start = source.indexOf(from), end = source.indexOf(to, start + from.length);
  assert.ok(start >= 0 && end > start, `Missing source block ${from}`);
  return source.slice(start, end);
}
function definition(name) {
  const match = new RegExp(`(?:async )?function ${name}\\(`).exec(source);
  assert.ok(match, `Missing function ${name}`);
  let depth = 0, quote = null;
  for (let i = source.indexOf('{', match.index); i < source.length; i++) {
    const ch = source[i];
    if (quote) { if (ch === '\\') i++; else if (ch === quote) quote = null; continue; }
    if (ch === '/' && source[i + 1] === '/') { i = source.indexOf('\n', i); if (i < 0) break; continue; }
    if (ch === '/' && source[i + 1] === '*') { i = source.indexOf('*/', i + 2) + 1; continue; }
    if (ch === '"' || ch === "'" || ch === '`') quote = ch;
    else if (ch === '{') depth++;
    else if (ch === '}' && --depth === 0) return source.slice(match.index, i + 1);
  }
  throw new Error(`Unclosed function ${name}`);
}

class Vector3 {
  constructor(x = 0, y = 0, z = 0) { this.set(x, y, z); }
  set(x, y, z) { Object.assign(this, { x, y, z }); return this; }
  setY(y) { this.y = y; return this; }
  clone() { return new Vector3(this.x, this.y, this.z); }
  copy(other) { return this.set(other.x, other.y, other.z); }
  add(other) { this.x += other.x; this.y += other.y; this.z += other.z; return this; }
  sub(other) { this.x -= other.x; this.y -= other.y; this.z -= other.z; return this; }
  multiplyScalar(n) { this.x *= n; this.y *= n; this.z *= n; return this; }
  addScaledVector(other, n) { this.x += other.x * n; this.y += other.y * n; this.z += other.z * n; return this; }
  lengthSq() { return this.x ** 2 + this.y ** 2 + this.z ** 2; }
  length() { return Math.sqrt(this.lengthSq()); }
  normalize() { return this.multiplyScalar(1 / (this.length() || 1)); }
  distanceToSquared(other) { return (this.x - other.x) ** 2 + (this.y - other.y) ** 2 + (this.z - other.z) ** 2; }
  distanceTo(other) { return Math.sqrt(this.distanceToSquared(other)); }
  lerpVectors(a, b, n) { return this.set(a.x + (b.x - a.x) * n, a.y + (b.y - a.y) * n, a.z + (b.z - a.z) * n); }
}
let nextGroupId = 0;
class Group {
  constructor() { this.id = ++nextGroupId; this.position = new Vector3(); this.rotation = { x: 0, y: 0, z: 0 }; this.children = []; this.userData = {}; this.visible = true; }
  add(...children) { this.children.push(...children); }
  remove(child) { this.children = this.children.filter(item => item !== child); }
  clear() { this.children.length = 0; }
}
const classList = () => ({ add() {}, remove() {}, toggle() {}, contains: () => true });
const element = () => ({ textContent: '', innerHTML: '', style: {}, value: '', options: [], classList: classList() });

function makeContext(extra = {}) {
  const events = [], storage = new Map();
  const context = vm.createContext({
    console, Math, Date, Object, Set, Map, Number, Array, AbortController, setTimeout: () => 0, clearTimeout() {}, setInterval() {},
    performance: { now: () => 1000 }, THREE: { Vector3, Group, MathUtils: { clamp: (n, min, max) => Math.min(max, Math.max(min, n)) } },
    document: { querySelectorAll: () => [], querySelector: () => element(), body: { classList: classList() } },
    scene: { add() {}, remove: object => events.push({ type: 'remove', object }) }, tmpV2: new Vector3(),
    units: [], towers: [], buildings: [], projectiles: [], effects: [], hazards: [], cameraShake: 0,
    ui: Object.fromEntries(['timer', 'phaseLabel', 'doubleAether', 'aether', 'aetherFill', 'playerCrowns', 'enemyCrowns', 'endTitle', 'endScore', 'end', 'devSpawnCard', 'devSpawnTeam', 'devArmSpawn', 'devAiToggle', 'devAiStyle', 'devTowerSelect', 'devTowerHp', 'devStatus', 'devAiReadout'].map(name => [name, element()])),
    localStorage: { getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key) },
    events, storage, placementPreview: { visible: false }, debugOverlayGroup: new Group(),
    sandbox: { speed: 1, spawnArmed: false, spawnTeam: 'player', spawnCard: 'ironclad', debug: {} },
    game: {
      started: true, running: true, elapsed: 10, time: 180, overtime: false, overtimeTime: 120, tiebreaker: false,
      tiebreakTime: 0, crowns: { player: 0, enemy: 0 }, aether: { player: 5, enemy: 5 }, aetherMultiplier: 1,
      aiThink: 0, ai: { style: 'beatdown', enabled: true, phase: 'bank', pushLane: 1, anchor: null, supportPlays: 0, planAge: 0,
        memory: { playerAetherEstimate: 5, playerCycle: [], playerPlays: [], recentSpend: 0, lastPunish: 0, lastSpellCycle: 0 } },
    },
    disposeObject() {}, renderHand() {}, resetSelectionUi() {}, recordMatchResult() {}, refreshPocketOverlays() {},
    toast: message => events.push({ type: 'toast', message }),
    ...extra,
  });
  const effects = ['createBurst', 'createTowerCollapse', 'createDamageNumber', 'createMuzzleFlash', 'createBowSnapFx', 'createElectricSpark', 'createStormPulse', 'createFrostHit', 'createFrostFootstep', 'createMoveDust', 'createTrailParticle', 'createFrostBreath', 'createSlashArc', 'createHitSpark', 'createImpactFlash', 'createDebrisBurst', 'createSmokePuff', 'createNovaImpact', 'createBulletBurstFx', 'createMeteorShardFx', 'createMeteorZone', 'createDeployEffect', 'createConstructionFx', 'createTowerChipBurst', 'updateUnitHud', 'updateTowerHud', 'updateBuildingHud'];
  for (const name of effects) if (!context[name]) context[name] = (...args) => events.push({ type: name, args });
  const functions = ['nonnegativeNumber', 'validateDeck', 'entityWorldPos', 'sightRangeFor', 'isWithinSight', 'findNearestVisibleTargetableUnit',
    'laneBridgeX', 'navCellFromWorld', 'navWorldFromCell', 'navKey', 'navHeuristic', 'navInsideArenaCell', 'navOnBridge', 'navCellBlockedByStructure', 'navWalkable', 'navGoalCellFor', 'navBridgeCongestionSnapshot', 'navBridgeCongestionAt', 'reconstructNavPath', 'buildGroundPath', 'pathDistanceFrom', 'navPathStillValid', 'clearBridgeCommit', 'updateBridgeCommit',
    'horizontalDistance', 'moveTowards', 'structureEntities', 'sideOfRiver', 'bridgePathDistanceFromPositions', 'navigableDistance', 'bestBridgeFor', 'findNearestTargetableUnit', 'findNearestUnit', 'chooseStructureTarget', 'opponentTeam', 'laneForX',
    'castNova', 'castBulletBurst', 'castMeteorShards', 'updateHazards', 'castSpell', 'triggerDeploymentPull', 'triggerBuildingPull', 'spawnCard',
    'coreShouldBeActive', 'activateCoreTower', 'updateProjectiles', 'formatTime', 'updateTimer', 'clearLiveProjectiles', 'beginTiebreaker', 'tiebreakResolveSimultaneous', 'updateTiebreaker', 'updateScore', 'finishMatch', 'updateAether',
    'battleDamageSource', 'replayEntity', 'recordCardPlay', 'captureBattleState', 'completeBattleRecording',
    'devTowerKey', 'setSandboxSpeed', 'devModifyAether', 'armDeveloperSpawn', 'devSpawnAt', 'applyDeveloperTowerHp', 'clearDeveloperField', 'setAiEnabled', 'clearDebugDynamic'];
  vm.runInContext([
    `(() => {${fs.readFileSync(path.join(root, 'src/combat-rules.js'), 'utf8').replace(/^import .*;\r?\n/gm, '').replaceAll('export ', '')}; Object.assign(globalThis, {refreshSlow, refreshStun, spellDamageFor, directionalSight});})()`,
    fs.readFileSync(path.join(root, 'src/deck-presets.js'), 'utf8').replace('export function', 'function'),
    range('const TEAM =', 'const COLORS ='), range('const CARD_LIBRARY =', 'const deckAnalyzer ='), range('const AI_STYLES =', 'const scene ='),
    'const presetStore = createPresetStore(CARD_LIBRARY, DEFAULT_DECK); globalThis.presetStore = presetStore;',
    range('class CombatEntity {', 'function coreShouldBeActive'), range('class Tower extends CombatEntity {', 'function entityWorldPos'),
    range('class DefensiveBuilding extends CombatEntity {', 'function laneBridgeX'), range('class Unit extends CombatEntity {', 'const units=[];'),
    range('class DeckState {', 'const game='),
    functions.map(definition).join('\n'),
    `Unit.prototype.buildVisual = function() {}; Tower.prototype.buildVisual = function() {}; DefensiveBuilding.prototype.buildVisual = function() {};
     globalThis.Unit = Unit; globalThis.Tower = Tower; globalThis.DefensiveBuilding = DefensiveBuilding; globalThis.DeckState = DeckState;
     globalThis.cards = CARD_LIBRARY; globalThis.defaultDeck = DEFAULT_DECK; globalThis.styles = AI_STYLES;`,
  ].join('\n'), context, { filename: path.join(root, 'src/game.js') });
  context.snapPointToTile = point => point.clone();
  context.validTeamPlacement = () => true;
  context.isPocketUnlocked = () => false;
  context.game.playerDeck = new context.DeckState(context.defaultDeck);
  context.game.enemyDeck = new context.DeckState(context.defaultDeck);
  context.fireProjectile = (attacker, target, damage, speed, splash) => events.push({ type: 'projectile', attacker, target, damage, speed, splash });
  return context;
}
function install(context, names) { vm.runInContext(names.map(definition).join('\n'), context); }
module.exports = { root, source, range, definition, makeContext, install, Vector3, Group, classList, element };
