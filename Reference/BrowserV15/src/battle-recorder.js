// Replays retain combat events and sampled positions. They never rerun AI or RNG.
export const REPLAY_FORMAT_VERSION = 1;
const TEAMS = ['player', 'enemy'];
const finite = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const positive = value => Math.max(0, finite(value));
const clone = value => JSON.parse(JSON.stringify(value));
const teamOK = team => TEAMS.includes(team);
const text = (value, max = 120) => String(value ?? '').slice(0, max);
const average = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0;

export const REPLAY_DEFINITIONS = Object.freeze({
  damage: 'Actual HP removed; overkill is excluded. Damage includes troops, deployed buildings, and Crown Towers.',
  leaked: 'Aether regeneration lost at the 10-Aether cap. Developer grants are recorded separately and excluded.',
  value: 'Most/least valuable cards rank actual damage per Aether spent. Unplayed cards are excluded; ties use tower damage.',
  push: 'Largest push is the maximum surviving deployed Aether on the opposing river half. Each swarm member contributes cost divided by member count, multiplied by its surviving HP fraction.',
  trade: 'Best defensive trade is enemy troop/building Aether killed on your river half by one deployment, minus that deployment’s cost. Swarm kills count cost divided by members; this is realized kill value, not predicted damage prevention.',
  advantage: 'Largest Aether advantage is the largest recorded bank difference. Hand cost is a time-weighted average of the four available cards.',
  crowns: 'A destroyed tower’s awarded crowns are credited by each card’s actual damage divided by that tower’s full HP. Tiebreaker drain and Developer damage have no card attribution, so card contributions may total less than the match score.',
  spell: 'Biggest spell value is actual hostile troop/building HP removed as a fraction of max HP, multiplied by per-entity deployment cost. Tower damage is reported separately; it has no invented Aether price.',
  playback: 'Combat, damage, deaths, and decisions are recorded events. Positions are linearly interpolated between recorded samples for smooth playback; AI is never simulated again.'
});

function cardRow(cardId) {
  return {cardId, uses: 0, aetherSpent: 0, damage: 0, troopDamage: 0, buildingDamage: 0, towerDamage: 0, kills: 0, deaths: 0, killedAether: 0, crownContribution: 0, damagePerAether: 0};
}
function teamRow(team) {
  return {team, aetherSpent: 0, aetherLeaked: 0, damage: 0, troopDamage: 0, buildingDamage: 0, towerDamage: 0, kills: 0, deaths: 0, cardsPlayed: 0, averageHandCost: 0, crownContribution: 0, cards: {}, mostValuable: null, leastValuable: null, biggestPush: null, biggestDefensiveTrade: null, largestAetherAdvantage: {value: 0, time: 0}};
}
function sampleTowerHp(state, team) {
  return (state.entities || []).filter(e => e.kind === 'tower' && e.team === team).reduce((sum, e) => sum + positive(e.hp), 0);
}

