import {analyzeReplay, replayStateAt, REPLAY_DEFINITIONS} from './battle-recorder.js';

const number = (value, decimals = 1) => Number.isFinite(Number(value)) ? Number(value).toLocaleString(undefined, {maximumFractionDigits: decimals}) : '0';
const clock = time => `${Math.floor(Math.max(0, time) / 60)}:${String(Math.floor(Math.max(0, time) % 60)).padStart(2, '0')}`;
const name = (cards, id) => cards[id]?.name || id || 'None';
function element(tag, className, content) {
  const el = document.createElement(tag); if (className) el.className = className; if (content != null) el.textContent = content; return el;
}
function button(label, action, className = '') {
  const el = element('button', className, label); el.type = 'button'; el.addEventListener('click', action); return el;
}
function download(text, filename, type = 'application/json') {
  const url = URL.createObjectURL(new Blob([text], {type})), anchor = element('a'); anchor.href = url; anchor.download = filename; anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function eventLabel(event, cards) {
  const d = event.data || {}, who = d.team === 'enemy' || d.sourceTeam === 'enemy' ? 'AI' : 'Player';
  if (event.type === 'card_play') return `${who} played ${name(cards, d.cardId)} (${number(d.cost)} Aether)`;
  if (event.type === 'ai_decision') return `AI: ${d.label || ''}${d.reason ? ` — ${d.reason}` : ''}`;
  if (event.type === 'tower_destroy') return `${d.targetTeam === 'enemy' ? 'AI' : 'Player'} tower destroyed`;
  if (event.type === 'phase') return `${d.phase === 'overtime' ? 'Overtime' : 'Tiebreaker'} starts`;
  if (event.type === 'death') return `${name(cards, d.targetCardId)} defeated${d.sourceCardId ? ` by ${name(cards, d.sourceCardId)}` : ''}`;
  return event.type;
}

export function mountReplayViewer(container, replay, options = {}) {
  const cards = options.cards || {}, analysis = replay.analysis || analyzeReplay(replay, cards);
  container.replaceChildren();
  const header = element('div', 'replay-heading');
  header.append(element('h3', '', `${replay.version || 'V15'} BATTLE RECORD`), element('p', '', `${new Date(replay.createdAt).toLocaleString()} · ${clock(replay.duration)} · ${replay.metadata?.training ? 'Training' : 'Battle'}${replay.metadata?.aiStyle ? ` · ${replay.metadata.aiStyle}` : ''}`));
  const description = element('p', 'replay-note', REPLAY_DEFINITIONS.playback);
  const status = element('div', 'replay-status'), canvas = element('canvas', 'replay-arena');
  canvas.width = 560; canvas.height = 840; canvas.setAttribute('role', 'img'); canvas.setAttribute('aria-label', 'Recorded arena with card positions, targets, damage, and tower health');
  const controls = element('div', 'replay-controls'), seek = element('input', 'replay-seek');
  seek.type = 'range'; seek.min = '0'; seek.max = String(replay.duration || 0); seek.step = '0.05'; seek.value = '0'; seek.setAttribute('aria-label', 'Replay time');
  const timeReadout = element('output', 'replay-time'), playButton = button('PLAY', () => setPlaying(!playing));
  controls.append(button('−5 SEC', () => seekTo(time - 5)), playButton, button('+5 SEC', () => seekTo(time + 5)));
  const speedButtons = [];
  for (const rate of [.25, .5, 1, 2, 4]) {
    const b = button(`${rate}×`, () => {speed = rate; for (const item of speedButtons) item.el.setAttribute('aria-pressed', String(item.rate === speed));});
    b.setAttribute('aria-label', `Replay speed ${rate}x`); b.setAttribute('aria-pressed', String(rate === 1)); speedButtons.push({rate, el: b}); controls.append(b);
  }
  controls.append(button('EXPORT REPLAY JSON', () => download(JSON.stringify(replay, null, 2), `rift-replay-${replay.id}.json`)));
  const bookmarks = element('div', 'replay-bookmarks'); bookmarks.append(element('strong', '', 'BOOKMARKS'));
  const expectedBookmarks = [{id: 'first-tower-damage', label: 'First Tower Damage'}, {id: 'first-tower-destroyed', label: 'First Tower Destroyed'}, {id: 'largest-push', label: 'Largest Push'}, {id: 'biggest-spell-value', label: 'Biggest Spell Value'}, {id: 'overtime-start', label: 'Overtime Start'}, {id: 'match-end', label: 'Match End'}];
  for (const expected of expectedBookmarks) {
    const entry = analysis.bookmarks.find(b => b.id === expected.id), b = button(entry ? `${expected.label} · ${clock(entry.time)}` : `${expected.label} · not recorded`, () => {if (entry) seekTo(entry.time);});
    b.disabled = !entry; bookmarks.append(b);
  }
  const hands = element('div', 'replay-hands'), log = element('ol', 'replay-event-log'); log.setAttribute('aria-label', 'Recorded match events');
  container.append(header, description, status, canvas, seek, timeReadout, controls, bookmarks, hands, element('h4', '', 'RECORDED EVENTS'), log);
  let time = 0, speed = 1, playing = false, destroyed = false, frameId = null, previousFrame = null;
  const ctx = canvas.getContext('2d'), images = new Map();
  for (const [id] of Object.entries(cards)) { const img = new Image(); img.onload = () => {if (!destroyed) draw();}; img.src = `./assets/ui/${id}.svg`; images.set(id, img); }
  function setPlaying(value) {
    if (destroyed) return;
    if (value && time >= replay.duration) time = 0;
    playing = value; previousFrame = null; playButton.textContent = playing ? 'PAUSE' : 'PLAY';
    if (playing && frameId === null) frameId = requestAnimationFrame(frame);
    if (!playing && frameId !== null) { cancelAnimationFrame(frameId); frameId = null; }
    draw();
  }
  function seekTo(value) { time = Math.max(0, Math.min(replay.duration, Number(value) || 0)); previousFrame = null; if (time >= replay.duration) setPlaying(false); draw(); }
  seek.addEventListener('input', () => seekTo(seek.value));
  function frame(timestamp) {
    frameId = null; if (destroyed || !playing) return;
    if (previousFrame !== null) time = Math.min(replay.duration, time + Math.min(.2, Math.max(0, (timestamp - previousFrame) / 1000)) * speed);
    previousFrame = timestamp; draw();
    if (time >= replay.duration) setPlaying(false); else frameId = requestAnimationFrame(frame);
  }
  function draw() {
    if (destroyed) return;
    const state = replayStateAt(replay, time); seek.value = String(time); timeReadout.textContent = `${clock(time)}.${Math.floor((time % 1) * 10)} / ${clock(replay.duration)} · ${state.phase.toUpperCase()}`;
    status.textContent = `PLAYER ${state.crowns.player} crowns · ${number(state.aether.player)} Aether  |  AI ${state.crowns.enemy} crowns · ${number(state.aether.enemy)} Aether`;
    hands.replaceChildren();
    for (const team of ['player', 'enemy']) { const p = element('p', '', `${team === 'player' ? 'Player' : 'AI'} hand: ${(state.hands[team] || []).map(id => name(cards, id)).join(' · ') || 'Not recorded'}`); hands.append(p); }
    const recent = (replay.events || []).filter(e => e.time <= time && ['card_play', 'ai_decision', 'death', 'tower_destroy', 'phase'].includes(e.type)).slice(-6).reverse();
    log.replaceChildren(...recent.map(e => element('li', '', `${clock(e.time)} · ${eventLabel(e, cards)}`)));
    if (!ctx) return;
    const inset = 28, width = canvas.width - inset * 2, height = canvas.height - inset * 2;
    const x = worldX => inset + (worldX + 14) / 28 * width, z = worldZ => inset + (worldZ + 21) / 42 * height;
    ctx.clearRect(0, 0, canvas.width, canvas.height); ctx.fillStyle = '#101713'; ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#263526'; ctx.fillRect(inset, inset, width, height); ctx.strokeStyle = '#31442e'; ctx.lineWidth = .6;
    for (let col = 0; col <= 28; col++) { ctx.beginPath(); ctx.moveTo(inset + col / 28 * width, inset); ctx.lineTo(inset + col / 28 * width, inset + height); ctx.stroke(); }
    for (let row = 0; row <= 42; row++) { ctx.beginPath(); ctx.moveTo(inset, inset + row / 42 * height); ctx.lineTo(inset + width, inset + row / 42 * height); ctx.stroke(); }
    ctx.fillStyle = '#214652'; ctx.fillRect(inset, z(-1.65), width, z(1.65) - z(-1.65));
    ctx.fillStyle = '#80734b'; for (const center of [-7.2, 7.2]) ctx.fillRect(x(center - 2.1), z(-1.75), x(center + 2.1) - x(center - 2.1), z(1.75) - z(-1.75));
    ctx.fillStyle = '#ddcfa8'; ctx.font = 'bold 12px system-ui'; ctx.textAlign = 'left'; ctx.fillText('AI', inset, 18); ctx.fillText('PLAYER', inset, canvas.height - 9);
    for (const h of state.hazards || []) { ctx.beginPath(); ctx.arc(x(h.x), z(h.z), h.radius / 28 * width, 0, Math.PI * 2); ctx.fillStyle = '#ff9b4922'; ctx.strokeStyle = '#f3a65f'; ctx.fill(); ctx.stroke(); }
    for (const entity of state.entities) {
      if (entity.dead || entity.hp <= 0) continue;
      const target = state.entities.find(e => e.id === entity.targetId && !e.dead);
      if (target) { ctx.beginPath(); ctx.moveTo(x(entity.x), z(entity.z)); ctx.lineTo(x(target.x), z(target.z)); ctx.strokeStyle = entity.team === 'player' ? '#6eceff55' : '#ff748b55'; ctx.lineWidth = 1; ctx.stroke(); }
    }
    for (const entity of state.entities) {
      const px = x(entity.x), py = z(entity.z), tower = entity.kind === 'tower', size = tower ? entity.towerKind === 'core' ? 38 : 30 : entity.kind === 'building' ? 29 : 24;
      if (entity.dead || entity.hp <= 0) { if (tower) {ctx.strokeStyle = '#766a54'; ctx.beginPath(); ctx.moveTo(px - 10, py - 10); ctx.lineTo(px + 10, py + 10); ctx.moveTo(px - 10, py + 10); ctx.lineTo(px + 10, py - 10); ctx.stroke();} continue; }
      ctx.fillStyle = entity.team === 'player' ? '#76d7ff' : '#ff7189'; ctx.strokeStyle = '#111a16'; ctx.lineWidth = 2;
      if (tower || entity.kind === 'building') { ctx.fillRect(px - size / 2, py - size / 2, size, size); ctx.strokeRect(px - size / 2, py - size / 2, size, size); }
      else { ctx.beginPath(); ctx.arc(px, py, size / 2 + 2, 0, Math.PI * 2); ctx.fill(); ctx.stroke(); }
      const img = images.get(entity.cardId); if (img?.complete && img.naturalWidth) ctx.drawImage(img, px - size / 2, py - size / 2, size, size);
      else { ctx.fillStyle = '#15231c'; ctx.font = `bold ${tower ? 17 : 10}px system-ui`; ctx.textAlign = 'center'; ctx.fillText(tower ? entity.towerKind === 'core' ? '♛' : '♜' : name(cards, entity.cardId).slice(0, 2), px, py + 5); }
      if (entity.flying) { ctx.strokeStyle = '#f5e9b8'; ctx.beginPath(); ctx.arc(px, py, size / 2 + 5, 0, Math.PI * 2); ctx.stroke(); }
      if (entity.slowUntil > time) {ctx.strokeStyle = '#b8efff'; ctx.beginPath(); ctx.arc(px, py, size / 2 + 7, 0, Math.PI * 2); ctx.stroke();}
      if (entity.stunUntil > time) {ctx.fillStyle = '#f3e995'; ctx.font = 'bold 15px system-ui'; ctx.textAlign = 'center'; ctx.fillText('✧', px, py - size / 2 - 11);}
      ctx.fillStyle = '#141b15'; ctx.fillRect(px - size / 2, py - size / 2 - 7, size, 4); ctx.fillStyle = entity.team === 'player' ? '#7bd8fb' : '#ff7c8f'; ctx.fillRect(px - size / 2, py - size / 2 - 7, size * Math.min(1, entity.hp / Math.max(1, entity.maxHp)), 4);
      if (tower) { ctx.fillStyle = '#f0e6c6'; ctx.font = '10px system-ui'; ctx.textAlign = 'center'; ctx.fillText(String(Math.ceil(entity.hp)), px, py + size / 2 + 13); }
    }
    for (const event of replay.events || []) {
      if (event.type !== 'damage' || event.time > time || time - event.time > .65) continue;
      const d = event.data, target = state.entities.find(e => e.id === String(d.targetId)); if (!target && !Number.isFinite(d.x)) continue;
      ctx.globalAlpha = Math.max(0, 1 - (time - event.time) / .65); ctx.fillStyle = '#fff2bf'; ctx.font = 'bold 12px system-ui'; ctx.textAlign = 'center'; ctx.fillText(`−${Math.round(d.amount)}`, x(target?.x ?? d.x), z(target?.z ?? d.z) - 15 - (time - event.time) * 20); ctx.globalAlpha = 1;
    }
  }
  draw();
  return {seek: seekTo, play() {setPlaying(true);}, pause() {setPlaying(false);}, destroy() {destroyed = true; if (frameId !== null) cancelAnimationFrame(frameId); for (const img of images.values()) img.onload = null; container.replaceChildren();}, get time() {return time;}};
}

function metricCard(label, player, enemy) {
  const card = element('div', 'match-analysis-metric'); card.append(element('span', '', label), element('strong', '', `P ${player}  /  AI ${enemy}`)); return card;
}
function timelineSvg(points, keys, title, yLabel, colors, symmetric = false) {
  const ns = 'http://www.w3.org/2000/svg', svg = document.createElementNS(ns, 'svg'); svg.setAttribute('viewBox', '0 0 720 220'); svg.setAttribute('role', 'img'); svg.setAttribute('aria-label', title); svg.classList.add('match-analysis-timeline');
  const values = points.flatMap(p => keys.map(key => Number(p[key]) || 0)); let low = symmetric ? -Math.max(1, ...values.map(Math.abs)) : 0, high = symmetric ? -low : Math.max(1, ...values), duration = Math.max(1, points.at(-1)?.time || 0);
  const x = t => 56 + t / duration * 640, y = value => 178 - (value - low) / (high - low) * 140;
  const add = (tag, attrs, content) => {const node = document.createElementNS(ns, tag); for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, String(value)); if (content != null) node.textContent = content; svg.append(node); return node;};
  add('title', {}, title); add('text', {x: 56, y: 20, fill: '#e8dbb4', 'font-size': 13}, title);
  for (let i = 0; i <= 4; i++) { const value = low + (high - low) * i / 4; add('line', {x1: 56, x2: 696, y1: y(value), y2: y(value), stroke: '#526047', 'stroke-width': .6}); add('text', {x: 48, y: y(value) + 4, fill: '#b9b597', 'font-size': 11, 'text-anchor': 'end'}, number(value)); }
  for (let i = 0; i <= 4; i++) add('text', {x: x(duration * i / 4), y: 198, fill: '#b9b597', 'font-size': 11, 'text-anchor': 'middle'}, clock(duration * i / 4));
  keys.forEach((key, index) => add('polyline', {points: points.map(p => `${x(p.time)},${y(Number(p[key]) || 0)}`).join(' '), fill: 'none', stroke: colors[index], 'stroke-width': 2}));
  add('text', {x: 56, y: 214, fill: '#b9b597', 'font-size': 10}, yLabel); return svg;
}

