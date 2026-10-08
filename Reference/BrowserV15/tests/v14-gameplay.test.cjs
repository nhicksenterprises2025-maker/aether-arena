const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');
const { pathToFileURL } = require('node:url');
const { root, source, range, definition, makeContext, install, Vector3 } = require('./v14-harness.cjs');

const locked = {
  ironclad: { cost: 3, hp: 840, damage: 96, attackSpeed: 1, moveSpeed: 2.25, range: 1.35, canHitAir: false },
  ember_archer: { cost: 3, hp: 423, damage: 152, attackSpeed: 1.55, moveSpeed: 2.35, range: 6, canHitAir: true, projectileSpeed: 16 },
  twin_blades: { cost: 2, count: 2, hp: 262, damage: 58, attackSpeed: .72, moveSpeed: 3.35, range: 1.2, canHitAir: false },
  boulderback: { cost: 5, hp: 1620, damage: 118, attackSpeed: 2, moveSpeed: 1.3, range: 1.5, buildingsOnly: true },
  arc_mage: { cost: 4, hp: 547, damage: 120, attackSpeed: 1, moveSpeed: 2, range: 6.5, canHitAir: true, projectileSpeed: 17, splash: 2.5 },
  rambeast: { cost: 4, hp: 880, damage: 140, attackSpeed: 1.4, moveSpeed: 2.6, range: 1.45, buildingsOnly: true, chargeDamage: 255 },
  sky_manta: { cost: 3, hp: 480, damage: 77, attackSpeed: .92, moveSpeed: 3.05, range: 3.4, flying: true, canHitAir: true, projectileSpeed: 15 },
  archer_tower: { cost: 4, hp: 850, damage: 75, attackSpeed: 1.1, range: 7, canHitAir: true, lifetime: 25, projectileSpeed: 18 },
  bullet_burst: { cost: 2, damage: 175, towerDamage: 55, radius: 2.2, bulletCount: 7 },
  nova_flask: { cost: 4, damage: 375, towerDamage: 185, radius: 3.25 },
  vampire_bats: { cost: 5, count: 5, hp: 174, damage: 86, attackSpeed: 1.05, moveSpeed: 1.95, range: 2, flying: true, canHitAir: true },
  frost_fang: { cost: 5, hp: 1155, damage: 72, attackSpeed: .8, moveSpeed: 2.5, range: 1.1, canHitAir: false, slowPct: .3, slowDuration: 2 },
  storm_raven: { cost: 6, hp: 1337, damage: 251, attackSpeed: 1.7, moveSpeed: 1.18, range: 4.3, flying: true, buildingsOnly: true, auraRadius: 2, auraDamage: 82, auraInterval: 3, stunDuration: .4 },
  meteor_shards: { cost: 5, damage: 262, dotDamage: 40, dotDuration: 5, radius: 4.5, towerDamage: 0 },
};
for (const [id, stats] of Object.entries(locked)) {
  test(`${id}: live stats and actual deployment remain intact`, () => {
    const c = makeContext(), card = c.cards[id];
    for (const [field, expected] of Object.entries(stats)) assert.equal(card[field], expected, `${id}.${field}`);
    const result = c.spawnCard('player', card, new Vector3(7, 0, 7));
    if (card.spell) {
      assert.equal(result.length, 0);
      assert.ok(c.events.some(event => event.type === (id === 'nova_flask' ? 'createNovaImpact' : id === 'bullet_burst' ? 'createBulletBurstFx' : 'createMeteorShardFx')));
    } else {
      assert.equal(result.length, card.count || 1);
      assert.ok(result.every(entity => entity.hp === card.hp && entity.team === 'player'));
      if (!card.building) assert.ok(result.every(entity => entity.frontSightTiles === 8 && entity.rearSightTiles === 5));
    }
  });
}

