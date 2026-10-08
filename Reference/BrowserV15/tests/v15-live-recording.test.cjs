const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const {root, source, definition, makeContext, install, Vector3} = require('./v14-harness.cjs');
const modulePromise = import('data:text/javascript;base64,' + Buffer.from(fs.readFileSync(path.join(root, 'src/battle-recorder.js'), 'utf8')).toString('base64'));
async function context() {
  const c = makeContext(); c.game.elapsed = 0; c.game.aether = {player: 10, enemy: 10};
  c.game.playsByTeam = {player: 0, enemy: 0}; c.game.leakPending = {player: 0, enemy: 0}; c.game.openingHands = {player: Array.from(c.game.playerDeck.hand), enemy: Array.from(c.game.enemyDeck.hand)};
  c.game.recorder = (await modulePromise).createBattleRecorder(c.cards, {storage: null}); c.game.recorder.start({training: true});
  c.persistLabSave = () => c.events.push({type: 'lab-saved'}); c.game.replayViewer = {refresh: () => c.events.push({type: 'replays-refreshed'})};
  c.captureBattleState(true); return c;
}
function finish(c) {c.captureBattleState(true); return c.game.recorder.finish(c.game.elapsed, {winner: null, crowns: c.game.crowns});}
function paid(c, team, cardId, point = new Vector3(7, 0, 7)) {const card = c.cards[cardId], before = c.game.aether[team]; assert.ok(before >= card.cost, 'Test deployment respects real Aether budget'); c.game.aether[team] -= card.cost; const play = c.recordCardPlay(team, card, point, before); return {play, spawned: c.spawnCard(team, card, point, play)};}

test('real Bullet Burst records all five swarm kills, actual overkill, building/tower damage and the paid cast', async () => {
  const c = await context(); const bats = c.spawnCard('enemy', c.cards.vampire_bats, new Vector3(7, 0, 7));
  const tower = new c.Tower('enemy', 'guard', 7, 7, 1), building = new c.DefensiveBuilding('enemy', c.cards.archer_tower, 7, 7);
  paid(c, 'player', 'bullet_burst'); c.game.elapsed = .1; const replay = finish(c), p = replay.analysis.teams.player, row = p.cards.bullet_burst;
  assert.ok(bats.every(b => b.dead)); assert.equal(tower.hp, 2195); assert.equal(building.hp, 795);
  assert.equal(row.uses, 1); assert.equal(row.aetherSpent, 2); assert.equal(row.troopDamage, 174 * 5); assert.equal(row.towerDamage, 55); assert.equal(row.buildingDamage, 55);
  assert.equal(row.kills, 5); assert.equal(row.killedAether, 5); assert.equal(p.biggestDefensiveTrade.value, 3);
  const hits = replay.events.filter(e => e.type === 'damage'); assert.equal(hits.length, 7); assert.ok(hits.every(e => e.data.sourceCardId === 'bullet_burst' && e.data.playId));
  assert.equal(hits.filter(e => e.data.targetKind === 'troop').reduce((sum, e) => sum + e.data.overkill, 0), 5);
});

test('real Meteor initial hit and all five DoT ticks share one cast attribution and never damage structures', async () => {
  const c = await context(), ground = new c.Unit('enemy', c.cards.ironclad, 7, 7), air = new c.Unit('enemy', c.cards.storm_raven, 7, 7), tower = new c.Tower('enemy', 'guard', 7, 7, 1);
  const {play} = paid(c, 'player', 'meteor_shards');
  for (let i = 0; i < 100; i++) {c.game.elapsed += .05; c.updateHazards(.05);}
  const replay = finish(c), row = replay.analysis.teams.player.cards.meteor_shards, hits = replay.events.filter(e => e.type === 'damage');
  assert.equal(ground.hp, 378); assert.equal(air.hp, 875); assert.equal(tower.hp, 2250); assert.equal(row.troopDamage, 924); assert.equal(row.uses, 1); assert.equal(row.aetherSpent, 5);
  assert.equal(hits.length, 12); assert.ok(hits.every(e => e.data.sourceCardId === 'meteor_shards' && e.data.playId === play.playId));
  assert.equal(hits.filter(e => e.data.damageKind === 'dot').length, 10); assert.equal(hits.filter(e => e.data.damageKind === 'initial').length, 2);
});