export function renderMatchAnalysis(container, replay, options = {}) {
  const cards = options.cards || {}, analysis = replay.analysis || analyzeReplay(replay, cards), p = analysis.teams.player, e = analysis.teams.enemy;
  container.replaceChildren();
  const outcome = replay.result?.abandoned ? 'ABANDONED BATTLE' : replay.result?.winner === 'player' ? 'PLAYER VICTORY' : replay.result?.winner === 'enemy' ? 'AI VICTORY' : 'DRAW';
  container.append(element('h3', '', `${outcome} · ${clock(replay.duration)}`), element('p', 'replay-note', 'Recorded battle results. P = Player; AI = opponent. All damage uses actual HP removed.'));
  const grid = element('div', 'match-analysis-grid');
  const metrics = [['Aether spent', 'aetherSpent'], ['Aether leaked', 'aetherLeaked'], ['Damage', 'damage'], ['Tower damage', 'towerDamage'], ['Kills', 'kills'], ['Deaths', 'deaths'], ['Cards played', 'cardsPlayed'], ['Average hand cost', 'averageHandCost'], ['Crown contribution', 'crownContribution']];
  for (const [label, key] of metrics) grid.append(metricCard(label, number(p[key]), number(e[key])));
  grid.append(metricCard('Most valuable card', name(cards, p.mostValuable), name(cards, e.mostValuable)), metricCard('Least valuable card', name(cards, p.leastValuable), name(cards, e.leastValuable)));
  const push = value => value ? `${number(value.value)} surviving Aether · ${value.units} units at ${clock(value.time)}` : 'No push crossed the river';
  const trade = value => value ? `${name(cards, value.cardId)} ${value.value >= 0 ? '+' : ''}${number(value.value)} Aether at ${clock(value.time)}` : 'No defensive kill trade';
  const advantage = value => `+${number(value.value)} at ${clock(value.time)}`;
  grid.append(metricCard('Biggest push', push(p.biggestPush), push(e.biggestPush)), metricCard('Biggest defensive trade', trade(p.biggestDefensiveTrade), trade(e.biggestDefensiveTrade)), metricCard('Largest Aether advantage', advantage(p.largestAetherAdvantage), advantage(e.largestAetherAdvantage))); container.append(grid);
  if (options.onReplay) container.append(button('WATCH REPLAY', () => options.onReplay(replay.id)));
  const table = element('table', 'match-analysis-table'), thead = element('thead'), tr = element('tr');
  for (const label of ['Side / Card', 'Plays', 'Aether', 'Damage', 'Tower Dmg', 'Kills', 'Deaths', 'Damage / Aether', 'Crowns']) tr.append(element('th', '', label)); thead.append(tr); table.append(thead);
  const tbody = element('tbody');
  for (const [side, team] of [['P', p], ['AI', e]]) for (const row of Object.values(team.cards).sort((a, b) => b.towerDamage - a.towerDamage || b.damage - a.damage)) {
    const r = element('tr'); for (const value of [`${side} · ${name(cards, row.cardId)}`, row.uses, number(row.aetherSpent), number(row.damage), number(row.towerDamage), row.kills, row.deaths, number(row.damagePerAether), number(row.crownContribution, 2)]) r.append(element('td', '', value)); tbody.append(r);
  }
  table.append(tbody); const tableWrap = element('div', 'match-analysis-table-wrap'); tableWrap.append(table); container.append(element('h4', '', 'CARD CONTRIBUTIONS'), tableWrap);
  const plots = element('div', 'match-analysis-plots');
  plots.append(timelineSvg(analysis.timeline, ['aetherAdvantage'], 'AETHER ADVANTAGE OVER TIME', 'Positive = Player bank advantage; negative = AI.', ['#e9c86d'], true), timelineSvg(analysis.timeline, ['playerTowerHp', 'enemyTowerHp'], 'TOTAL CROWN TOWER HP OVER TIME', 'Blue = Player HP; red = AI HP. Includes tiebreaker drain.', ['#78d7fb', '#fb8291'])); container.append(plots);
  const details = element('details', 'match-analysis-definitions'); details.append(element('summary', '', 'METRIC DEFINITIONS'));
  for (const [key, definition] of Object.entries(REPLAY_DEFINITIONS)) details.append(element('p', '', `${key.toUpperCase()}: ${definition}`)); container.append(details);
  return analysis;
}