test('Guard/Core towers retain locked stats and dormant Core activates only after a Guard falls', () => {
  const c = makeContext(), guard = new c.Tower('player', 'guard', -8.2, 12.4, -1), core = new c.Tower('player', 'core', 0, 16.3);
  assert.deepEqual([guard.hp, guard.damage, guard.attackSpeed, guard.range], [2250, 86, 1.02, 10]);
  assert.deepEqual([core.hp, core.damage, core.attackSpeed, core.range], [3600, 112, .92, 8.9]);
  core.update(.1); assert.equal(core.active, false);
  guard.destroy(); assert.equal(core.active, true);
});

test('ground troops cannot acquire air and structure win conditions ignore troop targets', () => {
  for (const card of Object.values(makeContext().cards).filter(card => !card.spell && !card.building)) {
    const c = makeContext(), attacker = new c.Unit('player', c.cards[card.id], 7, 7);
    const air = new c.Unit('enemy', c.cards.sky_manta, 7, 6), ground = new c.Unit('enemy', c.cards.ironclad, 7, 6);
    const tower = new c.Tower('enemy', 'guard', 7, -12, 1);
    attacker.acquireTarget();
    if (card.buildingsOnly) assert.equal(attacker.target, tower, `${card.id} must choose structure`);
    else {
      assert.equal(attacker.canTargetUnit(air), !!card.canHitAir, card.id);
      assert.equal(attacker.canTargetUnit(ground), true, card.id);
    }
  }
});

test('directional sight uses actual facing with eight front and five rear tiles', () => {
  const c = makeContext(), unit = new c.Unit('player', c.cards.ironclad, 0, 10);
  const front = new c.Unit('enemy', c.cards.ironclad, 0, 2), rear = new c.Unit('enemy', c.cards.ironclad, 0, 16);
  assert.equal(c.sightRangeFor(unit, front), 8);
  assert.equal(c.sightRangeFor(unit, rear), 5);
  assert.equal(c.isWithinSight(unit, front), true);
  assert.equal(c.isWithinSight(unit, rear), false);
});

for (const id of ['nova_flask', 'bullet_burst', 'meteor_shards']) {
  test(`${id} damages enemy ground/air with correct structure and friendly exclusions`, () => {
    const c = makeContext(), card = c.cards[id];
    const ground = new c.Unit('enemy', c.cards.ironclad, 7, 7), air = new c.Unit('enemy', c.cards.storm_raven, 7, 7);
    const friendly = new c.Unit('player', c.cards.ironclad, 7, 7), tower = new c.Tower('enemy', 'guard', 7, 7, 1);
    const building = new c.DefensiveBuilding('enemy', c.cards.archer_tower, 7, 7);
    c.castSpell('player', card, new Vector3(7, 0, 7));
    assert.equal(ground.hp, ground.maxHp - card.damage);
    assert.equal(air.hp, air.maxHp - card.damage);
    assert.equal(friendly.hp, friendly.maxHp);
    assert.equal(tower.hp, tower.maxHp - card.towerDamage);
    assert.equal(building.hp, building.maxHp - card.towerDamage);
    if (id === 'meteor_shards') {
      for (let i = 0; i < 100; i++) c.updateHazards(.05);
      assert.equal(ground.hp, ground.maxHp - 462);
      assert.equal(air.hp, air.maxHp - 462);
      assert.equal(tower.hp, tower.maxHp);
      assert.equal(building.hp, building.maxHp);
    }
  });
}

test('Archer Tower acquires airborne troops and expires after its real 25-second lifetime', () => {
  const c = makeContext(), building = new c.DefensiveBuilding('player', c.cards.archer_tower, 7, 7);
  const air = new c.Unit('enemy', c.cards.sky_manta, 7, 6);
  building.update(.4);
  assert.equal(building.target, air);
  assert.ok(c.events.some(event => event.type === 'projectile' && event.damage === 75));
  building.update(24.6);
  assert.equal(building.dead, true);
  assert.equal(building.hp, 0);
});

