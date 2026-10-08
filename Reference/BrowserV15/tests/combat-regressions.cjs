const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const sourcePath = process.env.RIFT_GAME_SOURCE || path.resolve(__dirname, '../src/game.js');
const source = fs.readFileSync(sourcePath, 'utf8');

function extract(from, to) {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start + from.length);
  assert.ok(start >= 0 && end > start, `Cannot locate ${from}`);
  return source.slice(start, end);
}

class Vector3 {
  constructor(x = 0, y = 0, z = 0) { Object.assign(this, { x, y, z }); }
  clone() { return new Vector3(this.x, this.y, this.z); }
  setY(y) { this.y = y; return this; }
}

function makeContext() {
  const events = [];
  const context = vm.createContext({
    console,
    Math,
    performance: { now: () => context.wallTime * 1000 },
    wallTime: 100,
    TEAM: { PLAYER: 'player', ENEMY: 'enemy' },
    MATCH: { regulation: 180, overtime: 120, tiebreakDrainPerSecond: 180 },
    RETARGET: { troopLeashBonusTiles: 2.25 },
    THREE: {
      Group: class { constructor() { this.position = new Vector3(); } },
      Vector3,
      MathUtils: { clamp: (value, min, max) => Math.min(max, Math.max(min, value)) },
    },
    game: {
      started: true, running: true, elapsed: 20, time: 180,
      overtime: false, overtimeTime: 120, tiebreaker: false,
      crowns: { player: 0, enemy: 0 },
    },
    units: [], towers: [], buildings: [], projectiles: [], effects: [], hazards: [],
    ui: {
      timer: { textContent: '' }, phaseLabel: { textContent: '' },
      doubleAether: { textContent: '', classList: { remove() {}, add() {} } },
    },
    scene: { remove: () => events.push('remove') },
    disposeObject() {},
    cameraShake: 0,
    setTimeout() {},
    toast() {},
    createElectricSpark() {}, createBurst() {}, createFrostFootstep() {},
    createDamageNumber() {}, updateTowerHud() {}, updateUnitHud() {},
    updateBuildingHud() {},
    events,
    updateCameraMotion: () => events.push('camera'),
    updateAether: () => events.push('aether'),
    aiUpdate: () => { events.push('ai'); context.onAi?.(); },
    updateProjectiles: () => events.push('projectiles'),
    updateEffects: () => events.push('effects'),
    updateTiebreaker: () => events.push('tiebreaker'),
    finishMatch: () => { events.push('finish'); context.game.running = false; },
    beginTiebreaker: () => { events.push('begin-tiebreaker'); context.game.tiebreaker = true; },
  });
  vm.runInContext([
    `(() => {${fs.readFileSync(path.join(path.dirname(sourcePath), 'combat-rules.js'), 'utf8').replace(/^import .*;\r?\n/gm, '').replaceAll('export ', '')}; Object.assign(globalThis, {refreshSlow, refreshStun, spellDamageFor, directionalSight});})()`,
    extract('class CombatEntity {', '\nfunction coreShouldBeActive'),
    'class Tower extends CombatEntity {}\nclass DefensiveBuilding extends CombatEntity {}',
    extract('class Unit extends CombatEntity {', '\nconst units=[];'),
    extract('function formatTime(sec)', '\nfunction clearLiveProjectiles'),
    extract('function clearLiveProjectiles(){', '\nfunction beginTiebreaker'),
    extract('function updateHazards(dt){', '\nfunction castSpell'),
    extract('function cleanupDead(){', '\nfunction resize'),
    extract('function simulateBattleStep(dt){', '\nfunction tick'),
  ].join('\n'), context, { filename: sourcePath });
  return context;
}

function makeUnit(context) {
  const unit = vm.runInContext('Object.create(Unit.prototype)', context);
  Object.assign(unit, {
    dead: false, team: 'player', card: {}, moveSpeed: 100,
    slowPct: 0, slowUntil: 0, stunUntil: 0,
    animTime: 0, attackAnim: 0, hitAnim: 0, hudTimer: 0,
    cooldown: 0, targetScan: 0, stepFxTimer: 0, forcedTargetTimer: 0,
    navRepath: 0, target: null, structureLockTarget: null, forcedTarget: null,
    group: { position: new Vector3(), rotation: { y: 0 } },
    updateStormAura() {},
    acquireTarget() { context.events.push('acquire-target'); },
  });
  return unit;
}

function stubHazards(context) {
  context.updateHazards = () => context.events.push('hazards');
}

