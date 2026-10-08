const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

const root = process.env.RIFT_GAME_ROOT || process.env.RIFT_PROJECT_ROOT || path.resolve(__dirname, '..');
const gameSource = fs.readFileSync(path.join(root, 'src/game.js'), 'utf8');
const cardStart = gameSource.indexOf('const CARD_LIBRARY ='), cardEnd = gameSource.indexOf('\n};', cardStart) + 4;
const cards = JSON.parse(JSON.stringify(vm.runInNewContext(gameSource.slice(cardStart, cardEnd) + '; CARD_LIBRARY')));
const recorderSource = fs.readFileSync(path.join(root, 'src/battle-recorder.js'), 'utf8');
const recorderUrl = 'data:text/javascript;base64,' + Buffer.from(recorderSource).toString('base64');
const promise = import(recorderUrl);
const viewerSource = fs.readFileSync(path.join(root, 'src/replay-viewer.js'), 'utf8').replace("'./battle-recorder.js'", JSON.stringify(recorderUrl));
const viewerPromise = import('data:text/javascript;base64,' + Buffer.from(viewerSource).toString('base64'));
const memory = () => { const values = new Map(); return {getItem: key => values.get(key), setItem: (key, value) => values.set(key, value), values}; };
const entity = (id, team = 'enemy', cardId = 'ironclad', hp = 840, extra = {}) => ({id, team, cardId, kind: 'troop', hp, maxHp: hp, x: 0, z: -3, count: 1, ...extra});
const state = (entities = [], extra = {}) => ({aether: {player: 5, enemy: 5}, hands: {player: ['ironclad', 'ember_archer', 'twin_blades', 'nova_flask'], enemy: ['boulderback', 'arc_mage', 'rambeast', 'sky_manta']}, crowns: {player: 0, enemy: 0}, entities, ...extra});
async function recorder(options = {}) { return (await promise).createBattleRecorder(cards, {storage: null, ...options}); }
function play(r, at, cardId, playId, team = 'player', extra = {}) {r.event(at, 'card_play', {team, cardId, playId, cost: cards[cardId].cost, ...extra});}
function damage(r, at, sourceCardId, playId, targetId, amount, extra = {}) {r.event(at, 'damage', {sourceTeam: 'player', sourceCardId, playId, targetId, targetTeam: 'enemy', targetKind: 'troop', targetCardId: 'ironclad', targetMaxHp: 840, targetCost: 3, targetCount: 1, amount, x: 0, z: 4, ...extra});}
function finiteTree(value) {if (typeof value === 'number') assert.ok(Number.isFinite(value)); else if (value && typeof value === 'object') Object.values(value).forEach(finiteTree);}

test('Recorder records real events immutably and computes per-card damage, Aether, kill, and death totals', async () => {
  const r = await recorder(), metadata = {aiStyle: 'control', playerDeck: ['ironclad']}; r.start(metadata); metadata.aiStyle = 'mutated';
  const initial = state([entity('enemy')]); r.snapshot(0, initial); initial.entities[0].hp = 1;
  play(r, 1, 'ironclad', 'p1'); damage(r, 2, 'ironclad', 'p1', 'enemy', 300);
  damage(r, 3, 'ironclad', 'p1', 'enemy', 540);
  r.event(3, 'death', {targetId: 'enemy', targetTeam: 'enemy', targetKind: 'troop', targetCardId: 'ironclad', sourceTeam: 'player', sourceCardId: 'ironclad', playId: 'p1', targetCost: 3, targetCount: 1, z: 4});
  r.event(3, 'death', {targetId: 'enemy', targetTeam: 'enemy', targetKind: 'troop', targetCardId: 'ironclad', sourceTeam: 'player', sourceCardId: 'ironclad', playId: 'p1', targetCost: 3, targetCount: 1, z: 4});
  r.event(3, 'aether_leak', {team: 'player', amount: .4});
  const result = r.finish(5, {winner: 'player'}), p = result.analysis.teams.player, e = result.analysis.teams.enemy;
  assert.equal(result.metadata.aiStyle, 'control'); assert.equal(result.snapshots[0].state.entities[0].hp, 840);
  assert.equal(p.cardsPlayed, 1); assert.equal(p.aetherSpent, 3); assert.equal(p.aetherLeaked, .4); assert.equal(p.damage, 840); assert.equal(p.troopDamage, 840);
  assert.equal(p.kills, 1); assert.equal(e.deaths, 1); assert.equal(p.cards.ironclad.damagePerAether, 280);
  assert.equal(p.mostValuable, 'ironclad'); assert.equal(p.biggestDefensiveTrade.value, 0); assert.equal(r.active, false);
  assert.equal(r.event(6, 'damage', {}), false); finiteTree(result.analysis);
});