test('Frost Fang actual melee hit slows movement by 30%, refreshes, and does not stack', () => {
  const c = makeContext(), frost = new c.Unit('player', c.cards.frost_fang, 7, 7), target = new c.Unit('enemy', c.cards.ironclad, 7, 6);
  frost.target = target; frost.attack();
  assert.equal(target.hp, 840 - 72);
  assert.equal(target.currentMoveSpeed(), 2.25 * .7);
  c.game.elapsed++; frost.attack();
  assert.equal(target.currentMoveSpeed(), 2.25 * .7);
  assert.equal(target.slowUntil, c.game.elapsed + 2);
  c.game.elapsed += 2.001;
  assert.equal(target.currentMoveSpeed(), 2.25);
});

test('Storm Raven aura hits clustered ground/air every three seconds with 0.4-second stun', () => {
  const c = makeContext(), raven = new c.Unit('player', c.cards.storm_raven, 7, 7);
  const ground = new c.Unit('enemy', c.cards.ironclad, 7, 6), air = new c.Unit('enemy', c.cards.sky_manta, 7, 6);
  const friend = new c.Unit('player', c.cards.ironclad, 7, 6), tower = new c.Tower('enemy', 'guard', 7, 7, 1);
  raven.updateStormAura(2.99); assert.equal(ground.hp, 840);
  raven.updateStormAura(.011);
  assert.equal(ground.hp, 840 - 82); assert.equal(air.hp, 480 - 82);
  assert.equal(ground.stunUntil, c.game.elapsed + .4);
  assert.equal(friend.hp, 840); assert.equal(tower.hp, 2250);
  raven.updateStormAura(1); assert.equal(ground.hp, 840 - 82);
});

test('Rambeast spends its charge on structures for exactly 255 damage', () => {
  const c = makeContext(), ram = new c.Unit('player', c.cards.rambeast, 7, 7), tower = new c.Tower('enemy', 'guard', 7, 6, 1);
  ram.target = tower; ram.charged = true; ram.attack();
  assert.equal(tower.hp, 2250 - 255); assert.equal(ram.charged, false);
  ram.attack(); assert.equal(tower.hp, 2250 - 255 - 140);
});

test('Arc Mage projectile delivers primary and splash to enemies while excluding friendlies', () => {
  const c = makeContext(), primary = new c.Unit('enemy', c.cards.ironclad, 7, 7), air = new c.Unit('enemy', c.cards.sky_manta, 8, 7);
  const friend = new c.Unit('player', c.cards.ironclad, 7, 7);
  c.projectiles.push({ target: primary, team: 'player', damage: 120, splash: 2.5, color: 0, mesh: { position: new Vector3(), rotation: { x: 0, y: 0 } }, start: new Vector3(), t: 0, duration: .1, life: 3, trail: 100, arc: 0 });
  c.updateProjectiles(.1);
  assert.equal(primary.hp, 720); assert.equal(air.hp, 360); assert.equal(friend.hp, 840); assert.equal(c.projectiles.length, 0);
});

test('deck cycle always retains exactly the same eight unique cards through 100 plays', () => {
  const c = makeContext(), deck = new c.DeckState(c.defaultDeck);
  for (let i = 0; i < 100; i++) {
    assert.ok(deck.play(i % 4)); assert.ok(deck.next());
    assert.equal(deck.hand.length, 4); assert.equal(deck.queue.length, 4);
    assert.deepEqual([...deck.hand, ...deck.queue].sort(), Array.from(c.defaultDeck).sort());
  }
  const before = JSON.stringify(deck); assert.equal(deck.play(99), null); assert.equal(JSON.stringify(deck), before);
});

test('regulation enters OT only at 0-0; OT first tower ends immediately', () => {
  const c = makeContext(); c.game.time = .01; c.updateTimer(.02);
  assert.equal(c.game.overtime, true); assert.equal(c.game.overtimeTime, 120); assert.equal(c.game.running, true);
  const guard = new c.Tower('enemy', 'guard', 7, -12, 1); guard.destroy();
  assert.equal(c.game.running, false); assert.equal(c.game.winner, 'player'); assert.equal(c.game.crowns.player, 1);
  const d = makeContext(); d.game.time = .01; d.game.crowns.enemy = 1; d.updateTimer(.02);
  assert.equal(d.game.running, false); assert.equal(d.game.winner, 'enemy'); assert.equal(d.game.overtime, false);
});