test('real Frost slow and Raven air/ground aura preserve source, actual damage and status expiry in event replay', async () => {
  const c = await context(), {spawned: [frost]} = paid(c, 'player', 'frost_fang'), ground = new c.Unit('enemy', c.cards.ironclad, 7, 6), air = new c.Unit('enemy', c.cards.sky_manta, 7, 6);
  frost.target = ground; frost.attack(); c.devModifyAether('player', 10); const {spawned: [raven]} = paid(c, 'player', 'storm_raven'); raven.updateStormAura(3);
  c.game.elapsed = .1; const replay = finish(c), hits = replay.events.filter(e => e.type === 'damage'), status = replay.events.filter(e => e.type === 'status');
  assert.equal(replay.analysis.teams.player.cards.frost_fang.troopDamage, 72); assert.equal(replay.analysis.teams.player.cards.storm_raven.troopDamage, 164);
  assert.ok(hits.filter(e => e.data.sourceCardId === 'storm_raven').every(e => e.data.damageKind === 'aura'));
  assert.equal(status.filter(e => e.data.kind === 'slow').length, 1); assert.equal(status.filter(e => e.data.kind === 'stun').length, 2);
  const s = (await modulePromise).replayStateAt(replay, .1); assert.ok(s.entities.find(e => e.id === ground._replayId).slowUntil > .1); assert.ok(s.entities.find(e => e.id === air._replayId).stunUntil > .1);
});

test('real primary and splash projectile impacts retain their source deployment and damage credit', async () => {
  const c = await context(), {play, spawned: [arc]} = paid(c, 'player', 'arc_mage');
  const ground = new c.Unit('enemy', c.cards.ironclad, 7, 7), air = new c.Unit('enemy', c.cards.sky_manta, 8, 7);
  c.projectiles.push({target: ground, source: c.battleDamageSource(arc), team: 'player', damage: 120, splash: 2.5, color: 0, mesh: {position: new Vector3(), rotation: {x: 0, y: 0}}, start: new Vector3(), t: 0, duration: .1, life: 3, trail: 100, arc: 0});
  c.updateProjectiles(.1); c.game.elapsed = .1; const replay = finish(c), hits = replay.events.filter(e => e.type === 'damage');
  assert.equal(ground.hp, 720); assert.equal(air.hp, 360); assert.equal(replay.analysis.teams.player.cards.arc_mage.troopDamage, 240);
  assert.equal(hits.length, 2); assert.ok(hits.every(e => e.data.sourceCardId === 'arc_mage' && e.data.playId === play.playId));
  assert.match(definition('fireProjectile'), /source:battleDamageSource\(source\)/);
});

test('real player deployment captures hand cycle and bank before/after, while Developer free spawns spend no Aether', async () => {
  const c = await context(); install(c, ['attemptPlayerDeploy']); c.pointInsideArena = () => true; c.validPlayerPlacement = () => true; c.placementOccupied = () => false; c.aiObservePlayerPlay = () => {};
  const handBefore = Array.from(c.game.playerDeck.hand), next = c.game.playerDeck.next(); c.game.elapsed = 1;
  assert.equal(c.attemptPlayerDeploy(0, new Vector3(7, 0, 7)), true); assert.equal(c.game.playerDeck.hand[0], next); assert.equal(c.game.aether.player, 7);
  c.game.elapsed = 2; c.sandbox.spawnCard = 'vampire_bats'; c.sandbox.spawnTeam = 'enemy'; c.devSpawnAt(new Vector3(7, 0, -7)); const replay = finish(c), plays = replay.events.filter(e => e.type === 'card_play');
  assert.equal(plays.length, 2); assert.equal(plays[0].data.cardId, handBefore[0]); assert.equal(plays[0].data.cost, 3); assert.equal(plays[0].data.aetherBefore, 10); assert.equal(plays[0].data.aetherAfter, 7);
  assert.equal(plays[1].data.sandbox, true); assert.equal(plays[1].data.cost, 0); assert.equal(replay.analysis.teams.enemy.aetherSpent, 0);
  assert.ok(replay.snapshots.some(s => s.time === 1 && s.state.aether.player === 10)); assert.ok(replay.snapshots.some(s => s.time === 1 && s.state.aether.player === 7));
  assert.equal((await modulePromise).replayStateAt(replay, 1).hands.player[0], next);
});

