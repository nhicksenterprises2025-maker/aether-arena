const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

const root = process.env.RIFT_GAME_ROOT || process.env.RIFT_PROJECT_ROOT
  || (fs.existsSync(path.resolve(__dirname, '../src/game.js')) ? path.resolve(__dirname, '..') : path.resolve(__dirname, '../rift_crown_arena_v14'));
const source = fs.readFileSync(path.join(root, 'src/game.js'), 'utf8');
const start = source.indexOf('const CARD_LIBRARY =');
const end = source.indexOf('\n};', start) + 4;
assert.ok(start >= 0 && end > start, 'The tests must read the live game card library.');
const cards = JSON.parse(JSON.stringify(vm.runInNewContext(source.slice(start, end) + '; CARD_LIBRARY')));
const before = JSON.stringify(cards);
const moduleSource = fs.readFileSync(path.join(root, 'src/deck-analysis.js'), 'utf8');
const modulePromise = import('data:text/javascript;base64,' + Buffer.from(moduleSource).toString('base64'));
const ids = Object.keys(cards);
const defaultDeck = ['ironclad', 'ember_archer', 'archer_tower', 'boulderback', 'arc_mage', 'rambeast', 'sky_manta', 'nova_flask'];
async function analyzer(library = cards) { return (await modulePromise).createDeckAnalyzer(library); }
function allFinite(object) {
  if (typeof object === 'number') assert.ok(Number.isFinite(object), `Non-finite value: ${object}`);
  else if (object && typeof object === 'object') Object.values(object).forEach(allFinite);
}
function seeded(seed) {
  return () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
}

test('The full 14-card live library remains unchanged by every analyzer entrypoint', async () => {
  const a = await analyzer();
  assert.equal(ids.length, 14);
  a.analyzeDeck(ids); ids.forEach(id => a.cardIntel(id));
  for (const x of ids) for (const y of ids) { a.synergy(x, y); a.canDamage(x, y); a.counterScore(x, y); }
  a.matchup(defaultDeck, a.buildAiDeck('control', seeded(1)));
  assert.equal(JSON.stringify(cards), before);
});

test('Swarm HP and direct DPS are summed per deployment and spells are excluded from averages', async () => {
  const a = await analyzer(), r = a.analyzeDeck(['twin_blades', 'vampire_bats', 'bullet_burst']);
  assert.deepEqual(r.denominators, { cost: 3, hp: 2, dps: 2, range: 2 });
  assert.equal(r.averages.hp, (262 * 2 + 174 * 5) / 2);
  assert.equal(r.averages.dps, Math.round((58 / .72 * 2 + 86 / 1.05 * 5) / 2 * 100) / 100);
  assert.equal(r.averages.range, 1.6);
  assert.equal(r.counts.swarm, 2);
  const b = a.analyzeDeck(['archer_tower', 'nova_flask']);
  assert.equal(b.averages.hp, cards.archer_tower.hp);
  assert.equal(b.denominators.hp, 1);
  assert.equal(b.counts.buildings, 1);
});

test('Deck composition counts separate persistent troops, spells, buildings and dedicated win conditions', async () => {
  const a = await analyzer(), r = a.analyzeDeck(defaultDeck);
  assert.ok(r.valid && r.isComplete);
  assert.deepEqual(r.counts, { troops: 6, spells: 1, buildings: 1, antiAir: 5, groundOnly: 3, winConditions: 2, splash: 2, swarm: 0 });
  assert.equal(r.averages.cost, 3.75);
  assert.equal(r.synergies.length, 28);
  assert.ok(r.archetypes.includes('Control') && r.archetypes.includes('Beatdown') && r.archetypes.includes('Hybrid'));
  assert.ok(r.strengths.length);
});