test('0-0 overtime enters live equal-drain tiebreaker and awards a 1-0 result', () => {
  const c = makeContext(); c.game.overtime = true; c.game.overtimeTime = .01;
  const player = new c.Tower('player', 'guard', -7, 12, -1), enemy = new c.Tower('enemy', 'guard', -7, -12, -1);
  player.hp = 50; enemy.hp = 100; c.updateTimer(.02);
  assert.equal(c.game.tiebreaker, true);
  c.updateTiebreaker(.84); assert.equal(player.hp, 50);
  c.updateTiebreaker(.1); assert.equal(player.hp, 32); assert.equal(enemy.hp, 82);
  c.updateTiebreaker(.2); assert.equal(c.game.running, false); assert.equal(c.game.winner, 'enemy');
  assert.deepEqual(c.game.crowns, { player: 0, enemy: 1 });
});

test('Aether max and 1x/2x/3x regulation/overtime regeneration follow locked rules', () => {
  for (const [elapsed, overtime, overtimeTime, seconds] of [[0, false, 120, 2.8], [120, false, 120, 1.4], [180, true, 90, 1.4], [240, true, 59, 2.8 / 3]]) {
    const c = makeContext(); Object.assign(c.game, { elapsed, time: 180 - elapsed, overtime, overtimeTime }); c.game.aether = { player: 0, enemy: 0 };
    c.updateAether(seconds); assert.ok(Math.abs(c.game.aether.player - 1) < 1e-8); assert.ok(Math.abs(c.game.aether.enemy - 1) < 1e-8);
    c.updateAether(100); assert.equal(c.game.aether.player, 10); assert.equal(c.game.aether.enemy, 10);
  }
});

test('A* routes every ground card over a legal bridge, respecting structures and target lane', () => {
  const library = makeContext().cards;
  for (const card of Object.values(library).filter(card => !card.spell && !card.building && !card.flying)) {
    for (const lane of [-1, 1]) {
      const c = makeContext(), unit = new c.Unit('player', c.cards[card.id], 0, 14), target = new c.Tower('enemy', 'guard', lane * 8.2, -12.4, lane);
      new c.Tower('player', 'core', 0, 16.3); new c.DefensiveBuilding('player', c.cards.archer_tower, lane * 3, 8);
      const route = c.buildGroundPath(unit, target);
      assert.ok(route.length > 1, `${card.id} route missing`);
      assert.equal(unit.bridgeCommitX, lane * 7.2);
      for (const point of route.filter(point => Math.abs(point.z) < 1.9)) assert.ok(c.navOnBridge(point.x, point.z, unit), `${card.id} entered water`);
      assert.ok(route.every(point => Number.isFinite(point.x) && Number.isFinite(point.z)));
    }
  }
});

test('live ground movement crosses the river without stalling or walking through water', () => {
  const c = makeContext(), unit = new c.Unit('player', c.cards.boulderback, 0, 10), target = new c.Tower('enemy', 'guard', 8.2, -12.4, 1);
  for (let i = 0; i < 2400 && unit.group.position.z > -4; i++) {
    c.game.elapsed += 1 / 60; unit.update(1 / 60);
    if (Math.abs(unit.group.position.z) < 1.65) assert.ok(Math.abs(unit.group.position.x - 7.2) < 2.1);
  }
  assert.ok(unit.group.position.z < -4, `Unit stalled at ${unit.group.position.x},${unit.group.position.z}`);
  assert.equal(unit.target, target);
});

