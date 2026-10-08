const assert = require('node:assert/strict');
const test = require('node:test');
const vm = require('node:vm');
const { makeContext, install, element, range } = require('./v14-harness.cjs');

function context(extra = {}) {
  const c = makeContext(extra);
  c.activeDeck = Array.from(c.defaultDeck);
  c.deckPresetState = c.presetStore.createDefaultPresets(c.activeDeck);
  c.loadoutDraft = c.activeDeck.slice(); c.loadoutPresetId = 'deck-1'; c.presetNameDraft = 'Deck 1';
  c.playerProfile = { username: 'LOCAL', wins: 2, matches: 3, crowns: 4 };
  c.persistentSaveRevision = 0; c.serverSavePending = Promise.resolve();
  c.ui.loadoutModal = element();
  c.renderHomeDeck = () => {}; c.renderLoadoutBuilder = () => {}; c.renderProfile = () => {};
  c.fetch = extra.fetch || (async () => ({ ok: true, json: async () => ({}) }));
  c.labReady = Promise.resolve(); c.game.started = false; c.leaveBattle = () => c.serverSavePending;
  install(c, ['newLocalPlayerId', 'defaultProfile', 'normalizeProfile', 'loadSavedDeck', 'loadDeckPresetState', 'saveDeckStorage', 'syncLoadoutDraft', 'saveProfile', 'saveLoadout', 'testDeck', 'persistServerSave', 'hydratePersistentSave', 'hasPendingLocalSave']);
  return c;
}

test('actual V14 saveLoadout persists every preset and keeps legacy deck/profile keys', async () => {
  const writes = [], c = context({ fetch: async (_, options) => { writes.push(JSON.parse(options.body)); return { ok: true }; } });
  const pool = Object.keys(c.cards);
  for (let i = 1; i <= 5; i++) {
    c.loadoutPresetId = `deck-${i}`; c.presetNameDraft = `Preset ${i}`;
    c.loadoutDraft = [...pool.slice(i), ...pool.slice(0, i)].slice(0, 8);
    assert.equal(c.saveLoadout(), true);
    await c.serverSavePending;
    assert.equal(c.deckPresetState.activePresetId, `deck-${i}`);
    assert.deepEqual(JSON.parse(c.localStorage.getItem('rift_crown_deck_v1')), Array.from(c.loadoutDraft));
    assert.deepEqual(Array.from(c.game.playerDeck.hand), Array.from(c.loadoutDraft.slice(0, 4)));
  }
  const browserSave = JSON.parse(c.localStorage.getItem('rift_crown_deck_presets_v14'));
  assert.deepEqual(browserSave.presets.map(p => p.name), ['Preset 1', 'Preset 2', 'Preset 3', 'Preset 4', 'Preset 5']);
  assert.deepEqual(Object.keys(writes[4]), ['profile', 'deck', 'deckPresets']);
  assert.deepEqual(writes[4].deckPresets, browserSave);
  assert.equal(c.hasPendingLocalSave(), false);
});

test('invalid live builder drafts/names reject saving and testing without affecting active deck', async () => {
  const c = context(); let starts = 0; c.startMatch = () => starts++;
  for (const draft of [c.activeDeck.slice(1), [...c.activeDeck, 'twin_blades'], c.activeDeck.map(() => 'ironclad'), ['constructor', ...c.activeDeck.slice(1)]]) {
    c.loadoutDraft = draft; assert.equal(c.saveLoadout(), false); await c.testDeck();
  }
  c.loadoutDraft = c.activeDeck.slice(); c.presetNameDraft = '  <>  '; assert.equal(c.saveLoadout(), false);
  assert.deepEqual(Array.from(c.activeDeck), Array.from(c.defaultDeck)); assert.equal(starts, 0); assert.equal(c.persistentSaveRevision, 0);
});

test('actual TEST DECK saves selected preset and immediately opens training from home', async () => {
  const c = context(); const starts = []; c.startMatch = training => starts.push({ training, deck: c.activeDeck.slice() });
  c.loadoutDraft = Object.keys(c.cards).slice(-8); c.loadoutPresetId = 'deck-4'; c.presetNameDraft = 'Air Trial';
  await c.testDeck(); await c.serverSavePending;
  assert.equal(starts.length, 1); assert.equal(starts[0].training, true);
  assert.deepEqual(starts[0].deck, c.loadoutDraft); assert.equal(c.deckPresetState.activePresetId, 'deck-4');
});