test('a regulation result stops combat in the same simulation step', () => {
  const context = makeContext();
  stubHazards(context);
  context.game.time = 0.005;
  context.game.crowns.player = 1;
  context.towers.push({ update: () => context.events.push('tower') });
  context.units.push({ update: () => context.events.push('unit') });
  context.simulateBattleStep(0.016);
  assert.ok(context.events.includes('finish'));
  assert.ok(context.events.includes('effects'));
  assert.deepEqual(context.events.filter(name => ['aether', 'ai', 'tower', 'unit', 'projectiles', 'hazards'].includes(name)), []);
});

test('the overtime-to-tiebreaker transition stops ordinary combat immediately', () => {
  const context = makeContext();
  stubHazards(context);
  context.game.overtime = true;
  context.game.overtimeTime = 0.005;
  context.towers.push({ update: () => context.events.push('tower') });
  context.units.push({ update: () => context.events.push('unit') });
  context.simulateBattleStep(0.016);
  assert.ok(context.events.includes('begin-tiebreaker'));
  assert.deepEqual(context.events.filter(name => ['aether', 'ai', 'tower', 'unit', 'projectiles', 'hazards'].includes(name)), []);
});

test('an already finished match animates effects without resolving damaging projectiles or hazards', () => {
  const context = makeContext();
  stubHazards(context);
  context.game.running = false;
  context.simulateBattleStep(0.016);
  assert.ok(context.events.includes('effects'));
  assert.deepEqual(context.events.filter(name => ['projectiles', 'hazards'].includes(name)), []);
});

test('an AI spell that ends the match stops subsequent battle phases', () => {
  const context = makeContext();
  stubHazards(context);
  context.onAi = () => { context.game.running = false; };
  context.towers.push({ update: () => context.events.push('tower') });
  context.simulateBattleStep(0.016);
  assert.ok(context.events.includes('ai'));
  assert.deepEqual(context.events.filter(name => ['tower', 'projectiles', 'hazards'].includes(name)), []);
});

test('a tower-ending combat update prevents later combatants taking another turn', () => {
  const context = makeContext();
  stubHazards(context);
  context.towers.push(
    { update() { context.events.push('first-tower'); context.game.running = false; } },
    { update: () => context.events.push('second-tower') },
  );
  context.units.push({ update: () => context.events.push('unit') });
  context.simulateBattleStep(0.016);
  assert.ok(context.events.includes('first-tower'));
  assert.deepEqual(context.events.filter(name => ['second-tower', 'unit', 'projectiles', 'hazards'].includes(name)), []);
});

test('entities reject damage after a result and during tiebreaker', () => {
  const context = makeContext();
  const entity = vm.runInContext("new CombatEntity('player', 100)", context);
  context.game.running = false;
  entity.takeDamage(40);
  assert.equal(entity.hp, 100);
  context.game.running = true;
  context.game.tiebreaker = true;
  entity.takeDamage(40);
  assert.equal(entity.hp, 100);
  context.game.tiebreaker = false;
  entity.takeDamage(40);
  assert.equal(entity.hp, 60);
});

test('slow and stun do not expire while the simulation is paused', () => {
  const context = makeContext();
  const unit = makeUnit(context);
  unit.applySlow(0.3, 2);
  unit.applyStun(0.4);
  context.wallTime += 5;
  assert.equal(unit.currentMoveSpeed(), 70);
  unit.update(0);
  assert.ok(!context.events.includes('acquire-target'), 'A paused stun must still prevent acting');
});

test('slow and stun durations follow simulation speed', () => {
  const context = makeContext();
  const unit = makeUnit(context);
  unit.applySlow(0.3, 2);
  unit.applyStun(0.4);
  context.game.elapsed += 0.41;
  context.wallTime += 0.1025;
  unit.update(0);
  assert.ok(context.events.includes('acquire-target'), 'A 0.4-second stun must expire after 0.41 simulated seconds at 4x');
  context.game.elapsed += 1.7;
  context.wallTime += 0.425;
  assert.equal(unit.currentMoveSpeed(), 100);
});

test('death cleanup follows simulated time and retains newly dead groups while paused', () => {
  const context = makeContext();
  const entity = vm.runInContext("new CombatEntity('player', 100)", context);
  entity.destroy();
  assert.equal(entity.deathAt, context.game.elapsed);
  context.units.push(entity);
  context.wallTime += 5;
  context.cleanupDead();
  assert.equal(context.units.length, 1);
  context.game.elapsed += 0.43;
  context.cleanupDead();
  assert.equal(context.units.length, 0);
});

test('an entity that dies at simulation time zero is eventually cleaned up', () => {
  const context = makeContext();
  context.game.elapsed = 0;
  const entity = vm.runInContext("new CombatEntity('player', 100)", context);
  entity.destroy();
  context.units.push(entity);
  context.game.elapsed = 0.43;
  context.cleanupDead();
  assert.equal(context.units.length, 0);
});