export function analyzeReplay(replay, cards = {}) {
  const teams = Object.fromEntries(TEAMS.map(team => [team, teamRow(team)]));
  const plays = new Map(), towerCredit = new Map(), destroyed = new Set(), deaths = new Set();
  const events = [...(replay.events || [])].sort((a, b) => a.time - b.time || a.seq - b.seq);
  const snapshots = [...(replay.snapshots || [])].sort((a, b) => a.time - b.time);
  const bookmarks = [], timeline = [];
  let firstTowerHit = false, firstTowerDestroyed = false, biggestSpell = null;
  const rowFor = (team, id) => teamOK(team) && Object.hasOwn(cards, id) ? (teams[team].cards[id] ||= cardRow(id)) : null;
  for (const event of events) {
    const d = event.data || {}, t = event.time;
    if (event.type === 'damage' && d.targetKind === 'tower' && positive(d.amount) > 0 && !firstTowerHit) { bookmarks.push({id: 'first-tower-damage', label: 'First Tower Damage', time: t}); firstTowerHit = true; }
    if (event.type === 'card_play' && teamOK(d.team) && Object.hasOwn(cards, d.cardId)) {
      const team = teams[d.team], row = rowFor(d.team, d.cardId), cost = positive(d.cost ?? cards[d.cardId].cost);
      team.cardsPlayed++; team.aetherSpent += cost; row.uses++; row.aetherSpent += cost;
      plays.set(String(d.playId ?? event.seq), {playId: String(d.playId ?? event.seq), cardId: d.cardId, team: d.team, time: t, cost, spell: !!cards[d.cardId].spell, hpValue: 0, towerDamage: 0, defensiveKilledValue: 0});
    } else if (event.type === 'aether_leak' && teamOK(d.team)) {
      teams[d.team].aetherLeaked += positive(d.amount);
    } else if (event.type === 'damage' && teamOK(d.sourceTeam)) {
      const amount = positive(d.amount), team = teams[d.sourceTeam], row = rowFor(d.sourceTeam, d.sourceCardId);
      team.damage += amount;
      const key = d.targetKind === 'tower' ? 'towerDamage' : d.targetKind === 'building' ? 'buildingDamage' : 'troopDamage';
      team[key] += amount;
      if (row) { row.damage += amount; row[key] += amount; }
      const play = plays.get(String(d.playId));
      if (play) {
        if (d.targetKind === 'tower') play.towerDamage += amount;
        else if (positive(d.targetMaxHp)) play.hpValue += Math.min(1, amount / positive(d.targetMaxHp)) * positive(d.targetCost ?? cards[d.targetCardId]?.cost) / Math.max(1, positive(d.targetCount ?? cards[d.targetCardId]?.count ?? 1));
      }
      if (d.targetKind === 'tower' && amount > 0) {
        if (!firstTowerHit) { bookmarks.push({id: 'first-tower-damage', label: 'First Tower Damage', time: t}); firstTowerHit = true; }
        const entry = towerCredit.get(String(d.targetId)) || {credits: new Map(), maxHp: 0}, credits = entry.credits;
        const creditKey = `${d.sourceTeam}:${d.sourceCardId || ''}`;
        credits.set(creditKey, (credits.get(creditKey) || 0) + amount); entry.maxHp = Math.max(entry.maxHp, positive(d.targetMaxHp)); towerCredit.set(String(d.targetId), entry);
      }
    } else if (event.type === 'death' && !deaths.has(String(d.targetId))) {
      deaths.add(String(d.targetId));
      if (d.targetKind !== 'tower') {
        if (teamOK(d.targetTeam)) { teams[d.targetTeam].deaths++; const victim = rowFor(d.targetTeam, d.targetCardId); if (victim) victim.deaths++; }
        if (teamOK(d.sourceTeam) && d.sourceTeam !== d.targetTeam) {
          teams[d.sourceTeam].kills++;
          const value = positive(d.targetCost ?? cards[d.targetCardId]?.cost) / Math.max(1, positive(d.targetCount ?? cards[d.targetCardId]?.count ?? 1));
          const killer = rowFor(d.sourceTeam, d.sourceCardId); if (killer) { killer.kills++; killer.killedAether += value; }
          const play = plays.get(String(d.playId));
          const ownHalf = d.sourceTeam === 'player' ? finite(d.z) >= 0 : finite(d.z) <= 0;
          if (play && ownHalf) play.defensiveKilledValue += value;
        }
      }
    } else if (event.type === 'tower_destroy' && !destroyed.has(String(d.targetId))) {
      destroyed.add(String(d.targetId));
      if (!firstTowerDestroyed) { bookmarks.push({id: 'first-tower-destroyed', label: 'First Tower Destroyed', time: t}); firstTowerDestroyed = true; }
      const entry = towerCredit.get(String(d.targetId)) || {credits: new Map(), maxHp: 0}, credits = entry.credits, total = Math.max(entry.maxHp, positive(d.targetMaxHp), [...credits.values()].reduce((a, b) => a + b, 0)), crowns = positive(d.crownsAwarded ?? 1);
      if (total) for (const [key, damage] of credits) {
        const [team, id] = key.split(':'), contribution = crowns * damage / total;
        teams[team].crownContribution += contribution; const row = rowFor(team, id); if (row) row.crownContribution += contribution;
      }
    } else if (event.type === 'phase' && d.phase === 'overtime' && !bookmarks.some(b => b.id === 'overtime-start')) bookmarks.push({id: 'overtime-start', label: 'Overtime Start', time: t});
  }
  const handTotals = {player: 0, enemy: 0}, handDurations = {player: 0, enemy: 0};
  for (let i = 0; i < snapshots.length; i++) {
    const sample = snapshots[i], state = sample.state || {}, duration = Math.max(0, Math.min(replay.duration, snapshots[i + 1]?.time ?? replay.duration) - sample.time);
    const advantage = finite(state.aether?.player) - finite(state.aether?.enemy);
    timeline.push({time: sample.time, aetherAdvantage: advantage, playerTowerHp: sampleTowerHp(state, 'player'), enemyTowerHp: sampleTowerHp(state, 'enemy')});
    for (const team of TEAMS) {
      const teamAdvantage = team === 'player' ? advantage : -advantage;
      if (teamAdvantage > teams[team].largestAetherAdvantage.value) teams[team].largestAetherAdvantage = {value: teamAdvantage, time: sample.time};
      const hand = (state.hands?.[team] || []).filter(id => cards[id]);
      if (hand.length) { handTotals[team] += average(hand.map(id => cards[id].cost)) * duration; handDurations[team] += duration; }
      const bodies = (state.entities || []).filter(e => e.team === team && e.kind === 'troop' && e.hp > 0 && (team === 'player' ? e.z < 0 : e.z > 0));
      const value = bodies.reduce((sum, e) => sum + positive(cards[e.cardId]?.cost) / Math.max(1, positive(e.count ?? cards[e.cardId]?.count ?? 1)) * Math.min(1, positive(e.hp) / Math.max(1, positive(e.maxHp))), 0);
      if (value > (teams[team].biggestPush?.value || 0)) teams[team].biggestPush = {time: sample.time, value, units: bodies.length, cardIds: [...new Set(bodies.map(e => e.cardId))]};
    }
  }
  for (const play of plays.values()) {
    if (play.spell && (play.hpValue > 0 || play.towerDamage > 0) && (!biggestSpell || play.hpValue > biggestSpell.value || (play.hpValue === biggestSpell.value && play.towerDamage > biggestSpell.towerDamage))) biggestSpell = {time: play.time, cardId: play.cardId, team: play.team, value: play.hpValue, towerDamage: play.towerDamage};
    if (play.defensiveKilledValue > 0) {
      const trade = {time: play.time, cardId: play.cardId, playId: play.playId, value: play.defensiveKilledValue - play.cost, killedAether: play.defensiveKilledValue, cost: play.cost};
      if (!teams[play.team].biggestDefensiveTrade || trade.value > teams[play.team].biggestDefensiveTrade.value) teams[play.team].biggestDefensiveTrade = trade;
    }
  }
  const biggestPush = TEAMS.map(t => teams[t].biggestPush && {...teams[t].biggestPush, team: t}).filter(Boolean).sort((a, b) => b.value - a.value)[0];
  if (biggestPush) bookmarks.push({id: 'largest-push', label: 'Largest Push', time: biggestPush.time});
  if (biggestSpell) bookmarks.push({id: 'biggest-spell-value', label: 'Biggest Spell Value', time: biggestSpell.time});
  for (const team of TEAMS) {
    teams[team].averageHandCost = handDurations[team] ? handTotals[team] / handDurations[team] : 0;
    const played = Object.values(teams[team].cards).filter(row => row.uses > 0);
    for (const row of played) row.damagePerAether = row.aetherSpent ? row.damage / row.aetherSpent : 0;
    played.sort((a, b) => b.damagePerAether - a.damagePerAether || b.towerDamage - a.towerDamage || a.cardId.localeCompare(b.cardId));
    teams[team].mostValuable = played[0]?.cardId || null; teams[team].leastValuable = played.at(-1)?.cardId || null;
  }
  bookmarks.push({id: 'match-end', label: 'Match End', time: positive(replay.duration)});
  bookmarks.sort((a, b) => a.time - b.time);
  return {teams, timeline, bookmarks, biggestSpell, definitions: REPLAY_DEFINITIONS};
}

