// Pure rules shared by rendered combat and the worker. Card values come from CARD_LIBRARY.
import { deckAnalysisVersion } from './deck-analysis.js';
export const COMBAT_MODEL_VERSION = 'v15-event-2';
export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export function aetherMultiplier(elapsed, match) {
  if (elapsed < match.regulation - 60) return 1;
  if (elapsed < match.regulation + 60) return 2;
  return 3;
}
export function aetherIncomeBetween(from, to, match) {
  let total = 0;
  for (const [lo, hi, mult] of [[0, match.regulation - 60, 1], [match.regulation - 60, match.regulation + 60, 2], [match.regulation + 60, match.regulation + match.overtime, 3]]) {
    total += Math.max(0, Math.min(to, hi) - Math.max(from, lo)) * mult / match.baseAetherSeconds;
  }
  return total;
}
export function spellDamageFor(card, structure = false, dot = false) {
  if (dot) return structure ? 0 : (card.dotDamage || 0);
  return structure ? (card.towerDamage || 0) : card.damage;
}
export function canDirectTarget(card, target) {
  if (!card || !target || target.dead || target.spell) return false;
  const structure = target.building || target.tower || target.kind === 'guard' || target.kind === 'core';
  if (card.spell) return structure ? spellDamageFor(card, true) > 0 : true;
  if (card.building && structure) return false;
  if (card.buildingsOnly) return !!structure;
  if (target.flying && !card.canHitAir) return false;
  return true;
}
export function refreshSlow(state, percent, duration, now) {
  state.slowPct = Math.max(state.slowPct || 0, clamp(percent, 0, .8));
  state.slowUntil = Math.max(state.slowUntil || 0, now + duration);
  return state;
}
export function refreshStun(state, duration, now) {
  state.stunUntil = Math.max(state.stunUntil || 0, now + duration);
  return state;
}
export function movementMultiplier(state, now) {
  return now < (state.slowUntil || 0) ? 1 - (state.slowPct || 0) : 1;
}
export function directionalSight(source, target, sight, extra = 0) {
  const dx = target.x - source.x, dz = target.z - source.z;
  const len = Math.hypot(dx, dz) || 1;
  const facingX = source.facingX || 0, facingZ = source.facingZ ?? (source.team === 0 ? -1 : 1);
  return ((dx * facingX + dz * facingZ) / len >= 0 ? sight.frontTiles : sight.rearTiles) + extra;
}
export function wilsonInterval(score, n) {
  if (!n) return { low: 0, high: 100 };
  const p = clamp(score / n, 0, 1), z = 1.96, z2 = z * z, den = 1 + z2 / n;
  const center = (p + z2 / (2 * n)) / den;
  const half = z * Math.sqrt((p * (1 - p) + z2 / (4 * n)) / n) / den;
  return { low: clamp((center - half) * 100, 0, 100), high: clamp((center + half) * 100, 0, 100) };
}
export function adjustedWinRate(score, n, prior = 24) { return (score + prior / 2) / (n + prior) * 100; }
export function seededRandom(seed = 1) {
  let s = (Number(seed) >>> 0) || 0x6d2b79f5;
  return () => { s += 0x6d2b79f5; let t = s; t = Math.imul(t ^ t >>> 15, t | 1); t ^= t + Math.imul(t ^ t >>> 7, t | 61); return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
export function stableStringify(value) {
  if (Array.isArray(value)) return `[${value.map(stableStringify).join(',')}]`;
  if (value && typeof value === 'object') return `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${stableStringify(value[k])}`).join(',')}}`;
  return JSON.stringify(value);
}
export function fingerprint(value) {
  let hash = 2166136261;
  for (const ch of stableStringify(value)) { hash ^= ch.charCodeAt(0); hash = Math.imul(hash, 16777619); }
  return (hash >>> 0).toString(16).padStart(8, '0');
}
export function combatFingerprint(cards, rules, styles) {
  const omit = new Set(['name', 'desc', 'icon', 'model', 'artA', 'artB', 'combatClass', 'category', 'type']);
  const stats = Object.fromEntries(Object.entries(cards).map(([id, card]) => [id, Object.fromEntries(Object.entries(card).filter(([k]) => !omit.has(k)))]));
  return fingerprint({ stats, rules, styles, model: COMBAT_MODEL_VERSION, deckPolicy:deckAnalysisVersion() });
}