test('TEST DECK waits for queued server save before reloading an existing match', async () => {
  let release, requests = 0; const response = new Promise(resolve => { release = resolve; });
  const c = context({ fetch: async () => { requests++; await response; return { ok: true }; } });
  const events = []; c.sessionStorage = { setItem: (key, value) => events.push([key, value]) }; c.location = { reload: () => events.push('reload') };
  c.game.started = true; const hand = c.game.playerDeck;
  c.loadoutDraft = Object.keys(c.cards).slice(-8);
  const pending = c.testDeck(); await Promise.resolve(); await Promise.resolve();
  assert.equal(requests, 1); assert.deepEqual(events, []); assert.equal(c.game.playerDeck, hand);
  release(); await pending;
  assert.deepEqual(events, [['riftAutostart', 'training'], 'reload']);
});

test('actual hydration restores all five names/cards and selected deck before battle', async () => {
  const c = context(), pool = Object.keys(c.cards);
  let saved = c.presetStore.createDefaultPresets();
  for (let i = 1; i <= 5; i++) saved = c.presetStore.update(saved, `deck-${i}`, { name: `Server ${i}`, cards: [...pool.slice(i), ...pool.slice(0, i)].slice(0, 8) });
  saved = c.presetStore.select(saved, 'deck-3'); c.fetch = async () => ({ ok: true, json: async () => ({ profile: { username: 'SERVER', wins: 7 }, deck: saved.presets[2].cards, deckPresets: saved }) });
  await c.hydratePersistentSave();
  assert.equal(c.playerProfile.username, 'SERVER'); assert.equal(c.playerProfile.wins, 7);
  assert.deepEqual(c.deckPresetState, saved); assert.deepEqual(c.activeDeck, saved.presets[2].cards);
  assert.equal(c.loadoutPresetId, 'deck-3'); assert.equal(c.presetNameDraft, 'Server 3');
  assert.deepEqual(Array.from(c.game.playerDeck.hand), saved.presets[2].cards.slice(0, 4));
});

test('V13 server saves migrate deck while preserving existing custom preset records', async () => {
  const c = context(), legacy = Object.keys(c.cards).slice(-8);
  c.deckPresetState = c.presetStore.update(c.deckPresetState, 'deck-5', { name: 'Keep Me', cards: legacy });
  c.fetch = async () => ({ ok: true, json: async () => ({ profile: { username: 'V13' }, deck: legacy }) });
  await c.hydratePersistentSave();
  assert.deepEqual(c.activeDeck, legacy); assert.equal(c.deckPresetState.presets[4].name, 'Keep Me');
  assert.equal(c.deckPresetState.presets.length, 5); assert.equal(c.playerProfile.username, 'V13');
});

test('late server hydration cannot replace newer local preset edits or current battle/draft', async () => {
  let release; const data = new Promise(resolve => { release = resolve; });
  const c = context({ fetch: async () => ({ ok: true, json: () => data }) });
  c.ui.loadoutModal.classList.contains = () => false; c.game.started = true;
  const hand = c.game.playerDeck, draft = c.activeDeck.slice(1); c.loadoutDraft = draft;
  const pending = c.hydratePersistentSave(); c.persistentSaveRevision++; c.playerProfile.username = 'NEW LOCAL';
  release({ profile: { username: 'OLD DISK' }, deck: Object.keys(c.cards).slice(-8) }); await pending;
  assert.equal(c.playerProfile.username, 'NEW LOCAL'); assert.equal(c.game.playerDeck, hand); assert.equal(c.loadoutDraft, draft);
});

test('hydration applies saved preset without changing an open builder draft or live hand', async () => {
  const c = context(); c.ui.loadoutModal.classList.contains = () => false; c.game.started = true;
  const hand = c.game.playerDeck, draft = c.activeDeck.slice(1); c.loadoutDraft = draft; c.presetNameDraft = 'Unsaved Work';
  c.fetch = async () => ({ ok: true, json: async () => ({ deck: Object.keys(c.cards).slice(-8) }) });
  await c.hydratePersistentSave(); assert.equal(c.game.playerDeck, hand); assert.equal(c.loadoutDraft, draft); assert.equal(c.presetNameDraft, 'Unsaved Work');
});