function safeEntity(e) {
  if (!e || !teamOK(e.team) || !['troop', 'building', 'tower'].includes(e.kind)) return null;
  return {id: text(e.id, 80), team: e.team, kind: e.kind, cardId: text(e.cardId, 40), playId: text(e.playId, 80), hp: positive(e.hp), maxHp: positive(e.maxHp), x: finite(e.x), z: finite(e.z), targetId: e.targetId == null ? null : text(e.targetId, 80), count: Math.max(1, positive(e.count ?? 1)), dead: !!e.dead, flying: !!e.flying, towerKind: text(e.towerKind, 20), slowUntil: positive(e.slowUntil), slowPct: positive(e.slowPct), stunUntil: positive(e.stunUntil)};
}
function safeState(state = {}) {
  const array = value => Array.isArray(value) ? value : [];
  return {aether: Object.fromEntries(TEAMS.map(t => [t, Math.max(0, Math.min(10, finite(state.aether?.[t])))])), hands: Object.fromEntries(TEAMS.map(t => [t, array(state.hands?.[t]).slice(0, 4).map(id => text(id, 40))])), crowns: Object.fromEntries(TEAMS.map(t => [t, positive(state.crowns?.[t])])), phase: text(state.phase || 'regulation', 30), entities: array(state.entities).slice(0, 1500).map(safeEntity).filter(Boolean), hazards: array(state.hazards).filter(h => h && typeof h === 'object').slice(0, 150).map(h => ({id: text(h.id, 80), team: teamOK(h.team) ? h.team : 'player', cardId: text(h.cardId, 40), x: finite(h.x), z: finite(h.z), radius: positive(h.radius), remaining: positive(h.remaining)}))};
}
function serializable(value, depth = 0) {
  if (depth > 8) return null;
  if (value == null || typeof value === 'boolean') return value;
  if (typeof value === 'number') return Number.isFinite(value) ? value : 0;
  if (typeof value === 'string') return value.slice(0, 400);
  if (Array.isArray(value)) return value.slice(0, 1500).map(v => serializable(v, depth + 1));
  if (typeof value === 'object') return Object.fromEntries(Object.entries(value).filter(([key]) => !['__proto__', 'constructor', 'prototype'].includes(key)).slice(0, 120).map(([key, v]) => [key.slice(0, 80), serializable(v, depth + 1)]));
  return null;
}