test('Every 0–14-card partial deck has finite, honest results and exact-eight validation', async () => {
  const a = await analyzer();
  for (let n = 0; n <= ids.length; n++) {
    const r = a.analyzeDeck(ids.slice(0, n));
    assert.equal(r.valid, n === 8);
    assert.equal(r.isComplete, n === 8);
    assert.equal(r.denominators.cost, n);
    allFinite(r);
    allFinite(a.matchup(ids.slice(0, n), defaultDeck));
  }
  assert.deepEqual(a.analyzeDeck([]).averages, { cost: 0, hp: 0, dps: 0, range: 0 });
});

test('Duplicates, unknown IDs, prototype property names and non-arrays never count as selected cards', async () => {
  const a = await analyzer();
  const r = a.analyzeDeck([...defaultDeck, 'ironclad', 'missing', '__proto__', 'constructor', null]);
  assert.equal(r.valid, false); assert.equal(r.ids.length, 8); assert.equal(r.denominators.cost, 8);
  assert.ok(r.errors.some(error => error.includes('Duplicate')));
  assert.ok(r.errors.some(error => error.includes('Unknown')));
  allFinite(r);
  allFinite(a.analyzeDeck(null));
  assert.equal(a.cardIntel('constructor'), null);
  assert.equal(a.canDamage('__proto__', 'ironclad'), false);
});

test('Ground-only troops and structure attackers never become magical air counters', async () => {
  const a = await analyzer();
  for (const attacker of ['ironclad', 'twin_blades', 'frost_fang', 'boulderback', 'rambeast']) {
    for (const target of ['sky_manta', 'vampire_bats', 'storm_raven']) {
      assert.equal(a.canDamage(attacker, target), false, `${attacker} must not hit ${target}`);
      assert.equal(a.counterScore(attacker, target), 0);
      assert.ok(!a.cardIntel(attacker).bestAgainst.some(card => card.id === target));
    }
  }
  for (const attacker of ['boulderback', 'rambeast']) {
    assert.equal(a.canDamage(attacker, 'ironclad'), false);
    assert.equal(a.canDamage(attacker, 'archer_tower'), true);
  }
});

test('Storm Raven damages troops through its real delayed aura, never through structure DPS', async () => {
  const a = await analyzer();
  for (const target of ['ironclad', 'twin_blades', 'vampire_bats']) assert.equal(a.canDamage('storm_raven', target), true);
  assert.equal(a.canDamage('storm_raven', 'archer_tower'), true);
  assert.ok(a.counterScore('storm_raven', 'vampire_bats') < a.counterScore('arc_mage', 'vampire_bats'));
  assert.ok(a.cardIntel('storm_raven').suggestedUses.some(reason => reason.includes('ring')));
  const withoutAura = structuredClone(cards); withoutAura.storm_raven.auraDamage = 0;
  const b = await analyzer(withoutAura);
  assert.equal(b.canDamage('storm_raven', 'ironclad'), false);
  assert.equal(b.canDamage('storm_raven', 'vampire_bats'), false);
  assert.equal(b.canDamage('storm_raven', 'archer_tower'), true);
});

test('Defensive buildings shoot troops only and pulling structure attackers influences coverage', async () => {
  const a = await analyzer();
  for (const id of ['rambeast', 'boulderback', 'storm_raven', 'sky_manta']) assert.equal(a.canDamage('archer_tower', id), true);
  assert.equal(a.canDamage('archer_tower', 'archer_tower'), false);
  assert.ok(a.counterScore('archer_tower', 'rambeast') > 50);
  assert.ok(a.cardIntel('archer_tower').bestAgainst.find(card => card.id === 'rambeast').reason.includes('pulls'));
  const noCharge = structuredClone(cards); noCharge.rambeast.chargeDamage = noCharge.rambeast.damage;
  const b = await analyzer(noCharge);
  assert.ok(a.counterScore('archer_tower', 'rambeast') < b.counterScore('archer_tower', 'rambeast'), 'A charged structure hit must lower building survival and pull time.');
});