test('real cap leakage is regeneration only; Developer grants have exact recorded Aether and no leak cost', async () => {
  const c = await context(); c.game.aether = {player: 9.5, enemy: 10}; c.game.elapsed = 1; c.updateAether(2.8); c.captureBattleState(true);
  c.game.elapsed = 2; c.devModifyAether('player', -5); const replay = finish(c);
  assert.ok(Math.abs(replay.analysis.teams.player.aetherLeaked - .5) < 1e-9); assert.ok(Math.abs(replay.analysis.teams.enemy.aetherLeaked - 1) < 1e-9);
  assert.equal(replay.analysis.teams.player.aetherSpent, 0); const grant = replay.events.find(e => e.type === 'aether_grant'); assert.equal(grant.data.amount, -5); assert.equal(grant.data.aether, 5);
  assert.equal((await modulePromise).replayStateAt(replay, 2).aether.player, 5);
});

test('live tower result finishes once, saves replay, credits actual crowns, and excludes post-result damage', async () => {
  const c = await context(), tower = new c.Tower('enemy', 'core', 7, 7), {spawned: [ram]} = paid(c, 'player', 'rambeast');
  c.game.elapsed = 1; ram.target = tower; while (!tower.dead) {ram.attack(); if (c.game.running) c.game.elapsed += ram.attackSpeed;}
  assert.equal(c.game.running, false); assert.equal(c.game.recorder.active, false); assert.equal(c.game.crowns.player, 3);
  assert.equal(c.game.recorder.history().length, 1); const replay = c.game.recorder.get(c.game.lastReplayId);
  assert.equal(replay.result.winner, 'player'); assert.equal(replay.result.coreKill, true); assert.equal(replay.analysis.teams.player.cards.rambeast.crownContribution, 3);
  assert.equal(replay.analysis.teams.player.towerDamage, 3600); assert.equal(c.events.filter(e => e.type === 'lab-saved').length, 1);
  const count = replay.events.length; tower.takeDamage(500, ram); c.finishMatch('player'); assert.equal(c.game.recorder.get(replay.id).events.length, count); assert.equal(c.game.recorder.history().length, 1);
});

test('all direct AI spell branches pass recorded play IDs and capture the resulting hand', () => {
  for (const method of ['aiTrySpellCycle', 'aiTryDefense', 'aiTrySupport']) {
    const body = definition(method);
    assert.match(body, /recordCardPlay\(TEAM\.ENEMY/); assert.match(body, /spawnCard\(TEAM\.ENEMY,[^;]+,play\)/); assert.match(body, /captureBattleState\(true\)/);
  }
});

test('real tiebreaker records simultaneous fallen towers with one awarded crown, a destruction bookmark, and no invented card credit', async () => {
  const c = await context(), playerA = new c.Tower('player', 'guard', -7, 12, -1), playerB = new c.Tower('player', 'guard', 7, 12, 1), enemy = new c.Tower('enemy', 'guard', -7, -12, -1);
  playerA.hp = 50; playerB.hp = 50; enemy.hp = 100; c.game.overtime = true; c.game.overtimeTime = .01; c.captureBattleState(true); c.updateTimer(.02);
  assert.equal(c.game.tiebreaker, true);
  for (const dt of [.84, .1, .2]) {c.game.elapsed += dt; c.updateTiebreaker(dt); if (c.game.running) c.captureBattleState(true);}
  assert.equal(c.game.running, false); const replay = c.game.recorder.get(c.game.lastReplayId), fallen = replay.events.filter(e => e.type === 'tower_destroy');
  assert.equal(replay.result.winner, 'enemy'); assert.deepEqual(replay.result.crowns, {player: 0, enemy: 1}); assert.equal(fallen.length, 2);
  assert.equal(fallen.reduce((sum, e) => sum + e.data.crownsAwarded, 0), 1); assert.ok(fallen.every(e => e.data.reason === 'tiebreaker'));
  assert.equal(replay.analysis.teams.enemy.crownContribution, 0); assert.equal(replay.analysis.teams.player.crownContribution, 0); assert.equal(replay.analysis.teams.enemy.towerDamage, 0);
  assert.equal(replay.analysis.bookmarks.find(b => b.id === 'first-tower-destroyed').time, c.game.elapsed); assert.equal(replay.analysis.bookmarks.find(b => b.id === 'match-end').time, c.game.elapsed);
});