export function validateReplay(input) {
  if (!input || input.formatVersion !== REPLAY_FORMAT_VERSION || !Array.isArray(input.events) || !Array.isArray(input.snapshots) || !Number.isFinite(input.duration) || input.duration < 0 || input.duration > 3600 || input.events.length > 100000 || input.snapshots.length > 16000) return null;
  if (!input.events.every(e => e && Number.isFinite(e.time) && e.time >= 0 && e.time <= input.duration + .01 && typeof e.type === 'string') || !input.snapshots.every(s => s && Number.isFinite(s.time) && s.time >= 0 && s.time <= input.duration + .01 && s.state && Array.isArray(s.state.entities))) return null;
  return {formatVersion: REPLAY_FORMAT_VERSION, id: text(input.id, 80) || `import-${Date.now()}`, version: text(input.version, 30), createdAt: text(input.createdAt, 40), duration: input.duration, metadata: serializable(input.metadata || {}), result: serializable(input.result || {}), events: input.events.map((e, index) => ({time: e.time, seq: Number.isFinite(e.seq) ? e.seq : index, type: text(e.type, 40), data: serializable(e.data || {})})).sort((a, b) => a.time - b.time || a.seq - b.seq), snapshots: input.snapshots.map(s => ({time: s.time, state: safeState(s.state)})).sort((a, b) => a.time - b.time)};
}