test('Actual swarm kill value is per member, while hostile building and troop damage stay separate', async () => {
  const r = await recorder(); r.start(); play(r, 0, 'nova_flask', 'n');
  damage(r, 1, 'nova_flask', 'n', 'bat', 174, {targetCardId: 'vampire_bats', targetMaxHp: 174, targetCost: 5, targetCount: 5});
  damage(r, 1, 'nova_flask', 'n', 'building', 185, {targetKind: 'building', targetCardId: 'archer_tower', targetMaxHp: 850, targetCost: 4});
  r.event(1, 'death', {targetId: 'bat', targetTeam: 'enemy', targetKind: 'troop', targetCardId: 'vampire_bats', sourceTeam: 'player', sourceCardId: 'nova_flask', playId: 'n', targetCost: 5, targetCount: 5, z: 3});
  const a = r.finish(2).analysis, p = a.teams.player;
  assert.equal(p.troopDamage, 174); assert.equal(p.buildingDamage, 185); assert.equal(p.cards.nova_flask.killedAether, 1);
  assert.equal(p.biggestDefensiveTrade.value, -3);
  assert.ok(Math.abs(a.biggestSpell.value - (1 + 185 / 850 * 4)) < 1e-9);
});

test('Tower contributions use full tower HP and cannot credit Developer or tiebreaker drain to a card', async () => {
  const r = await recorder(); r.start(); play(r, 0, 'rambeast', 'r'); play(r, .1, 'nova_flask', 'n');
  damage(r, 1, 'rambeast', 'r', 'tower', 900, {targetKind: 'tower', targetMaxHp: 2250});
  damage(r, 2, 'nova_flask', 'n', 'tower', 185, {targetKind: 'tower', targetMaxHp: 2250});
  r.event(3, 'tower_destroy', {targetId: 'tower', targetTeam: 'enemy', crownsAwarded: 1});
  r.event(3, 'tower_destroy', {targetId: 'tower', targetTeam: 'enemy', crownsAwarded: 1});
  const a = r.finish(4).analysis;
  assert.ok(Math.abs(a.teams.player.cards.rambeast.crownContribution - .4) < 1e-9);
  assert.ok(Math.abs(a.teams.player.crownContribution - 1085 / 2250) < 1e-9);
  assert.equal(a.teams.player.towerDamage, 1085);
  assert.equal(a.biggestSpell.towerDamage, 185); assert.equal(a.biggestSpell.value, 0);
  assert.equal(a.bookmarks.filter(b => b.id === 'first-tower-destroyed').length, 1);
});

test('Snapshot analysis measures surviving push Aether, exact sample bank advantage, and time-weighted hand cost', async () => {
  const r = await recorder(); r.start();
  r.snapshot(0, state([], {aether: {player: 8, enemy: 3}, hands: {player: ['twin_blades'], enemy: ['boulderback']}}));
  r.snapshot(1, state([entity('bat-1', 'player', 'vampire_bats', 87, {maxHp: 174, count: 5}), entity('bat-2', 'player', 'vampire_bats', 174, {count: 5}), entity('own-half', 'player', 'boulderback', 1620, {z: 4})], {aether: {player: 1, enemy: 9}, hands: {player: ['storm_raven'], enemy: ['twin_blades']}}));
  const a = r.finish(4).analysis;
  assert.equal(a.teams.player.biggestPush.value, 1.5); assert.equal(a.teams.player.biggestPush.units, 2);
  assert.equal(a.teams.player.averageHandCost, 5); assert.equal(a.teams.enemy.averageHandCost, 2.75);
  assert.equal(a.teams.player.largestAetherAdvantage.value, 5); assert.equal(a.teams.enemy.largestAetherAdvantage.value, 8);
  assert.equal(a.bookmarks.find(b => b.id === 'largest-push').time, 1);
});