for (const dt of [0.016, 0.019, 0.03, 0.033, 0.05]) {
  test(`Meteor Shards delivers five DOT ticks with ${dt}s frame steps`, () => {
    const context = makeContext();
    let damage = 0;
    context.units.push({
      dead: false, team: 'enemy', radius: 0.5, group: { position: new Vector3() },
      takeDamage(amount) { damage += amount; },
    });
    context.hazards.push({
      kind: 'meteor_shards', team: 'player', position: new Vector3(), radius: 4.5,
      remaining: 5, tick: 1, tickEvery: 1, damage: 40, fxTick: 1000,
    });
    for (let frame = 0; context.hazards.length && frame < 1000; frame++) context.updateHazards(dt);
    assert.equal(context.hazards.length, 0);
    assert.equal(damage, 200);
  });
}

test('owned scene resources are released once even when several child meshes share them', () => {
  const context = vm.createContext({});
  vm.runInContext(extract('function disposeObject(object){', '\nfunction mat('), context);
  const counts = { geometry: 0, material: 0, texture: 0 };
  const geometry = { dispose() { counts.geometry++; } };
  const texture = { isTexture: true, dispose() { counts.texture++; } };
  const material = { map: texture, emissiveMap: texture, dispose() { counts.material++; } };
  context.disposeObject({ children: [
    { geometry, material, children: [] },
    { geometry, material: [material], children: [] },
  ] });
  assert.deepEqual(counts, { geometry: 1, material: 1, texture: 1 });
});

test('disposing a troop preserves cached GLB resources and shared Sprite geometry', () => {
  const context = vm.createContext({});
  vm.runInContext(extract('function disposeObject(object){', '\nfunction mat('), context);
  const disposed = [];
  const resource = name => ({ dispose() { disposed.push(name); } });
  context.disposeObject({ children: [
    {
      userData: { riftBlenderModel: true },
      children: [{ geometry: resource('cached-geometry'), material: resource('cached-material') }],
    },
    {
      isSprite: true,
      geometry: resource('shared-sprite-geometry'),
      material: { ...resource('owned-sprite-material'), map: { ...resource('owned-sprite-texture'), isTexture: true } },
    },
  ] });
  assert.deepEqual(disposed.sort(), ['owned-sprite-material', 'owned-sprite-texture']);
});

test('scheduled bullet impacts wait for simulated delay and fire once without owning a mesh', () => {
  const context = makeContext();
  let impacts = 0;
  let debris = 0;
  Object.assign(context, {
    createImpactFlash() { impacts++; },
    createDebrisBurst() { debris++; },
  });
  vm.runInContext(extract('function updateEffects(dt){', '\nfunction castNova('), context);
  context.effects.push({ type: 'bulletImpact', t: 0, d: 0.024, end: new Vector3(), color: 0xffffff });
  context.updateEffects(0);
  context.updateEffects(0.01);
  assert.equal(impacts, 0);
  context.updateEffects(0.014);
  assert.equal(impacts, 1);
  assert.equal(debris, 1);
  assert.equal(context.effects.length, 0);
  context.updateEffects(0.1);
  assert.equal(impacts, 1);
});

test('expiring an ordinary transient effect releases its owned scene resources', () => {
  const context = makeContext();
  let disposed = 0;
  Object.assign(context, { disposeObject() { disposed++; } });
  vm.runInContext(extract('function updateEffects(dt){', '\nfunction castNova('), context);
  context.effects.push({ type: 'light', t: 0, d: 0.1, size: 1, mesh: { intensity: 1 } });
  context.updateEffects(0.1);
  assert.equal(disposed, 1);
  assert.equal(context.effects.length, 0);
  assert.ok(context.events.includes('remove'));
});

for (const speed of [0.25, 1, 2, 4]) {
  test(`the frame loop preserves elapsed time at ${speed}x speed with a 50ms frame`, () => {
    const context = makeContext();
    const steps = [];
    Object.assign(context, {
      sandbox: { speed },
      clock: { getDelta: () => 0.05 },
      requestAnimationFrame() {},
      updateAmbient() {}, updateDebugOverlays() {}, updateDeveloperReadout() {}, updateHealthBarFacing() {},
      composer: { render() {} },
      simulateBattleStep: dt => steps.push(dt),
      lastRecoveredFrameError: 0,
    });
    vm.runInContext(extract('function tick(){', '\nfunction startMatch('), context);
    context.tick();
    assert.ok(steps.length > 0);
    assert.ok(Math.abs(steps.reduce((sum, dt) => sum + dt, 0) - 0.05 * speed) < 1e-9);
    assert.ok(steps.every(dt => dt <= 1 / 60 + 1e-9));
  });
}