export function renderReplayHistory(container, recorder, options = {}) {
  container.replaceChildren();
  const history = recorder.history(), list = element('div', 'replay-history');
  if (!history.length) list.append(element('p', '', 'Finish a battle to record its replay and analysis.'));
  for (const replay of history) {
    const row = element('div', 'replay-history-row'), result = replay.result?.abandoned ? 'ABANDONED' : replay.result?.winner === 'player' ? 'VICTORY' : replay.result?.winner === 'enemy' ? 'DEFEAT' : 'DRAW';
    row.append(element('span', '', `${new Date(replay.createdAt).toLocaleString()} · ${result} · ${clock(replay.duration)} · ${replay.version}`), button('REPLAY', () => options.onReplay?.(replay.id)), button('ANALYSIS', () => options.onAnalysis?.(replay.id)), button('DELETE', () => {recorder.remove(replay.id); options.onChange?.(); renderReplayHistory(container, recorder, options);})); list.append(row);
  }
  const fileLabel = element('label', 'replay-import', 'IMPORT REPLAY JSON '), file = element('input'); file.type = 'file'; file.accept = '.json,application/json';
  const feedback = element('p', 'replay-import-feedback'); feedback.setAttribute('role', 'status');
  file.addEventListener('change', async () => {
    const selected = file.files?.[0]; if (!selected) return;
    if (selected.size > 25000000) {feedback.textContent = 'Replay exceeds the 25 MB import limit.'; return;}
    try {const imported = recorder.import(await selected.text()); if (!imported) {feedback.textContent = 'This file is not a supported Rift Crown replay.'; return;} options.onChange?.(); renderReplayHistory(container, recorder, options); options.onReplay?.(imported.id);} catch {feedback.textContent = 'Unable to read that replay file.';}
  }); fileLabel.append(file); container.append(list, fileLabel, feedback);
  if (recorder.storageError) container.append(element('p', 'replay-note', recorder.storageError));
}