test('MVP/LVP use actual damage per Aether; unplayed damage sources do not become candidates', async () => {
  const r = await recorder(); r.start(); play(r, 0, 'ironclad', 'i'); play(r, 1, 'ember_archer', 'a');
  damage(r, 2, 'ironclad', 'i', 't1', 100); damage(r, 3, 'ember_archer', 'a', 't2', 300); damage(r, 3, 'sky_manta', 'missing', 't3', 900);
  const p = r.finish(5).analysis.teams.player;
  assert.equal(p.mostValuable, 'ember_archer'); assert.equal(p.leastValuable, 'ironclad');
  assert.equal(p.cards.sky_manta.uses, 0); assert.equal(p.cards.sky_manta.damagePerAether, 0);
});

test('Seeking applies exact recorded births, damage, deaths, phase, hand, resource, and crowns without rerunning RNG', async () => {
  const {replayStateAt} = await promise, r = await recorder(); r.start(); r.snapshot(0, state([entity('t', 'enemy', '', 2250, {kind: 'tower'})]));
  const spawned = entity('u', 'player', 'ironclad', 840, {z: 4}); r.event(1, 'entity_spawn', {entity: spawned});
  play(r, 1, 'ironclad', 'i', 'player', {aetherAfter: 2, handAfter: ['twin_blades', 'arc_mage']}); damage(r, 1.1, 'ironclad', 'i', 't', 96, {targetKind: 'tower'});
  r.event(1.2, 'death', {targetId: 'u'}); r.event(1.3, 'phase', {phase: 'overtime'}); r.event(1.4, 'crowns', {player: 1, enemy: 0});
  r.snapshot(2, state([entity('t', 'enemy', '', 2154, {kind: 'tower'}), {...spawned, hp: 0, dead: true}], {aether: {player: 2.2, enemy: 6}}));
  const replay = r.finish(3), originalRandom = Math.random; Math.random = () => {throw Error('Replay reran RNG');};
  try {
    assert.equal(replayStateAt(replay, .9).entities.length, 1);
    const s = replayStateAt(replay, 1.15); assert.equal(s.entities.length, 2); assert.equal(s.entities[0].hp, 2154); assert.equal(s.entities[1].dead, false); assert.equal(s.aether.player, 2); assert.deepEqual(s.hands.player, ['twin_blades', 'arc_mage']);
    const end = replayStateAt(replay, 1.5); assert.equal(end.entities[1].dead, true); assert.equal(end.phase, 'overtime'); assert.equal(end.crowns.player, 1);
    assert.deepEqual(replayStateAt(replay, 1.5), replayStateAt(replay, 1.5)); assert.equal(replayStateAt(replay, 99).time, 3);
  } finally {Math.random = originalRandom;}
});

test('Position interpolation follows recorded samples and snapshots themselves are never mutated by seeking', async () => {
  const {replayStateAt} = await promise, r = await recorder(); r.start(); r.snapshot(0, state([entity('u', 'player', 'ironclad', 840, {x: 0, z: 4})])); r.snapshot(2, state([entity('u', 'player', 'ironclad', 840, {x: 8, z: 2})]));
  const replay = r.finish(2), before = JSON.stringify(replay), mid = replayStateAt(replay, 1);
  assert.equal(mid.entities[0].x, 4); assert.equal(mid.entities[0].z, 3); assert.equal(JSON.stringify(replay), before);
});

