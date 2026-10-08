// Save format is independent of UI state and preserves the V13 eight-card deck.
export function createPresetStore(cards, defaultDeck) {
  const size = 8;
  const presetIds = Array.from({ length: 5 }, (_, i) => `deck-${i + 1}`);
  const reservedIds = new Set(['__proto__', 'prototype', 'constructor']);
  const validCardIds = new Set(Object.keys(cards).filter(id => !reservedIds.has(id)));

  function validateDeck(ids) {
    return Array.isArray(ids) && ids.length === size && new Set(ids).size === size
      && ids.every(id => typeof id === 'string' && validCardIds.has(id));
  }
  if (!validateDeck(defaultDeck)) throw new TypeError('A preset store requires a valid eight-card default deck.');
  const seedDeck = defaultDeck.slice();

  function sanitizeName(value, fallback = '') {
    if (typeof value !== 'string') return fallback;
    const clean = value.replace(/[\u0000-\u001f\u007f<>"'&`]/g, '').replace(/\s+/g, ' ').trim();
    return Array.from(clean).slice(0, 24).join('').trim() || fallback;
  }

  function createDefaultPresets(legacyDeck) {
    return {
      activePresetId: presetIds[0],
      presets: presetIds.map((id, i) => ({
        id, name: `Deck ${i + 1}`,
        cards: (i === 0 && validateDeck(legacyDeck) ? legacyDeck : seedDeck).slice(),
      })),
    };
  }

  function normalize(data, legacyDeck) {
    const state = createDefaultPresets(legacyDeck);
    if (!data || typeof data !== 'object' || Array.isArray(data)) return state;
    if (!Array.isArray(data.presets)) return state;
    const candidates = new Map();
    for (const preset of data.presets) {
      if (!preset || typeof preset !== 'object' || Array.isArray(preset)) continue;
      if (!presetIds.includes(preset.id)) continue;
      // A later duplicate can recover a corrupt entry, but cannot replace a valid deck.
      const previous = candidates.get(preset.id);
      if (!previous || (!validateDeck(previous.cards) && validateDeck(preset.cards))) candidates.set(preset.id, preset);
    }
    state.presets = state.presets.map(fallback => {
      const preset = candidates.get(fallback.id);
      if (!preset) return fallback;
      return {
        id: fallback.id,
        name: sanitizeName(preset.name, fallback.name),
        cards: (validateDeck(preset.cards) ? preset.cards : fallback.cards).slice(),
      };
    });
    if (presetIds.includes(data.activePresetId)) state.activePresetId = data.activePresetId;
    return state;
  }

  function update(data, presetId, patch) {
    if (!presetIds.includes(presetId) || !patch || typeof patch !== 'object' || Array.isArray(patch)) return null;
    const name = Object.hasOwn(patch, 'name') ? sanitizeName(patch.name) : undefined;
    if (name === '' || (Object.hasOwn(patch, 'cards') && !validateDeck(patch.cards))) return null;
    const state = normalize(data);
    const preset = state.presets.find(item => item.id === presetId);
    if (name !== undefined) preset.name = name;
    if (Object.hasOwn(patch, 'cards')) preset.cards = patch.cards.slice();
    return state;
  }

  function select(data, presetId) {
    if (!presetIds.includes(presetId)) return null;
    return { ...normalize(data), activePresetId: presetId };
  }

  return {
    validateDeck, sanitizeName, createDefaultPresets, normalize, update, select,
    renamePreset: (data, id, name) => update(data, id, { name }),
    updatePreset: (data, id, ids) => update(data, id, { cards: ids }),
    selectPreset: select,
  };
}
