// The workshop is a view of the caller's draft. Saves and match starts stay in game.js.
const DECK_SIZE = 8;
const workspaceState = new WeakMap();
const escapeHtml = value => String(value ?? '').replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[character]));
const artPath = id => `./assets/ui/${encodeURIComponent(id)}.svg`;
const number = (value, digits = 0) => Number.isFinite(Number(value)) ? Number(value).toLocaleString(undefined, { maximumFractionDigits: digits, minimumFractionDigits: digits }) : '—';
const query = id => document.getElementById(id);

function cardName(analyzer, id) {
  return analyzer.cards?.[id]?.name || id?.split('_').map(word => word.charAt(0).toUpperCase() + word.slice(1)).join(' ') || 'None';
}

function noteList(items, fallback) {
  return `<ul class="deck-note-list">${items?.length ? items.map(item => `<li>${escapeHtml(item)}</li>`).join('') : `<li class="deck-note-muted">${escapeHtml(fallback)}</li>`}</ul>`;
}

function intelMatchups(analyzer, entries, limit = 3) {
  if (!entries?.length) return '<p class="intel-empty">No meaningful direct matchup in this card pool.</p>';
  return `<ul class="intel-matchups">${entries.slice(0, limit).map(entry => `<li><div><strong>${escapeHtml(cardName(analyzer, entry.id))}</strong><span>${number(entry.score)} / 100</span></div><p>${escapeHtml(entry.reason)}</p></li>`).join('')}</ul>`;
}

function intelSingle(analyzer, entry) {
  if (!entry) return '<p class="intel-empty">No suitable direct answer in this card pool.</p>';
  return `<p class="intel-answer"><strong>${escapeHtml(cardName(analyzer, entry.id))}</strong> <span>${number(entry.score)} / 100</span></p><p class="intel-reason">${escapeHtml(entry.reason)}</p>`;
}

/** Append a compact strategy disclosure to the original live-stat card details. */
export function renderCardIntelligence(analyzer, cardId, container) {
  if (!container) return;
  const intel = analyzer.cardIntel(cardId);
  container.querySelector('.card-intelligence')?.remove();
  if (!intel) return;
  const section = document.createElement('section');
  section.className = 'card-intelligence';
  section.innerHTML = `<div class="card-role"><span>ROLE</span><strong>${escapeHtml(intel.role)}</strong></div>
    <details class="card-strategy-details">
      <summary>STRATEGY &amp; MATCHUPS <span>Expand field notes</span></summary>
      <div class="intel-section"><h5>SUGGESTED USES</h5>${noteList(intel.suggestedUses, 'Use its targeting and cost to cover the current threat.')}</div>
      <div class="intel-two-col">
        <div class="intel-section"><h5>BEST AGAINST</h5>${intelMatchups(analyzer, intel.bestAgainst)}</div>
        <div class="intel-section"><h5>WEAK AGAINST · COMMON COUNTERS</h5>${intelMatchups(analyzer, intel.weakAgainst)}</div>
      </div>
      <div class="intel-section"><h5>GOOD PARTNERS · COMMON PAIRS</h5>${intelMatchups(analyzer, intel.partners)}</div>
      <div class="intel-two-col">
        <div class="intel-section"><h5>BEST DEFENSIVE ANSWER</h5>${intelSingle(analyzer, intel.bestDefensiveAnswer)}</div>
        <div class="intel-section"><h5>BEST OFFENSIVE PARTNER</h5>${intelSingle(analyzer, intel.bestOffensivePartner)}</div>
      </div>
      <p class="intel-score-note">Scores compare mechanics, cost and coverage. Placement and timing still decide the trade.</p>
    </details>`;
  container.appendChild(section);
}

/** Normal home preview contains only the player's composition. */
export function renderHomeDeckProfile(analyzer, ids) {
  const analysis = analyzer.analyzeDeck(ids);
  for (const id of ['home-deck-profile', 'home-battle-deck-profile']) {
    const container = query(id);
    if (container) container.innerHTML = `<small>DECK PROFILE</small><strong>${escapeHtml(analysis.profile)}</strong><p>${escapeHtml(analysis.strengths?.[0] || 'Complete your loadout to establish a strategy.')}</p>`;
  }
}