test('All required replay bookmarks are generated from recorded events and actual spell value', async () => {
  const r = await recorder(); r.start(); r.snapshot(0, state()); play(r, 1, 'meteor_shards', 'm');
  damage(r, 2, 'meteor_shards', 'm', 'u', 262); damage(r, 3, 'meteor_shards', 'm', 'u', 40);
  damage(r, 3, 'ironclad', 'i', 'tower', 96, {targetKind: 'tower'}); r.event(4, 'tower_destroy', {targetId: 'tower', crownsAwarded: 1});
  r.snapshot(5, state([entity('push', 'player', 'ironclad')])); r.event(180, 'phase', {phase: 'overtime'});
  const a = r.finish(190).analysis;
  assert.deepEqual(new Set(a.bookmarks.map(b => b.id)), new Set(['first-tower-damage', 'first-tower-destroyed', 'largest-push', 'biggest-spell-value', 'overtime-start', 'match-end']));
  assert.equal(a.biggestSpell.time, 1); assert.ok(Math.abs(a.biggestSpell.value - 302 / 840 * 3) < 1e-9);
});

test('Versioned history round-trips through disk payload and JSON import with recomputed analysis', async () => {
  const disk = memory(), r = await recorder({storage: disk, maxReplays: 3}); r.start({aiStyle: 'cycle'}); play(r, 0, 'ironclad', 'p'); const replay = r.finish(3, {winner: 'player'});
  const loaded = await recorder({storage: disk}); assert.equal(loaded.history().length, 1); assert.equal(loaded.get(replay.id).analysis.teams.player.aetherSpent, 3);
  const imported = await recorder(); const exported = JSON.parse(r.export(replay.id)); exported.analysis.teams.player.aetherSpent = 999;
  assert.equal(imported.import(exported).analysis.teams.player.aetherSpent, 3);
  assert.equal(imported.hydrate(loaded.serialize()), true); assert.equal(imported.history().length, 1);
  assert.equal(imported.remove(replay.id), true); assert.equal(imported.remove(replay.id), false); assert.equal(imported.get(replay.id), null);
});

test('History is bounded, quota failures preserve newest full replay in RAM, and malformed old saves cannot crash recording', async () => {
  const storage = {getItem: () => '{broken', setItem() {throw Error('quota');}}, r = await recorder({storage, maxReplays: 2});
  for (let i = 0; i < 4; i++) {r.start(); r.event(i, 'ai_decision', {label: `decision ${i}`}); r.finish(i);}
  assert.equal(r.history().length, 1); assert.ok(r.storageError); const replay = r.get(r.history()[0].id); assert.equal(replay.events[0].data.label, 'decision 3');
  assert.equal(r.import('{broken'), null); assert.equal(r.import({formatVersion: 99, events: [], snapshots: [], duration: 1}), null); assert.equal(r.hydrate({oldDeck: []}), false);
});

test('Validation rejects non-finite event times and unsupported durations; malformed payload fields are sanitized', async () => {
  const {validateReplay} = await promise, r = await recorder(); r.start(); r.snapshot(0, state()); const good = r.finish(1);
  assert.equal(validateReplay({...good, duration: Infinity}), null); assert.equal(validateReplay({...good, duration: 4000}), null); assert.equal(validateReplay({...good, events: [{time: NaN, type: 'damage'}]}), null);
  const odd = {...good, snapshots: [{time: 0, state: {...state(), hands: {player: 2}, hazards: [null], entities: [null, entity('bad', 'player', 'ironclad', Infinity, {x: NaN})]}}]};
  const valid = validateReplay(odd); finiteTree(valid); assert.equal(valid.snapshots[0].state.entities[0].hp, 0); assert.deepEqual(valid.snapshots[0].state.hands.player, []);
});

test('Prototype card names and injected invalid numeric data cannot contaminate analysis or live balance', async () => {
  const before = JSON.stringify(cards), r = await recorder(); r.start();
  r.event(0, 'card_play', {team: 'player', cardId: '__proto__', cost: 999}); r.event(0, 'damage', {sourceTeam: 'player', sourceCardId: 'constructor', amount: Infinity});
  r.snapshot(0, state()); const replay = r.finish(1); assert.equal(replay.analysis.teams.player.cardsPlayed, 0); assert.deepEqual(replay.analysis.teams.player.cards, {}); finiteTree(replay); assert.equal(JSON.stringify(cards), before);
});