test('Developer Lab pause/speed, Aether, spawn, tower editing, field clear and AI toggle remain functional', () => {
  const c = makeContext(); c.setSandboxSpeed(0); assert.equal(c.sandbox.speed, 0); c.setSandboxSpeed(4); assert.equal(c.sandbox.speed, 4);
  c.devModifyAether('player', -100); assert.equal(c.game.aether.player, 0); c.devModifyAether('player', 10); assert.equal(c.game.aether.player, 10);
  c.ui.devSpawnCard.value = 'vampire_bats'; c.ui.devSpawnTeam.value = 'enemy'; c.armDeveloperSpawn(); c.devSpawnAt(new Vector3(7, 0, -8));
  assert.equal(c.units.length, 5); assert.equal(c.sandbox.spawnArmed, false);
  const guard = new c.Tower('player', 'guard', -7, 12, -1); c.ui.devTowerSelect.value = 'P_L'; c.ui.devTowerHp.value = '123'; c.applyDeveloperTowerHp(); assert.equal(guard.hp, 123);
  install(c, ['currentAiStyle', 'aiDecision']); c.setAiEnabled(false); assert.equal(c.game.ai.enabled, false); c.setAiEnabled(true); assert.equal(c.game.ai.enabled, true);
  c.clearDeveloperField(); assert.equal(c.units.length, 0); assert.equal(c.buildings.length, 0); assert.equal(c.towers.length, 1); assert.equal(guard.hp, 123);
});

test('event Meta Lab preserves all 14 cards, clean Bayesian confidence, aggregate persistence, and live battle pause wiring', async () => {
  const {cards, rules, styles} = require('./meta-fixtures.cjs');
  const [{createMetaEngine}, shared] = await Promise.all(['meta-engine', 'combat-rules'].map(name => import(pathToFileURL(path.join(root, `src/${name}.js`)))));
  const engine = createMetaEngine(cards, {rules, styles}); engine.runBatch(40);
  const saved = engine.snapshot(); assert.equal(saved.games, 40); assert.equal(Object.keys(saved.cards).length, 14);
  assert.ok(Object.values(saved.cards).every(stats => Object.values(stats).every(value => Number.isFinite(value))));
  const restored = createMetaEngine(cards, {rules, styles, saved}); assert.deepEqual(restored.snapshot(), saved);
  assert.equal(shared.adjustedWinRate(0, 0), 50);
  const confidence = shared.wilsonInterval(50, 100); assert.ok(confidence.low < 50 && confidence.high > 50);
  assert.ok(Math.abs(engine.validation().pickRateTotal - 800) < 1e-7);
  assert.match(definition('startMatch'), /setBattleRunning\(true\)/);
  assert.match(definition('finishMatch'), /setBattleRunning\(false\)/);
});

test('event Meta matchup separates Raven direct structure damage from aura answers to ground/air swarms', async () => {
  const {cards, rules, styles} = require('./meta-fixtures.cjs');
  const [{createMetaEngine}, shared] = await Promise.all(['meta-engine', 'combat-rules'].map(name => import(pathToFileURL(path.join(root, `src/${name}.js`)))));
  const engine = createMetaEngine(cards, {rules, styles});
  assert.equal(shared.canDirectTarget(cards.storm_raven, cards.ironclad), false);
  assert.equal(shared.canDirectTarget(cards.storm_raven, cards.sky_manta), false);
  assert.equal(shared.canDirectTarget(cards.storm_raven, {tower: true}), true);
  for (const target of ['twin_blades', 'sky_manta', 'vampire_bats']) {
    const matchup = engine.matchup('storm_raven', target);
    assert.ok(matchup.reasons.some(reason => reason.includes('82 damage')), target);
    assert.ok(matchup.reasons.some(reason => reason.includes('stun')), target);
  }
});