export function createReplayViewer(options) {
  const {recorder, cards = {}, onChange} = options;
  const replayContainer = options.replayContainer || document.querySelector('#replay-viewer');
  const analysisContainer = options.analysisContainer || document.querySelector('#match-analysis-content');
  const historyContainer = options.historyContainer || document.querySelector('#replay-history-content');
  const replayModal = options.replayModal || document.querySelector('#replay-modal');
  const analysisModal = options.analysisModal || document.querySelector('#match-analysis-modal');
  const historyModal = options.historyModal || document.querySelector('#replay-history-modal');
  let mounted = null;
  const cleanup = () => {mounted?.destroy(); mounted = null;};
  const get = id => recorder.get(id || recorder.history()[0]?.id);
  const controller = {
    refresh() {if (historyContainer) renderReplayHistory(historyContainer, recorder, {cards, onReplay: id => controller.openReplay(id), onAnalysis: id => controller.openAnalysis(id), onChange});},
    openHistory() {cleanup(); replayModal?.classList.add('hidden'); analysisModal?.classList.add('hidden'); controller.refresh(); historyModal?.classList.remove('hidden');},
    openReplay(id) {
      const replay = get(id); if (!replay || !replayContainer) {controller.openHistory(); return false;}
      cleanup(); historyModal?.classList.add('hidden'); analysisModal?.classList.add('hidden'); mounted = mountReplayViewer(replayContainer, replay, {cards}); replayModal?.classList.remove('hidden'); options.onOpen?.('replay'); return true;
    },
    openAnalysis(id) {
      const replay = get(id); if (!replay || !analysisContainer) {controller.openHistory(); return false;}
      cleanup(); historyModal?.classList.add('hidden'); replayModal?.classList.add('hidden'); renderMatchAnalysis(analysisContainer, replay, {cards, onReplay: replayId => controller.openReplay(replayId)}); analysisModal?.classList.remove('hidden'); options.onOpen?.('analysis'); return true;
    },
    close() {cleanup(); replayModal?.classList.add('hidden'); analysisModal?.classList.add('hidden');},
    destroy() {cleanup(); for (const [el, type, handler] of closeHandlers) el.removeEventListener(type, handler);}
  };
  const closeHandlers = [];
  for (const id of ['replay-modal', 'match-analysis-modal']) for (const el of document.querySelectorAll(`[data-close="${id}"]`)) {
    const handler = () => controller.close(); el.addEventListener('click', handler); closeHandlers.push([el, 'click', handler]);
  }
  for (const modal of [replayModal, analysisModal]) if (modal) {const handler = event => {if (event.target === modal) controller.close();}; modal.addEventListener('pointerdown', handler); closeHandlers.push([modal, 'pointerdown', handler]);}
  controller.refresh(); return controller;
}