// Minimal DOM exercises real UI handlers and cleanup without external test packages.
class NodeElement {
  constructor(tag) {this.tagName = tag; this.children = []; this.attributes = {}; this.listeners = {}; this.classList = {add() {}}; this.value = ''; this.textContent = ''; this.files = [];}
  append(...children) {this.children.push(...children);} replaceChildren(...children) {this.children = children;} setAttribute(name, value) {this.attributes[name] = value;} addEventListener(name, fn) {this.listeners[name] = fn;} removeEventListener(name, fn) {if (this.listeners[name] === fn) delete this.listeners[name];} click() {return this.listeners.click?.();} getContext() {return null;}
}
function nodes(rootNode) {return [rootNode, ...rootNode.children.flatMap(nodes)];}
function installDOM() {
  const old = Object.fromEntries(['document', 'Image', 'requestAnimationFrame', 'cancelAnimationFrame'].map(key => [key, global[key]]));
  const frames = new Map(); let next = 0; global.document = {createElement: tag => new NodeElement(tag), createElementNS: (_ns, tag) => new NodeElement(tag)}; global.Image = class {set src(value) {this.source = value;}};
  global.requestAnimationFrame = fn => {frames.set(++next, fn); return next;}; global.cancelAnimationFrame = id => frames.delete(id);
  return {frames, restore() {for (const [key, value] of Object.entries(old)) if (value === undefined) delete global[key]; else global[key] = value;}};
}

test('Replay viewer controls really seek, change speed, play, pause, bookmark, and cancel animation frames', async () => {
  const dom = installDOM(); try {
    const {mountReplayViewer} = await viewerPromise, r = await recorder(); r.start(); r.snapshot(0, state()); r.event(1, 'phase', {phase: 'overtime'}); const replay = r.finish(20), host = new NodeElement('div'), viewer = mountReplayViewer(host, replay, {cards});
    const buttons = () => nodes(host).filter(n => n.tagName === 'button');
    buttons().find(n => n.textContent === '+5 SEC').click(); assert.equal(viewer.time, 5); buttons().find(n => n.textContent === '−5 SEC').click(); assert.equal(viewer.time, 0);
    buttons().find(n => n.textContent === '4×').click(); assert.equal(buttons().find(n => n.textContent === '4×').attributes['aria-pressed'], 'true');
    buttons().find(n => n.textContent.startsWith('Overtime Start')).click(); assert.equal(viewer.time, 1);
    viewer.play(); assert.equal(dom.frames.size, 1); buttons().find(n => n.textContent === 'PAUSE').click(); assert.equal(dom.frames.size, 0);
    viewer.play(); viewer.destroy(); assert.equal(dom.frames.size, 0); assert.equal(host.children.length, 0);
  } finally {dom.restore();}
});

test('Analysis renderer includes all requested metrics, real card rows, two SVG timelines, definitions, and replay action', async () => {
  const dom = installDOM(); try {
    const {renderMatchAnalysis} = await viewerPromise, r = await recorder(); r.start(); r.snapshot(0, state()); play(r, 1, 'ironclad', 'i'); damage(r, 2, 'ironclad', 'i', 'u', 96); const replay = r.finish(3), host = new NodeElement('div'); let selected = null;
    renderMatchAnalysis(host, replay, {cards, onReplay: id => selected = id}); const all = nodes(host), content = all.map(n => n.textContent).join(' ');
    for (const label of ['Aether spent', 'Aether leaked', 'Damage', 'Tower damage', 'Kills', 'Deaths', 'Cards played', 'Most valuable card', 'Least valuable card', 'Biggest push', 'Biggest defensive trade', 'Largest Aether advantage', 'Average hand cost', 'Crown contribution']) assert.ok(content.includes(label), label);
    assert.equal(all.filter(n => n.tagName === 'svg').length, 2); assert.ok(content.includes('Ironclad')); assert.ok(content.includes('METRIC DEFINITIONS'));
    all.find(n => n.textContent === 'WATCH REPLAY').click(); assert.equal(selected, replay.id);
  } finally {dom.restore();}
});