test('failed server save retains local pending marker; hydration retries newest local data', async () => {
  const c = context({ fetch: async () => ({ ok: false }) });
  c.loadoutDraft = Object.keys(c.cards).slice(-8); c.loadoutPresetId = 'deck-5'; c.presetNameDraft = 'Local Winner'; c.saveLoadout(); await c.serverSavePending;
  assert.equal(c.hasPendingLocalSave(), true);
  const local = JSON.stringify(c.deckPresetState), writes = [];
  c.fetch = async (_, options) => {
    if (options.method === 'POST') { writes.push(JSON.parse(options.body)); return { ok: true }; }
    return { ok: true, json: async () => ({ profile: { username: 'STALE' }, deck: c.defaultDeck }) };
  };
  await c.hydratePersistentSave(); await c.serverSavePending;
  assert.equal(c.playerProfile.username, 'LOCAL'); assert.equal(JSON.stringify(c.deckPresetState), local);
  assert.equal(writes[0].deckPresets.presets[4].name, 'Local Winner'); assert.equal(c.hasPendingLocalSave(), false);
});

test('server save queue snapshots records in order and only newest successful write clears marker', async () => {
  let release; const first = new Promise(resolve => { release = resolve; }); const writes = [];
  const c = context({ fetch: async (_, options) => { writes.push(JSON.parse(options.body)); if (writes.length === 1) await first; return { ok: true }; } });
  const old = c.persistServerSave(); await Promise.resolve(); await Promise.resolve();
  c.playerProfile.username = 'NEWER'; c.deckPresetState = c.presetStore.update(c.deckPresetState, 'deck-2', { name: 'New Deck' }); const newer = c.persistServerSave();
  assert.equal(writes.length, 1); assert.equal(writes[0].profile.username, 'LOCAL'); assert.equal(c.hasPendingLocalSave(), true);
  release(); await Promise.all([old, newer]);
  assert.equal(writes.length, 2); assert.equal(writes[1].profile.username, 'NEWER'); assert.equal(writes[1].deckPresets.presets[1].name, 'New Deck'); assert.equal(c.hasPendingLocalSave(), false);
});

test('offline browser load uses local presets and rejects corrupt storage safely', () => {
  const c = context(); const valid = c.presetStore.select(c.presetStore.update(c.deckPresetState, 'deck-2', { name: 'Offline', cards: Object.keys(c.cards).slice(-8) }), 'deck-2');
  c.localStorage.setItem('rift_crown_deck_presets_v14', JSON.stringify(valid)); assert.equal(JSON.stringify(c.loadDeckPresetState()), JSON.stringify(valid));
  c.localStorage.setItem('rift_crown_deck_presets_v14', '{broken'); assert.equal(c.loadDeckPresetState().presets.length, 5);
  c.localStorage.setItem('rift_crown_deck_v1', JSON.stringify(['constructor', ...c.defaultDeck.slice(1)])); assert.deepEqual(Array.from(c.loadSavedDeck()), Array.from(c.defaultDeck));
});

for (const [buttonName, training] of [['homeBattle', false], ['training', true]]) {
  test(`manual ${buttonName} waits for server preset hydration before starting a battle`, async () => {
    let release; const readiness = new Promise(resolve => { release = resolve; });
    const c = context(), callbacks = {}, starts = [];
    c.persistentSaveReady = readiness; c.startMatch = value => starts.push({ training: value, deck: c.activeDeck.slice() });
    for (const key of ['homeBattle', 'training']) c.ui[key] = { addEventListener: (_, callback) => { callbacks[key] = callback; } };
    vm.runInContext(range('ui.homeBattle?.addEventListener', 'ui.openDeck?.addEventListener'), c);
    const pending = callbacks[buttonName](); await Promise.resolve(); assert.equal(starts.length, 0);
    const restoredDeck = Object.keys(c.cards).slice(-8); c.activeDeck = restoredDeck;
    release(); await pending;
    assert.deepEqual(starts, [{ training, deck: restoredDeck }]);
  });
}