export function createBattleRecorder(cards, options = {}) {
  const version = options.version || 'V15', storageKey = options.storageKey || 'rift_crown_replays_v15';
  const maxReplays = Math.max(1, Math.min(30, finite(options.maxReplays, 8))), maxBytes = Math.max(50000, finite(options.maxBytes, 3800000));
  let storage = options.storage;
  if (storage === undefined) { try { storage = globalThis.localStorage; } catch { storage = null; } }
  let history = [], active = null, sequence = 0, error = null;
  function bounded(list) {
    const sorted = [...list].sort((a, b) => String(b.createdAt).localeCompare(String(a.createdAt))).slice(0, maxReplays);
    while (sorted.length > 1 && JSON.stringify(sorted).length * 2 > maxBytes) sorted.pop();
    return sorted;
  }
  function persist() {
    history = bounded(history); error = null;
    if (!storage) return;
    for (;;) {
      try { storage.setItem(storageKey, JSON.stringify({formatVersion: REPLAY_FORMAT_VERSION, replays: history})); return; }
      catch (e) { error = 'Replay storage is full or unavailable. The newest replay remains available in this session.'; if (history.length > 1) history.pop(); else return; }
    }
  }
  function hydrate(data, save = true) {
    const values = Array.isArray(data) ? data : data?.replays;
    if (!Array.isArray(values)) return false;
    const valid = values.map(validateReplay).filter(Boolean), merged = new Map(history.map(r => [r.id, r]));
    for (const replay of valid) { replay.analysis = analyzeReplay(replay, cards); merged.set(replay.id, replay); }
    history = bounded([...merged.values()]); if (save) persist(); return true;
  }
  try { if (storage) hydrate(JSON.parse(storage.getItem(storageKey) || 'null'), false); } catch { error = 'An unreadable replay history was ignored.'; }
  return {
    start(metadata = {}) {
      active = {formatVersion: REPLAY_FORMAT_VERSION, id: `battle-${Date.now().toString(36)}-${(++sequence).toString(36)}`, version, createdAt: new Date().toISOString(), duration: 0, metadata: serializable(metadata), result: null, events: [], snapshots: []};
      return active.id;
    },
    event(time, type, data = {}) {
      if (!active || !Number.isFinite(time) || time < 0 || !type || active.events.length >= 100000) return false;
      active.events.push({time, seq: active.events.length, type: text(type, 40), data: serializable(data)}); return true;
    },
    snapshot(time, state) {
      if (!active || !Number.isFinite(time) || time < 0 || active.snapshots.length >= 16000) return false;
      const sample = {time, state: safeState(state)};
      // Retain before/after samples at one simulation time: bank extrema matter,
      // while playback intentionally uses the final sample at that timestamp.
      active.snapshots.push(sample);
      return true;
    },
    finish(time, result = {}) {
      if (!active) return null;
      active.duration = active.events.reduce((max, e) => Math.max(max, e.time), active.snapshots.reduce((max, s) => Math.max(max, s.time), positive(time))); active.result = serializable(result);
      active.analysis = analyzeReplay(active, cards); const replay = active; active = null;
      history = [replay, ...history.filter(r => r.id !== replay.id)]; persist(); return clone(replay);
    },
    cancel() { active = null; },
    get active() { return !!active; },
    get storageError() { return error; },
    get(id) { const replay = history.find(r => r.id === id); return replay ? clone(replay) : null; },
    history() { return history.map(r => ({id: r.id, version: r.version, createdAt: r.createdAt, duration: r.duration, metadata: clone(r.metadata), result: clone(r.result)})); },
    serialize() { return {formatVersion: REPLAY_FORMAT_VERSION, replays: clone(history)}; },
    hydrate,
    remove(id) { const before = history.length; history = history.filter(r => r.id !== id); persist(); return history.length !== before; },
    export(id) { const replay = history.find(r => r.id === id); return replay ? JSON.stringify(replay, null, 2) : null; },
    import(json) { let value; try { value = typeof json === 'string' ? JSON.parse(json) : json; } catch { return null; } const replay = validateReplay(value); if (!replay) return null; replay.analysis = analyzeReplay(replay, cards); history = [replay, ...history.filter(r => r.id !== replay.id)]; persist(); return clone(replay); }
  };
}