test('Controller switches history/replay/analysis modals and backdrop closing cancels playback without orphan frames', async () => {
  const dom = installDOM(); try {
    const {createReplayViewer} = await viewerPromise, r = await recorder(); r.start(); r.snapshot(0, state()); const replay = r.finish(8);
    const names = ['replay-viewer', 'match-analysis-content', 'replay-history-content', 'replay-modal', 'match-analysis-modal', 'replay-history-modal'], elements = new Map(names.map(id => [id, new NodeElement('div')]));
    for (const el of elements.values()) {const classes = new Set(['hidden']); el.classList = {add: value => classes.add(value), remove: value => classes.delete(value), contains: value => classes.has(value)};}
    global.document.querySelector = selector => elements.get(selector.slice(1)); global.document.querySelectorAll = () => [];
    let changes = 0; const viewer = createReplayViewer({recorder: r, cards, onChange: () => changes++});
    viewer.openHistory(); assert.equal(elements.get('replay-history-modal').classList.contains('hidden'), false);
    assert.equal(viewer.openReplay(replay.id), true); assert.equal(elements.get('replay-history-modal').classList.contains('hidden'), true); assert.equal(elements.get('replay-modal').classList.contains('hidden'), false);
    nodes(elements.get('replay-viewer')).find(n => n.textContent === 'PLAY').click(); assert.equal(dom.frames.size, 1);
    const modal = elements.get('replay-modal'); modal.listeners.pointerdown({target: modal}); assert.equal(dom.frames.size, 0); assert.equal(modal.classList.contains('hidden'), true);
    assert.equal(viewer.openAnalysis(replay.id), true); assert.equal(elements.get('match-analysis-modal').classList.contains('hidden'), false);
    viewer.openReplay(replay.id); assert.equal(elements.get('match-analysis-modal').classList.contains('hidden'), true);
    viewer.openHistory(); nodes(elements.get('replay-history-content')).find(n => n.textContent === 'DELETE').click(); assert.equal(changes, 1); assert.equal(r.history().length, 0);
    r.start(); r.finish(2, {winner: null, abandoned: true}); viewer.refresh();
    assert.ok(nodes(elements.get('replay-history-content')).some(n => n.textContent.includes('ABANDONED')));
    viewer.openAnalysis(); assert.ok(nodes(elements.get('match-analysis-content')).some(n => n.textContent.includes('ABANDONED BATTLE')));
    viewer.destroy(); assert.equal(modal.listeners.pointerdown, undefined);
  } finally {dom.restore();}
});

test('Developer edits, grants, clear field, and slow/stun replay at their exact event timestamps', async () => {
  const {replayStateAt} = await promise, r = await recorder(); r.start(); r.snapshot(0, state([entity('t', 'player', '', 2250, {kind: 'tower'}), entity('u', 'player')], {hazards: [{x: 0, z: 0, radius: 4.5, team: 'enemy'}]}));
  r.event(1, 'aether_grant', {team: 'player', amount: 5, aether: 10}); r.event(1, 'tower_edit', {targetId: 't', hp: 123});
  r.event(1, 'status', {targetId: 'u', kind: 'slow', until: 3, percent: .3}); r.event(1, 'status', {targetId: 'u', kind: 'stun', until: 1.4});
  r.event(2, 'clear_field', {sandbox: true}); const replay = r.finish(4), beforeClear = replayStateAt(replay, 1.1);
  assert.equal(beforeClear.aether.player, 10); assert.equal(beforeClear.entities[0].hp, 123); assert.equal(beforeClear.entities[1].slowUntil, 3); assert.equal(beforeClear.entities[1].stunUntil, 1.4);
  const afterClear = replayStateAt(replay, 2); assert.equal(afterClear.entities.length, 1); assert.equal(afterClear.entities[0].kind, 'tower'); assert.equal(afterClear.hazards.length, 0);
});

test('Same-time before/after snapshots preserve bank extrema while seeks use the final post-play state', async () => {
  const {replayStateAt} = await promise, r = await recorder(); r.start(); r.snapshot(0, state());
  r.snapshot(1, state([], {aether: {player: 10, enemy: 2}})); r.snapshot(1, state([], {aether: {player: 4, enemy: 2}}));
  const replay = r.finish(2); assert.equal(replay.snapshots.length, 3); assert.equal(replay.analysis.teams.player.largestAetherAdvantage.value, 8); assert.equal(replayStateAt(replay, 1).aether.player, 4);
});