function renderAnalysis(container, analysis, cards) {
  if (!container) return;
  const counts = analysis.counts;
  const metrics = [
    ['Troops', counts.troops], ['Spells', counts.spells], ['Buildings', counts.buildings],
    ['Air-targeting', counts.antiAir], ['Ground-only', counts.groundOnly], ['Win conditions', counts.winConditions],
    ['Splash / area', counts.splash], ['Swarms', counts.swarm],
    ['Average HP', number(analysis.averages.hp, 1)], ['Average DPS', number(analysis.averages.dps, 1)],
    ['Average range', `${number(analysis.averages.range, 2)} tiles`],
  ];
  const curve = [2, 3, 4, 5, 6].map(cost => ({ cost, count: analysis.ids.filter(id => cards[id]?.cost === cost).length }));
  container.innerHTML = `<div class="deck-analysis-heading"><small>DECK PROFILE</small><h3>${escapeHtml(analysis.profile)}</h3><div class="deck-archetypes">${analysis.archetypes.map(label => `<span>${escapeHtml(label)}</span>`).join('')}</div></div>
    <dl class="deck-metric-list">${metrics.map(([label, value]) => `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd></div>`).join('')}</dl>
    <div class="deck-cost-curve"><h4>AETHER CURVE</h4><div>${curve.map(({ cost, count }) => `<span title="${count} ${count === 1 ? 'card' : 'cards'} at ${cost} Aether"><b>${count}</b><i style="--curve-height:${Math.max(2, count * 9)}px"></i><small>${cost}</small></span>`).join('')}</div></div>
    <p class="deck-metric-note">HP, DPS and range average troops + buildings. Swarm HP / DPS include all units; spells are excluded. Air-targeting includes spells and Storm Raven's short aura.</p>
    <div class="deck-assessment"><h4>STRENGTHS</h4>${noteList(analysis.strengths, 'Add more cards to establish coverage.')}<h4>WEAKNESSES</h4>${noteList(analysis.weaknesses, 'No major composition gap identified.')}</div>`;
}

function pairRows(analyzer, pairs) {
  return `<ol class="deck-pair-list">${pairs.map(pair => `<li><div class="deck-pair-title"><strong>${escapeHtml(cardName(analyzer, pair.aId))} <i>+</i> ${escapeHtml(cardName(analyzer, pair.bId))}</strong><span>${number(pair.score)}<small> / 100</small></span></div><p>${escapeHtml(pair.reasons.join(' · '))}</p></li>`).join('')}</ol>`;
}

function renderSynergies(container, analyzer, analysis) {
  if (!container) return;
  const pairs = analysis.synergies;
  const isOpen = container.querySelector('details')?.open || false;
  container.innerHTML = `<div class="loadout-section-title"><strong>CARD SYNERGY</strong><span>${pairs.length} pairs · 0–100</span></div>${pairs.length ? `${pairRows(analyzer, pairs.slice(0, 4))}${pairs.length > 4 ? `<details class="deck-all-pairs"${isOpen ? ' open' : ''}><summary>VIEW ALL ${pairs.length} PAIRS</summary>${pairRows(analyzer, pairs.slice(4))}</details>` : ''}` : '<p class="deck-note-muted">Add two cards to compare their mechanics.</p>'}<p class="deck-metric-note">Analytical compatibility, not a predicted win rate.</p>`;
}

function inspectCard(container, analyzer, cards, id) {
  if (!container || !cards[id]) return;
  const card = cards[id];
  container.classList.remove('hidden');
  const hp = card.hp ? `${number(card.hp)}${card.count > 1 ? ` × ${card.count}` : ''}` : '—';
  const dps = card.attackSpeed ? number(card.damage / card.attackSpeed * (card.count || 1), 1) : '—';
  container.innerHTML = `<div class="loadout-inspection-head"><img src="${artPath(id)}" alt=""><div><small>CARD INTELLIGENCE</small><h3>${escapeHtml(card.name)}</h3><p>${escapeHtml(card.desc)}</p></div><button type="button" aria-label="Close card inspection">×</button></div>
    <dl class="inspection-stats"><div><dt>Aether</dt><dd>${card.cost}</dd></div><div><dt>HP</dt><dd>${hp}</dd></div><div><dt>Deployment DPS</dt><dd>${dps}</dd></div><div><dt>${card.spell ? 'Radius' : 'Range'}</dt><dd>${number(card.radius ?? card.range, 2)} tiles</dd></div></dl>`;
  container.querySelector('button').onclick = () => container.classList.add('hidden');
  renderCardIntelligence(analyzer, id, container);
  container.querySelector('details').open = true;
}