test('Small spells remove clustered bats while Meteor Shards cannot damage buildings', async () => {
  const a = await analyzer();
  assert.equal(a.canDamage('bullet_burst', 'vampire_bats'), true);
  assert.ok(a.counterScore('bullet_burst', 'vampire_bats') >= 95);
  assert.ok(a.counterScore('bullet_burst', 'twin_blades') < 60);
  assert.equal(a.canDamage('meteor_shards', 'archer_tower'), false);
  assert.equal(a.counterScore('meteor_shards', 'archer_tower'), 0);
  assert.equal(a.canDamage('nova_flask', 'archer_tower'), true);
  assert.equal(a.canDamage('bullet_burst', 'meteor_shards'), false);
  assert.ok(a.cardIntel('meteor_shards').suggestedUses.some(text => text.includes('no structure damage')));
  for (const spell of ['bullet_burst', 'nova_flask', 'meteor_shards']) {
    const intel = a.cardIntel(spell);
    assert.ok(intel.bestDefensiveAnswer.reason.includes('Survives'));
    assert.equal(a.counterScore(intel.bestDefensiveAnswer.id, spell), 0, 'Spell resistance must not masquerade as cancellation.');
  }
});

test('Splash, refreshing Frost slow and Meteor DoT derive value from real mechanic fields', async () => {
  const a = await analyzer();
  assert.ok(a.counterScore('arc_mage', 'vampire_bats') > a.counterScore('ember_archer', 'vampire_bats'));
  const noSlow = structuredClone(cards); noSlow.frost_fang.slowPct = 0;
  const b = await analyzer(noSlow);
  assert.ok(a.counterScore('frost_fang', 'rambeast') > b.counterScore('frost_fang', 'rambeast'));
  assert.ok(a.synergy('frost_fang', 'meteor_shards').score > b.synergy('frost_fang', 'meteor_shards').score);
  const noDot = structuredClone(cards); noDot.meteor_shards.dotDamage = 0;
  const c = await analyzer(noDot);
  assert.ok(a.counterScore('meteor_shards', 'ember_archer') > c.counterScore('meteor_shards', 'ember_archer'));
});

test('All 91 unique pairs are symmetric, finite, explained and within 0–100', async () => {
  const a = await analyzer();
  for (let i = 0; i < ids.length; i++) for (let j = i + 1; j < ids.length; j++) {
    const x = a.synergy(ids[i], ids[j]), y = a.synergy(ids[j], ids[i]);
    assert.equal(x.score, y.score); assert.ok(x.score >= 0 && x.score <= 100);
    assert.ok(x.reasons.length && x.reasons.every(reason => typeof reason === 'string' && reason.length > 10));
  }
  assert.equal(a.synergy('ironclad', 'ironclad').score, 0);
  assert.ok(a.synergy('boulderback', 'arc_mage').score > a.synergy('boulderback', 'rambeast').score);
  assert.ok(a.synergy('rambeast', 'ember_archer').score >= 75);
  assert.ok(a.synergy('frost_fang', 'storm_raven').score >= 70);
  assert.ok(a.synergy('archer_tower', 'bullet_burst').score >= 60);
});

test('All 14 card details have roles, uses, counters, partners and both recommendations', async () => {
  const a = await analyzer();
  for (const id of ids) {
    const intel = a.cardIntel(id);
    assert.equal(intel.id, id); assert.ok(intel.role && intel.suggestedUses.length);
    assert.ok(intel.bestAgainst.length && intel.weakAgainst.length && intel.partners.length);
    assert.ok(intel.bestDefensiveAnswer && intel.bestOffensivePartner);
    for (const answer of intel.bestAgainst) assert.ok(a.canDamage(id, answer.id), `${id} must reach ${answer.id}`);
    for (const partner of intel.partners) assert.ok(partner.id !== id && cards[partner.id]);
    allFinite(intel);
  }
  const returned = a.cardIntel('boulderback'); returned.partners[0].score = -999;
  assert.ok(a.cardIntel('boulderback').partners[0].score > 0, 'UI mutation must not corrupt cached intelligence.');
});