const aiNames = Array.from(source.matchAll(/^function (ai\w+|currentAiStyle|randomAiStyle|setAiStyle)\(/gm), match => match[1]);
for (const style of ['beatdown', 'aggro', 'control', 'cycle', 'split', 'spell_cycle', 'counter']) {
  test(`${style} AI uses its own deck and observations without reading player hand/Aether`, () => {
    const c = makeContext();
    const analysisPath = path.join(root, 'src/deck-analysis.js');
    if (fs.existsSync(analysisPath)) vm.runInContext(fs.readFileSync(analysisPath, 'utf8').replaceAll('export function', 'function') + '\nglobalThis.deckAnalyzer = createDeckAnalyzer(cards);', c);
    install(c, aiNames);
    if (source.includes('const AI_COUNTER_HINTS=')) vm.runInContext(range('const AI_COUNTER_HINTS=', 'function aiObservePlayerPlay'), c);
    Object.defineProperty(c.game.playerDeck, 'hand', { get() { throw new Error('AI cheated by inspecting player hand'); } });
    Object.defineProperty(c.game.aether, 'player', { get() { throw new Error('AI cheated by inspecting player Aether'); } });
    Object.defineProperty(c.game.ai, 'debugMatchup', { get() { throw new Error('AI used the private omniscient matchup report'); } });
    c.game.ai.style = style; c.game.aether.enemy = 10;
    for (let i = 0; i < 24; i++) { c.game.elapsed += .5; c.game.aiThink = 0; c.aiUpdate(.5); c.game.aether.enemy = Math.min(10, c.game.aether.enemy + 1); }
    assert.ok(c.game.ai.memory.lastDecision);
    assert.ok(c.game.aether.enemy >= 0 && c.game.aether.enemy <= 10);
    assert.equal(new Set([...c.game.enemyDeck.hand, ...c.game.enemyDeck.queue]).size, 8);
    c.aiObservePlayerPlay(c.cards.rambeast, new Vector3(-7, 0, 7)); assert.equal(c.game.ai.memory.playerAetherEstimate, 1);
    for (const id of ['ironclad', 'ember_archer', 'sky_manta', 'bullet_burst']) c.aiObservePlayerPlay(c.cards[id], new Vector3(7, 0, 7));
    assert.equal(c.aiPlayerCardLikelyReady('rambeast'), true);
  });
}

test('all seven live AI deck configurations enforce eight cards and meaningful strategic coverage', () => {
  const c = makeContext();
  vm.runInContext(fs.readFileSync(path.join(root, 'src/deck-analysis.js'), 'utf8').replaceAll('export function', 'function') + '\nglobalThis.deckAnalyzer = createDeckAnalyzer(cards);', c);
  install(c, ['configureAiDeck', 'shuffledCardPool']); c.renderDeveloperMatchup = () => {}; c.game.battleDeck = Array.from(c.defaultDeck);
  for (const style of ['beatdown', 'aggro', 'control', 'cycle', 'split', 'spell_cycle', 'counter']) {
    c.game.ai.style = style; c.configureAiDeck();
    const ids = [...c.game.enemyDeck.hand, ...c.game.enemyDeck.queue], profile = c.game.ai.deckProfile;
    assert.equal(c.presetStore.validateDeck(ids), true); assert.equal(profile.isComplete, true);
    assert.ok(profile.counts.winConditions >= 1 && profile.counts.spells >= 1 && profile.metrics.sustainedAntiAir >= 2);
    assert.ok(profile.metrics.rangedSupport >= 1 && profile.metrics.defensiveTroops >= 3);
    assert.ok(profile.winConditions.length >= 1); assert.ok(profile.cheapCycle.length >= 2);
    if (style === 'cycle') assert.ok(profile.averages.cost <= 3.375);
    if (style === 'spell_cycle') assert.ok(profile.counts.spells >= 2);
    if (style === 'split') assert.ok(profile.counts.winConditions >= 2);
  }
});

test('AI defense refuses ground-only answers against flying pressure and selects a compatible card', () => {
  const c = makeContext();
  vm.runInContext(fs.readFileSync(path.join(root, 'src/deck-analysis.js'), 'utf8').replaceAll('export function', 'function') + '\nglobalThis.deckAnalyzer = createDeckAnalyzer(cards);', c);
  install(c, aiNames); c.game.aether.enemy = 10;
  new c.Unit('player', c.cards.storm_raven, 7, -9);
  c.game.enemyDeck.hand = ['frost_fang', 'ironclad', 'twin_blades', 'rambeast'];
  const snapshot = c.aiThreatSnapshot(); assert.equal(c.aiTryDefense(snapshot), false);
  c.game.enemyDeck.hand[3] = 'ember_archer'; assert.equal(c.aiTryDefense(snapshot), true);
  const deployed = c.units.filter(unit => unit.team === 'enemy');
  assert.equal(deployed.length, 1); assert.equal(deployed[0].card.id, 'ember_archer');
});

function statContext() {
  const c = makeContext();
  install(c, ['n1', 'statNumber', 'toTiles', 'cardDps', 'targetModeLabel', 'combatClassLabel', 'categoryLabel', 'cardStatRows']);
  return c;
}

test('card details preserve exact live hit-speed, movement and range precision for every combat card', () => {
  const c = statContext();
  for (const card of Object.values(c.cards).filter(card => !card.spell)) {
    const rows = Object.fromEntries(c.cardStatRows(card));
    assert.equal(parseFloat(rows['Hit Speed']), card.attackSpeed, `${card.id} hit speed`);
    assert.equal(parseFloat(rows['Move Speed']), card.building ? 0 : card.moveSpeed, `${card.id} movement`);
    assert.equal(parseFloat(rows['Attack Range']), card.range, `${card.id} range`);
    assert.equal(parseFloat(rows.Hitpoints), card.hp); assert.equal(parseFloat(rows['Damage / Hit']), card.damage);
    assert.equal(parseFloat(rows['Front Sight']), 8); assert.equal(parseFloat(rows['Rear Sight']), 5);
    assert.equal(parseFloat(rows.Units), card.count || 1);
    assert.ok(Math.abs(parseFloat(rows.DPS) - card.damage / card.attackSpeed) <= .05);
    assert.equal(rows['Aether Cost'], String(card.cost));
  }
  for (const value of [0, 1, 1.05, 1.35, 1.55, 2.25, 3.35, 7, 16, 100, 850]) assert.equal(Number(c.statNumber(value)), value);
});

test('all spell details include explicit non-applicable stats, targeting, count and actual damage mechanics', () => {
  const c = statContext();
  for (const card of Object.values(c.cards).filter(card => card.spell)) {
    const rows = Object.fromEntries(c.cardStatRows(card));
    for (const label of ['Aether Cost', 'Hitpoints', 'Initial Damage', 'Tower / Building Damage', 'DPS', 'Hit Speed', 'Move Speed', 'Attack Range', 'Front Sight', 'Rear Sight', 'Units', 'Targeting', 'Radius']) assert.ok(rows[label], `${card.id} missing ${label}`);
    for (const label of ['Hitpoints', 'Hit Speed', 'Move Speed', 'Front Sight', 'Rear Sight']) assert.match(rows[label], /N\/A/);
    assert.equal(Number(rows['Initial Damage']), card.damage); assert.equal(Number(rows['Tower / Building Damage']), card.towerDamage);
    assert.equal(parseFloat(rows.Radius), card.radius); assert.equal(rows.Units, '1 cast'); assert.match(rows.Targeting, /Ground \+ Air/);
    if (card.dotDamage) { assert.equal(rows['Maximum Troop Damage'], '462 while stationary'); assert.equal(rows.DPS, '40/s inside zone'); }
    if (card.bulletCount) assert.equal(rows.Rounds, '7');
  }
});

test('special mechanics and swarm totals are explicit in card details', () => {
  const c = statContext(), frost = Object.fromEntries(c.cardStatRows(c.cards.frost_fang)), raven = Object.fromEntries(c.cardStatRows(c.cards.storm_raven)), ram = Object.fromEntries(c.cardStatRows(c.cards.rambeast));
  assert.match(frost['Frost Slow'], /30%.*2s.*refreshes, no stack/);
  assert.match(raven['Storm Ring'], /82.*3s.*2 tiles.*0\.4s stun.*Ground \+ Air troops/);
  assert.match(ram['Charged Hit'], /255 damage after 1\.65 s run/);
  for (const id of ['twin_blades', 'vampire_bats']) {
    const card = c.cards[id], rows = Object.fromEntries(c.cardStatRows(card));
    assert.match(rows.Hitpoints, new RegExp(`${card.hp} each.*${card.hp * card.count} total`));
    assert.match(rows.DPS, /each.*total/);
  }
});