/** Render the complete workshop; callbacks own all game state and persistence. */
export function renderDeckWorkspace({ cards, analyzer, draft, presets, selectedPresetId, presetName, onDraftChange, onSelectPreset, onNameChange, onSave, onTest, onReset }) {
  const modal = query('loadout-modal');
  const slots = query('loadout-slots');
  const pool = query('loadout-card-pool');
  if (!modal || !slots || !pool) return;
  const analysis = analyzer.analyzeDeck(draft);
  const state = workspaceState.get(modal) || { inspectedCardId: null };
  workspaceState.set(modal, state);
  const feedback = query('loadout-feedback');
  const announce = message => { if (feedback) feedback.textContent = message; };
  const toggleCard = id => {
    if (draft.includes(id)) onDraftChange(draft.filter(cardId => cardId !== id));
    else if (draft.length < DECK_SIZE) onDraftChange([...draft, id]);
    else announce('Deck is full — remove a card before adding another.');
  };
  slots.replaceChildren();
  for (let index = 0; index < DECK_SIZE; index += 1) {
    const id = draft[index];
    const card = cards[id];
    const slot = document.createElement('button');
    slot.type = 'button';
    slot.className = `loadout-slot ${card ? 'filled' : 'empty'}`;
    slot.dataset.index = index;
    slot.dataset.card = id || '';
    slot.disabled = !card;
    slot.setAttribute('aria-label', card ? `Remove ${card.name} from deck slot ${index + 1}` : `Empty deck slot ${index + 1}`);
    slot.innerHTML = card ? `<img src="${artPath(id)}" alt=""><span class="loadout-cost">${card.cost}</span><strong>${escapeHtml(card.name)}</strong><small>CLICK TO REMOVE</small>` : `<span class="slot-number">${index + 1}</span><strong>EMPTY</strong><small>CHOOSE A CARD</small>`;
    if (card) slot.onclick = () => onDraftChange(draft.filter((_, draftIndex) => draftIndex !== index));
    slots.appendChild(slot);
  }
  pool.replaceChildren();
  for (const [id, card] of Object.entries(cards)) {
    const selected = draft.includes(id);
    const wrapper = document.createElement('div');
    wrapper.className = 'loadout-pool-entry';
    wrapper.innerHTML = `<button type="button" class="loadout-pool-card${selected ? ' selected' : ''}" data-card="${escapeHtml(id)}" aria-pressed="${selected}" aria-label="${selected ? 'Remove' : 'Add'} ${escapeHtml(card.name)}"><div class="pool-art"><img src="${artPath(id)}" alt=""><span>${card.cost}</span>${selected ? '<i>✓</i>' : ''}</div><strong>${escapeHtml(card.name)}</strong><small>${escapeHtml(card.category || card.type)}</small></button><button type="button" class="loadout-inspect-button" aria-label="Inspect ${escapeHtml(card.name)}">INSPECT</button>`;
    wrapper.querySelector('.loadout-pool-card').onclick = () => toggleCard(id);
    wrapper.querySelector('.loadout-inspect-button').onclick = () => {
      state.inspectedCardId = id;
      inspectCard(query('loadout-card-inspection'), analyzer, cards, id);
      query('loadout-card-inspection')?.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    };
    pool.appendChild(wrapper);
  }
  const presetBar = query('loadout-presets');
  presetBar?.replaceChildren();
  presets.forEach((preset, index) => {
    if (!presetBar) return;
    const button = document.createElement('button');
    button.type = 'button';
    button.dataset.preset = preset.id;
    button.className = `loadout-preset${preset.id === selectedPresetId ? ' selected' : ''}`;
    button.setAttribute('aria-pressed', String(preset.id === selectedPresetId));
    button.title = `Edit saved preset ${index + 1}: ${preset.name}`;
    button.innerHTML = `<small>DECK ${index + 1}${preset.id === selectedPresetId ? ' · EDITING' : ''}</small><strong>${escapeHtml(preset.name)}</strong>`;
    button.onclick = () => onSelectPreset(preset.id);
    presetBar.appendChild(button);
  });
  const nameInput = query('loadout-preset-name');
  if (nameInput) {
    nameInput.value = presetName ?? presets.find(preset => preset.id === selectedPresetId)?.name ?? '';
    nameInput.oninput = () => onNameChange(nameInput.value);
  }
  const count = query('loadout-count');
  const average = query('loadout-avg');
  if (count) count.textContent = `${draft.length} / ${DECK_SIZE}`;
  if (average) average.textContent = number(analysis.averages.cost, 1);
  const valid = analysis.valid && draft.length === DECK_SIZE && new Set(draft).size === DECK_SIZE;
  const saveButton = query('loadout-save');
  const testButton = query('loadout-test');
  if (saveButton) { saveButton.disabled = !valid; saveButton.onclick = () => { if (valid) onSave(); }; }
  if (testButton) { testButton.disabled = !valid; testButton.onclick = () => { if (valid) onTest(); }; }
  const resetButton = query('loadout-reset');
  if (resetButton) resetButton.onclick = () => onReset();
  announce(valid ? 'Ready — save to set your battle deck, or save and test in training.' : analysis.errors?.length ? analysis.errors.join(' ') : `Choose ${Math.max(0, DECK_SIZE - draft.length)} more unique cards to save or test.`);
  renderAnalysis(query('loadout-analysis'), analysis, cards);
  renderSynergies(query('loadout-synergies'), analyzer, analysis);
  if (state.inspectedCardId && !query('loadout-card-inspection')?.classList.contains('hidden')) inspectCard(query('loadout-card-inspection'), analyzer, cards, state.inspectedCardId);
}
