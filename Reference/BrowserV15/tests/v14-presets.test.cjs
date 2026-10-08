const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

const root = process.env.RIFT_GAME_ROOT || process.env.RIFT_PROJECT_ROOT
  || (fs.existsSync(path.resolve(__dirname, '../src/game.js')) ? path.resolve(__dirname, '..') : path.resolve(__dirname, '../rift_crown_arena_v14'));
const game = fs.readFileSync(path.join(root, 'src/game.js'), 'utf8');
const context = vm.createContext({});
vm.runInContext(game.slice(game.indexOf('const CARD_LIBRARY ='), game.indexOf('const DECK_SIZE'))
  + '\nglobalThis.cards = CARD_LIBRARY; globalThis.deck = DEFAULT_DECK;', context);
const cards = context.cards, deck = Array.from(context.deck);
const alternate = Object.keys(cards).slice(-8);
const moduleSource = fs.readFileSync(path.join(root, 'src/deck-presets.js'), 'utf8');
const storePromise = import(`data:text/javascript;base64,${Buffer.from(moduleSource).toString('base64')}`)
  .then(module => module.createPresetStore(cards, deck));

test('V13 deck migration creates exactly five valid isolated presets', async () => {
  const store = await storePromise, state = store.normalize(null, alternate);
  assert.equal(state.activePresetId, 'deck-1');
  assert.equal(state.presets.length, 5);
  assert.deepEqual(state.presets.map(p => p.id), ['deck-1', 'deck-2', 'deck-3', 'deck-4', 'deck-5']);
  assert.deepEqual(state.presets[0].cards, alternate);
  assert.deepEqual(state.presets.slice(1).map(p => p.cards), [deck, deck, deck, deck]);
  assert.ok(state.presets.every(p => store.validateDeck(p.cards)));
  state.presets[0].cards[0] = 'changed';
  assert.deepEqual(alternate, Object.keys(cards).slice(-8));
  assert.equal(state.presets[1].cards[0], deck[0]);
});

test('all five custom preset names, cards and active selection survive JSON save/load', async () => {
  const store = await storePromise;
  let state = store.createDefaultPresets();
  for (let i = 1; i <= 5; i++) {
    const pool = Object.keys(cards);
    const ownDeck = [...pool.slice(i), ...pool.slice(0, i)].slice(0, 8);
    state = store.update(state, `deck-${i}`, { name: `Warden ${i}`, cards: ownDeck });
    state = store.select(state, `deck-${i}`);
    assert.equal(state.activePresetId, `deck-${i}`);
  }
  assert.deepEqual(store.normalize(JSON.parse(JSON.stringify(state))), state);
  assert.deepEqual(state.presets.map(p => p.name), ['Warden 1', 'Warden 2', 'Warden 3', 'Warden 4', 'Warden 5']);
});

test('validation refuses wrong sizes, duplicates, unknown IDs, prototypes and coercion', async () => {
  const store = await storePromise;
  const invalid = [null, {}, 'ironclad', deck.slice(1), [...deck, 'twin_blades'], deck.map(() => deck[0]),
    ['__proto__', ...deck.slice(1)], ['constructor', ...deck.slice(1)], ['toString', ...deck.slice(1)],
    ['not-a-card', ...deck.slice(1)], [{ toString: () => deck[0] }, ...deck.slice(1)]];
  assert.equal(store.validateDeck(deck), true);
  for (const value of invalid) assert.equal(store.validateDeck(value), false);
});

test('normalization repairs corrupt saves without discarding good named decks', async () => {
  const store = await storePromise;
  const state = store.normalize({ activePresetId: 'deck-4', presets: [
    { id: 'deck-4', name: 'Air Pressure', cards: alternate },
    { id: 'deck-4', name: 'Duplicate overwriter', cards: deck },
    { id: 'deck-1', name: '<script>unsafe & name</script>', cards: deck.slice(1) },
    { id: 'deck-2', name: '\n\t', cards: ['constructor', ...deck.slice(1)] },
    { id: '__proto__', name: 'polluted', cards: alternate },
    { id: 'deck-6', name: 'extra', cards: alternate }, null, []], extra: true }, alternate);
  assert.equal(state.activePresetId, 'deck-4');
  assert.equal(state.presets.length, 5);
  assert.equal(state.presets[3].name, 'Air Pressure');
  assert.deepEqual(state.presets[3].cards, alternate);
  assert.deepEqual(state.presets[0].cards, alternate);
  assert.equal(state.presets[1].name, 'Deck 2');
  assert.deepEqual(Object.keys(state), ['activePresetId', 'presets']);
  assert.ok(state.presets.every(p => store.validateDeck(p.cards) && !/[<>"'&`\x00-\x1f]/.test(p.name)));
});

test('names are trimmed, safe, nonempty and limited to 24 Unicode characters', async () => {
  const store = await storePromise;
  assert.equal(store.sanitizeName('   Frost   &   Storm\n '), 'Frost Storm');
  assert.equal(store.sanitizeName('<>"\'&`\u0000', 'Deck 1'), 'Deck 1');
  assert.equal(Array.from(store.sanitizeName('👑'.repeat(30))).length, 24);
  assert.equal(store.update(store.createDefaultPresets(), 'deck-1', { name: '   ' }), null);
  assert.equal(store.update(store.createDefaultPresets(), 'deck-1', { name: {} }), null);
});

test('preset edits/selects are immutable and invalid operations preserve the original', async () => {
  const store = await storePromise, original = store.createDefaultPresets();
  const snapshot = JSON.stringify(original);
  const updated = store.update(original, 'deck-3', { name: 'Custom', cards: alternate });
  const selected = store.select(updated, 'deck-3');
  assert.equal(selected.activePresetId, 'deck-3');
  assert.equal(updated.activePresetId, 'deck-1');
  assert.equal(JSON.stringify(original), snapshot);
  assert.equal(store.select(original, '__proto__'), null);
  assert.equal(store.update(original, 'deck-6', { cards: deck }), null);
  assert.equal(store.update(original, 'deck-1', { cards: deck.slice(1) }), null);
  updated.presets[0].cards[0] = 'changed';
  assert.equal(selected.presets[0].cards[0], deck[0]);
});

test('missing/corrupt preset data and invalid legacy saves always use valid defaults', async () => {
  const store = await storePromise;
  for (const value of [undefined, null, 7, 'old', [], {}, { presets: {} }, { activePresetId: 'constructor', presets: [] }]) {
    const state = store.normalize(value, ['bad']);
    assert.equal(state.activePresetId, 'deck-1');
    assert.ok(state.presets.every(p => store.validateDeck(p.cards)));
  }
});

test('a duplicate valid entry can recover an earlier corrupt entry', async () => {
  const store = await storePromise;
  const state = store.normalize({ presets: [
    { id: 'deck-2', name: 'Broken', cards: ['bad'] },
    { id: 'deck-2', name: 'Recovered', cards: alternate },
  ] });
  assert.equal(state.presets[1].name, 'Recovered');
  assert.deepEqual(state.presets[1].cards, alternate);
});