export function replayStateAt(replay, time) {
  const at = Math.max(0, Math.min(positive(replay.duration), finite(time))), samples = replay.snapshots || [];
  let index = -1;
  for (let i = 0; i < samples.length && samples[i].time <= at; i++) index = i;
  const previous = index >= 0 ? samples[index] : null, next = samples[index + 1];
  const state = previous ? clone(previous.state) : safeState();
  const from = previous?.time ?? -Infinity;
  for (const event of replay.events || []) {
    if (event.time <= from || event.time > at) continue;
    const d = event.data || {};
    if (event.type === 'entity_spawn') { const e = safeEntity(d.entity || d); if (e && !state.entities.some(old => old.id === e.id)) state.entities.push(e); }
    else if (event.type === 'damage') { const target = state.entities.find(e => e.id === String(d.targetId)); if (target) target.hp = Math.max(0, target.hp - positive(d.amount)); }
    else if (event.type === 'death' || event.type === 'tower_destroy') { const target = state.entities.find(e => e.id === String(d.targetId)); if (target) { target.hp = 0; target.dead = true; } }
    else if (event.type === 'card_play' && teamOK(d.team)) { state.aether[d.team] = Math.max(0, Math.min(10, finite(d.aetherAfter, state.aether[d.team]))); if (Array.isArray(d.handAfter)) state.hands[d.team] = d.handAfter.slice(0, 4); }
    else if (event.type === 'aether_grant' && teamOK(d.team)) state.aether[d.team] = Math.max(0, Math.min(10, finite(d.aether ?? d.aetherAfter, state.aether[d.team] + finite(d.amount))));
    else if (event.type === 'tower_edit') { const target = state.entities.find(e => e.id === String(d.targetId)); if (target) {target.hp = positive(d.hp); target.dead = target.hp <= 0;} }
    else if (event.type === 'entity_remove') state.entities = state.entities.filter(e => e.id !== String(d.targetId));
    else if (event.type === 'clear_field') {state.entities = state.entities.filter(e => e.kind === 'tower'); state.hazards = [];}
    else if (event.type === 'status') { const target = state.entities.find(e => e.id === String(d.targetId)); if (target && d.kind === 'slow') {target.slowUntil = positive(d.until); target.slowPct = positive(d.percent);} else if (target && d.kind === 'stun') target.stunUntil = positive(d.until); }
    else if (event.type === 'crowns') state.crowns = {player: positive(d.player), enemy: positive(d.enemy)};
    else if (event.type === 'phase') state.phase = text(d.phase, 30);
  }
  if (previous && next && next.time > previous.time) {
    const ratio = Math.max(0, Math.min(1, (at - previous.time) / (next.time - previous.time)));
    for (const entity of state.entities) {
      const destination = next.state.entities.find(e => e.id === entity.id);
      if (destination && !entity.dead) { entity.x += (destination.x - entity.x) * ratio; entity.z += (destination.z - entity.z) * ratio; }
    }
    // Resource interpolation is safe only where no deployment/grant occurred.
    for (const team of TEAMS) if (!(replay.events || []).some(e => e.time > previous.time && e.time <= next.time && ((e.type === 'card_play' && e.data?.team === team) || (e.type === 'aether_grant' && e.data?.team === team)))) state.aether[team] += (next.state.aether[team] - state.aether[team]) * ratio;
  }
  return {...state, time: at};
}