for (const style of ['beatdown', 'aggro', 'control', 'cycle', 'split', 'spell_cycle', 'counter']) {
  test(`${style} builds 50 coherent, unique 8-card decks with real personality constraints`, async () => {
    const a = await analyzer(), seen = new Set(), rng = seeded(4321);
    for (let iteration = 0; iteration < 50; iteration++) {
      const deck = a.buildAiDeck(style, rng), r = a.analyzeDeck(deck), c = r.counts, m = r.metrics;
      assert.equal(deck.length, 8); assert.equal(new Set(deck).size, 8); assert.ok(deck.every(id => cards[id]));
      assert.ok(r.valid && c.winConditions >= 1 && c.spells >= 1 && c.antiAir >= 3 && m.sustainedAntiAir >= 2 && m.defensiveTroops >= 3 && m.rangedSupport >= 1 && m.cheap >= 2);
      if (style === 'beatdown') assert.ok(m.heavyWinConditions >= 1 && m.rangedSupport >= 2);
      if (style === 'aggro') assert.ok(m.fastWinConditions >= 1 && m.fast >= 3 && m.cheap >= 3 && r.averages.cost <= 3.875);
      if (style === 'control') assert.ok(c.buildings && m.defense >= 65);
      if (style === 'cycle') assert.ok(m.cheap >= 4 && r.averages.cost <= 3.5 && r.archetypes.includes('Cycle'));
      if (style === 'split') assert.ok(c.winConditions >= 2 && m.fast >= 2 && r.archetypes.includes('Split Lane'));
      if (style === 'spell_cycle') assert.ok(c.spells >= 2 && m.towerSpells >= 2 && c.buildings && m.cheap >= 3);
      if (style === 'counter') assert.ok(c.buildings && m.frontline >= 2 && m.defensiveTroops >= 4);
      seen.add(deck.slice().sort().join(',')); allFinite(r);
    }
    assert.ok(seen.size > 1, 'Top-scoring legal candidates should retain meaningful deck variety.');
  });
}

test('Seeded AI generation is reproducible, aliases work, and RNG edge values remain legal', async () => {
  const a = await analyzer(), b = await analyzer();
  assert.deepEqual(a.buildAiDeck('beatdown', seeded(8)), b.buildAiDeck('beatdown', seeded(8)));
  for (const name of ['Split Lane', 'Spell Cycle', 'Counter Push', 'unknown']) {
    for (const value of [0, 1, -1, Infinity, NaN]) assert.ok(a.analyzeDeck(a.buildAiDeck(name, () => value)).valid);
  }
});

test('Matchup scores are antisymmetric mechanical advantage, with usable threats and counters', async () => {
  const a = await analyzer(), opponent = a.buildAiDeck('cycle', seeded(77));
  const forward = a.matchup(defaultDeck, opponent), reverse = a.matchup(opponent, defaultDeck);
  assert.equal(forward.score + reverse.score, 0);
  assert.equal(a.matchup(defaultDeck, defaultDeck).score, 0);
  assert.ok(forward.label && forward.laneStrategy && forward.disclaimer.includes('not a win rate'));
  assert.ok(forward.threats.length && forward.counters.length && forward.reasons.length);
  for (const counter of forward.counters) assert.ok(defaultDeck.includes(counter.id) && opponent.includes(counter.againstId) && a.canDamage(counter.id, counter.againstId));
});

test('All ten archetype labels arise from legal compositions rather than card pair lookup tables', async () => {
  const a = await analyzer(), observed = new Set();
  const choose = (offset, deck) => {
    if (deck.length === 8) { a.analyzeDeck(deck).archetypes.forEach(label => observed.add(label)); return; }
    for (let i = offset; i <= ids.length - (8 - deck.length); i++) choose(i + 1, [...deck, ids[i]]);
  };
  choose(0, []);
  for (const label of ['Beatdown', 'Control', 'Cycle', 'Bridge Pressure', 'Split Lane', 'Air Pressure', 'Siege', 'Spell Control', 'Defensive', 'Hybrid']) assert.ok(observed.has(label), label);
});
