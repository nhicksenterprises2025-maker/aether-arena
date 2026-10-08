/* V14 deck intelligence. All card values come from the game's live library.
 * Counter and matchup scores describe mechanical coverage, not win probability.
 * HP/DPS/range averages use non-spell deployments (including buildings); HP and
 * DPS include every member of a swarm. Cost averages use all valid unique cards.
 * Counter estimates assume a defensive deployment, a clustered swarm, and no
 * Crown Tower assistance. Position, surviving HP and the observed cycle still
 * belong to the live battle AI rather than this static composition model.
 */
const clamp = (n, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, n));
const mean = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0;
const finite = value => Number.isFinite(value) ? value : 0;
const round = value => Math.round(finite(value) * 100) / 100;

export function deckAnalysisVersion() { return 'v15-deck-1'; }

export function createDeckAnalyzer(cards) {
  const ids = Object.keys(cards).filter(id => cards[id] && typeof cards[id] === 'object');
  const features = Object.assign(Object.create(null), Object.fromEntries(ids.map(id => {
    const c = cards[id], spell = !!c.spell, building = !!c.building;
    const count = spell ? 0 : Math.max(1, finite(c.count) || 1);
    const dps = !spell && c.attackSpeed > 0 ? finite(c.damage / c.attackSpeed) * count : 0;
    const auraDps = c.auraInterval > 0 ? finite(c.auraDamage / c.auraInterval) : 0;
    return [id, {
      id, card: c, spell, building, troop: !spell && !building,
      cost: Math.max(0, finite(c.cost)), count, hp: finite(c.hp) * count,
      unitHp: finite(c.hp), dps, range: spell ? 0 : finite(c.range),
      move: Math.max(0, finite(c.moveSpeed)), flying: !!c.flying,
      win: !spell && !building && !!c.buildingsOnly,
      directAir: !spell && !c.buildingsOnly && !!c.canHitAir,
      antiAir: spell ? c.damage > 0 : (!c.buildingsOnly && !!c.canHitAir) || auraDps > 0,
      groundOnly: !spell && !c.canHitAir && auraDps <= 0,
      area: spell && c.radius > 0 || c.splash > 0 || auraDps > 0,
      swarm: count > 1, auraDps, slow: Math.max(0, finite(c.slowPct)),
      towerSpell: spell && c.towerDamage > 0,
    }];
  })));
  const troops = ids.map(id => features[id]).filter(f => f.troop);
  const threats = troops.map(f => f.id);
  const poolHp = mean(troops.map(f => f.unitHp));
  const poolRange = mean(troops.map(f => f.range));
  const poolMove = mean(troops.map(f => f.move));
  const poolCost = mean(ids.map(id => features[id].cost));
  const cheapLimit = Math.floor(poolCost);
  const counters = new Map(), pairs = new Map(), intelligence = new Map();
  let candidates = null;
  const styleCandidates = new Map();

  function directCanDamage(a, b) {
    if (!a || !b || b.spell) return false;
    if (a.spell) return b.building ? a.card.towerDamage > 0 : a.card.damage > 0;
    // Defensive buildings acquire only troops in the live combat loop.
    if (a.building) return b.troop && (!b.flying || a.card.canHitAir);
    if (a.win) return b.building;
    return !b.flying || !!a.card.canHitAir;
  }

  function canDamage(attackerId, targetId) {
    const a = features[attackerId], b = features[targetId];
    return !!(directCanDamage(a, b) || a?.auraDps > 0 && b?.troop);
  }

  function spellDamage(a, b, stationary = false) {
    if (b.building) return Math.max(0, finite(a.card.towerDamage));
    const duration = Math.max(0, finite(a.card.dotDuration));
    // Moving troops can leave a zone; spells never promise stationary maximum.
    const dwell = stationary ? duration : Math.min(duration, 2 * finite(a.card.radius) / Math.max(.5, b.move));
    return Math.max(0, finite(a.card.damage)) + Math.max(0, finite(a.card.dotDamage)) * dwell;
  }

  function attackDps(a, b) {
    let dps = 0;
    if (directCanDamage(a, b)) {
      const hits = b.troop && a.card.splash > 0 ? Math.min(b.count, 1 + a.card.splash) : 1;
      dps += a.dps * hits;
    }
    if (a.auraDps > 0 && b.troop) dps += a.auraDps * Math.min(b.count, 1 + finite(a.card.auraRadius));
    return dps;
  }

  function runningChargeBonus(a, b) {
    // Chargers gain one opening hit after a running approach. This bonus only
    // applies to a legal structure target, never to troop defense.
    return a.card.charger && b.building && directCanDamage(a, b)
      ? Math.max(0, finite(a.card.chargeDamage) - finite(a.card.damage)) * a.count : 0;
  }

  function counterScore(defenderId, threatId) {
    const key = `${defenderId}/${threatId}`;
    if (counters.has(key)) return counters.get(key);
    const d = features[defenderId], t = features[threatId];
    if (!d || !t || t.spell || !canDamage(defenderId, threatId)) return 0;
    let result;
    if (d.spell) {
      const damage = spellDamage(d, t), fraction = clamp(damage / Math.max(1, t.unitHp));
      const killed = damage >= t.unitHp;
      // A spell must remove each swarm member, rather than dividing damage by
      // aggregate HP. Its cost trades against the whole deployed card.
      const efficiency = clamp(t.cost / Math.max(.1, d.cost), 0, 2);
      result = fraction * 57 + (killed ? 23 : 0) + efficiency * 10;
      if (t.building && !killed) result *= .72;
    } else {
      const outgoing = attackDps(d, t), incoming = attackDps(t, d);
      const start = !directCanDamage(d, t) ? finite(d.card.auraInterval) : .25;
      const approach = d.building ? 0 : Math.max(0, t.range - d.range) / Math.max(.5, d.move);
      const initialLoss = incoming * approach + runningChargeBonus(t, d);
      // Single-target attacks progressively remove swarm DPS. Area attacks keep
      // their damage against clustered members; single-target swarm output is
      // discounted when the opposing card can retaliate.
      const casualtyFactor = d.swarm && incoming > 0 ? (t.area ? .56 : .76) : 1;
      const killTime = Math.max(1, t.hp - runningChargeBonus(d, t)) / Math.max(1, outgoing * casualtyFactor) + start + approach;
      const decay = d.building && d.card.lifetime > 0 ? d.hp / d.card.lifetime : 0;
      const survival = incoming + decay > 0 ? Math.max(0, d.hp - initialLoss) / (incoming + decay) : 30;
      const survivalFraction = clamp(survival / Math.max(.25, killTime));
      const removal = clamp(9 / Math.max(1, killTime));
      const efficiency = clamp(t.cost / Math.max(.1, d.cost), 0, 1.5);
      result = 34 * removal + 35 * survivalFraction + 12 * efficiency;
      if (t.flying && d.directAir) result += 4;
      if (t.swarm && d.card.splash > 0) result += 10;
      if (d.slow > 0 && !t.flying && t.troop && directCanDamage(d, t)) {
        const uptime = clamp(finite(d.card.slowDuration) / Math.max(.1, finite(d.card.attackSpeed)));
        const reliance = clamp(t.move / Math.max(.1, poolMove), .5, 1.5);
        result += d.slow * uptime * reliance * (t.win || t.card.charger ? 33 : 16);
      }
      if (d.building && t.win) {
        // Structure attackers are pulled off a Crown Tower. Building decay
        // participates in the diversion duration, as it does in live combat.
        const diverted = Math.max(0, d.hp - runningChargeBonus(t, d)) / Math.max(1, incoming + decay);
        result += 10 + clamp(diverted / 7) * 14;
      }
      if (d.auraDps > 0 && t.troop) result += clamp(finite(d.card.stunDuration) / Math.max(.1, finite(d.card.auraInterval))) * 28;
      // An aura is delayed, short-range troop damage, not a structure attack's
      // full direct DPS and not equivalent to ranged anti-air coverage.
      if (!directCanDamage(d, t) && d.auraDps > 0) result *= .64;
    }
    const value = Math.round(clamp(result, 0, 100));
    counters.set(key, value);
    return value;
  }

  function counterReason(defenderId, threatId) {
    const d = features[defenderId], t = features[threatId];
    if (!d || !t) return 'Unknown card.';
    if (!canDamage(defenderId, threatId)) return t.flying ? 'Cannot damage this flying target.' : 'Target restrictions prevent damage.';
    if (d.spell) {
      const hit = spellDamage(d, t);
      return `${hit >= t.unitHp ? 'Removes' : 'Damages'} ${t.swarm ? 'clustered swarm members' : t.building ? 'the building' : 'the troop'}${d.card.dotDamage && !t.building ? '; damage over time depends on remaining inside the zone' : ''}.`;
    }
    const reasons = [];
    if (d.building && t.win) reasons.push('pulls the structure attacker away from Crown Towers');
    if (d.card.charger && t.building) reasons.push('a running approach adds opening charge damage');
    if (t.flying && d.directAir) reasons.push('direct attacks reach flying troops');
    if (d.flying && !canDamage(threatId, defenderId)) reasons.push('flies above this card’s ground attacks');
    if (t.swarm && d.card.splash) reasons.push('splash hits clustered swarm members');
    if (d.slow && !t.flying) reasons.push('refreshing movement slow delays ground pressure');
    if (d.auraDps && t.troop) reasons.push('nearby troops take delayed ring damage and stun');
    if (d.range > t.range && directCanDamage(d, t)) reasons.push('range creates a first-strike window');
    if (!reasons.length) reasons.push(d.swarm ? 'multiple units concentrate damage' : 'HP and damage support a defensive trade');
    return reasons.join('; ') + '.';
  }

  function synergy(aId, bId) {
    if (!features[aId] || !features[bId] || aId === bId) return { aId, bId, score: 0, reasons: [aId === bId ? 'Duplicate cards cannot share a deck.' : 'Unknown card.'] };
    const key = [aId, bId].sort().join('/');
    if (pairs.has(key)) return { ...pairs.get(key), aId, bId, reasons: pairs.get(key).reasons.slice() };
    const a = features[aId], b = features[bId], reasons = [];
    let score = 32;
    const tankSupport = (tank, support) => tank.troop && tank.unitHp >= poolHp && support.troop && !support.win && support.range >= poolRange;
    if (tankSupport(a, b) || tankSupport(b, a)) {
      score += 19; reasons.push('A durable frontline protects ranged damage.');
    }
    if (a.win !== b.win && (a.win ? b : a).troop && !(a.win ? b : a).win) {
      score += 11; reasons.push('Troop damage clears defenders for the structure attacker.');
    }
    if (a.antiAir !== b.antiAir) {
      score += a.directAir || b.directAir || a.spell || b.spell ? 9 : 4;
      reasons.push(a.directAir || b.directAir || a.spell || b.spell ? 'Adds damage against flying defenders.' : 'The nearby electric ring adds limited air coverage.');
    }
    if (a.area !== b.area) { score += 9; reasons.push('Area damage complements single-target pressure.'); }
    if (a.spell !== b.spell) {
      const spell = a.spell ? a : b, unit = a.spell ? b : a;
      if (unit.win || unit.building || unit.swarm) {
        score += 11; reasons.push(unit.building ? 'Area spell damage supports a defensive building’s single-target fire.' : 'A spell removes clustered defenders during a push.');
      }
      if (spell.card.dotDamage && unit.slow > 0) {
        score += 12; reasons.push('Movement slow increases time inside the damage zone.');
      }
    }
    if ((a.slow > 0 && b.troop && (b.win || b.range > poolRange)) || (b.slow > 0 && a.troop && (a.win || a.range > poolRange))) {
      score += 11; reasons.push('Refreshing slow buys time for supporting damage or a structure push.');
    }
    if ((a.auraDps > 0 && b.troop && b.range < poolRange) || (b.auraDps > 0 && a.troop && a.range < poolRange)) {
      score += 8; reasons.push('A nearby frontline keeps defenders in the ring’s damage and stun radius.');
    }
    if ((a.swarm && b.troop && b.unitHp >= poolHp) || (b.swarm && a.troop && a.unitHp >= poolHp)) {
      score += 7; reasons.push('High-HP pressure screens multiple damage dealers.');
    }
    if ((a.cost <= cheapLimit && b.cost > cheapLimit) || (b.cost <= cheapLimit && a.cost > cheapLimit)) {
      score += 5; reasons.push('The cheaper card leaves Aether for support and rotation.');
    }
    const coverage = threats.filter(id => {
      const ac = counterScore(aId, id), bc = counterScore(bId, id);
      return Math.min(ac, bc) < 40 && Math.max(ac, bc) >= 65;
    }).length;
    if (coverage >= 2) { score += Math.min(10, coverage * 2); reasons.push('Their defensive target coverage fills different gaps.'); }
    if (a.cost + b.cost > 10) { score -= (a.cost + b.cost - 10) * 5; reasons.push('The combined cost requires staging the push across regeneration.'); }
    if (a.spell && b.spell) { score -= 7; reasons.push('Two spells need troops or a building to hold the lane.'); }
    if (a.win && b.win) { score -= 4; reasons.push('Two structure attackers need separate troop support.'); }
    if (a.groundOnly && b.groundOnly) { score -= 6; reasons.push('The pair needs separate air coverage.'); }
    if (!reasons.length) reasons.push('Similar roles offer reliable rotation but limited complementary coverage.');
    const result = { aId, bId, score: Math.round(clamp(score, 0, 100)), reasons };
    pairs.set(key, result);
    return { ...result, reasons: reasons.slice() };
  }

  function collect(input) {
    const source = Array.isArray(input) ? input : [], unique = [...new Set(source.filter(id => typeof id === 'string' && features[id]))];
    const errors = [];
    if (!Array.isArray(input)) errors.push('A deck must be an array of card IDs.');
    if (source.some(id => typeof id !== 'string' || !features[id])) errors.push('Unknown cards are excluded from analysis.');
    if (source.length !== unique.length && source.some((id, i) => source.indexOf(id) !== i)) errors.push('Duplicate cards are excluded from analysis.');
    if (unique.length !== 8) errors.push(`Choose exactly 8 unique cards (${unique.length} selected).`);
    return { selected: unique.map(id => features[id]), unique, errors };
  }

  function composition(selected) {
    const entities = selected.filter(f => !f.spell), units = selected.filter(f => f.troop);
    const counts = {
      troops: units.length, spells: selected.filter(f => f.spell).length,
      buildings: selected.filter(f => f.building).length,
      antiAir: selected.filter(f => f.antiAir).length,
      groundOnly: selected.filter(f => f.groundOnly).length,
      winConditions: selected.filter(f => f.win).length,
      splash: selected.filter(f => f.area).length, swarm: selected.filter(f => f.swarm).length,
    };
    const averages = { cost: round(mean(selected.map(f => f.cost))), hp: round(mean(entities.map(f => f.hp))), dps: round(mean(entities.map(f => f.dps))), range: round(mean(entities.map(f => f.range))) };
    const metrics = {
      sustainedAntiAir: selected.filter(f => f.directAir).length,
      cheap: selected.filter(f => f.cost <= cheapLimit).length,
      rangedSupport: units.filter(f => !f.win && f.range >= poolRange).length,
      frontline: units.filter(f => f.unitHp >= poolHp).length,
      defensiveTroops: units.filter(f => !f.win).length,
      flying: units.filter(f => f.flying).length,
      fast: units.filter(f => f.move >= poolMove).length,
      fastWinConditions: units.filter(f => f.win && f.move >= poolMove).length,
      heavyWinConditions: units.filter(f => f.win && f.unitHp >= poolHp && f.move < poolMove).length,
      siege: units.filter(f => f.win && f.move < poolMove && !f.flying).length,
      slow: units.filter(f => f.slow > 0).length,
      towerSpells: selected.filter(f => f.towerSpell).length,
      cycleCost: selected.map(f => f.cost).sort((a, b) => a - b).slice(0, 4).reduce((a, b) => a + b, 0),
      totalHp: round(entities.reduce((sum, f) => sum + f.hp, 0)),
      totalDps: round(entities.reduce((sum, f) => sum + f.dps, 0)),
      spellTowerDamage: selected.reduce((sum, f) => sum + (f.towerSpell ? finite(f.card.towerDamage) : 0), 0),
      defense: 0, synergy: 0,
    };
    return { counts, averages, metrics, denominators: { cost: selected.length, hp: entities.length, dps: entities.length, range: entities.length } };
  }

  function classifications(counts, averages, metrics) {
    if (!counts.troops && !counts.spells && !counts.buildings) return [];
    const labels = [];
    if (metrics.heavyWinConditions && metrics.frontline >= 2 && metrics.rangedSupport >= 1) labels.push('Beatdown');
    if (counts.buildings && metrics.sustainedAntiAir >= 2 && counts.spells >= 1) labels.push('Control');
    if (averages.cost <= poolCost - .35 && metrics.cheap >= 4 && counts.winConditions) labels.push('Cycle');
    if (metrics.fastWinConditions && metrics.fast >= 2 && metrics.cheap >= 3) labels.push('Bridge Pressure');
    if (counts.winConditions >= 2 && metrics.fast >= 2 || counts.swarm >= 2 && metrics.fastWinConditions) labels.push('Split Lane');
    if (metrics.flying >= 3 || metrics.flying >= 2 && metrics.heavyWinConditions && counts.antiAir >= 3) labels.push('Air Pressure');
    if (metrics.siege) labels.push('Siege');
    if (counts.spells >= 2 && metrics.towerSpells >= 1 && metrics.defense >= 55) labels.push('Spell Control');
    if (counts.buildings && metrics.defensiveTroops >= 4 && metrics.defense >= 65) labels.push('Defensive');
    if (labels.length > 1) labels.push('Hybrid');
    return labels.length ? labels : ['Hybrid'];
  }

  function analyzeDeck(input) {
    const { selected, unique, errors } = collect(input), summary = composition(selected);
    const counterCoverage = threats.map(threatId => {
      const answers = unique.map(answerId => ({ answerId, score: counterScore(answerId, threatId) })).sort((a, b) => b.score - a.score);
      const best = answers[0];
      return { threatId, answerId: best?.score ? best.answerId : null, score: best?.score || 0, reason: best?.score ? counterReason(best.answerId, threatId) : 'No compatible damage in the selected cards.' };
    });
    summary.metrics.defense = round(mean(counterCoverage.map(c => c.score)));
    const synergies = [];
    for (let i = 0; i < unique.length; i++) for (let j = i + 1; j < unique.length; j++) synergies.push(synergy(unique[i], unique[j]));
    summary.metrics.synergy = round(mean(synergies.map(s => s.score)));
    synergies.sort((a, b) => b.score - a.score || a.aId.localeCompare(b.aId));
    const { counts: c, averages: a, metrics: m } = summary, strengths = [], weaknesses = [];
    if (m.sustainedAntiAir >= 3 && c.spells) strengths.push('Strong sustained anti-air with spell backup.');
    if (m.rangedSupport >= 2 && c.winConditions) strengths.push('Multiple ranged supports protect structure pressure.');
    if (a.hp >= poolHp * 1.15) strengths.push('High defensive HP per deployment.');
    if (c.splash >= 3) strengths.push('Several ways to clear clustered swarms.');
    if (m.cheap >= 4) strengths.push('Cheap cards create fast rotations.');
    if (c.buildings) strengths.push('A defensive building diverts structure attackers.');
    if (m.slow && c.splash) strengths.push('Movement control gives area damage time to work.');
    if (m.flying >= 2) strengths.push('Flying pressure bypasses ground-only defenders.');
    if (c.winConditions >= 2) strengths.push('Multiple win conditions support lane changes.');
    if (a.cost > poolCost + .25) weaknesses.push('Expensive average cost demands careful Aether banking.');
    if (m.cheap < 3) weaknesses.push('Limited cheap cycle makes missed trades expensive.');
    if (m.sustainedAntiAir < 2) weaknesses.push('Thin sustained anti-air; spells and short-range auras need careful timing.');
    if (c.splash < 2) weaknesses.push('Limited swarm clear against clustered pressure.');
    if (!c.spells) weaknesses.push('No spell coverage for urgent removals or clustered defenders.');
    if (!c.winConditions) weaknesses.push('No dedicated structure attacker to anchor Crown Tower pressure.');
    if (!m.rangedSupport && c.winConditions) weaknesses.push('Structure attackers lack ranged troop support.');
    if (c.spells >= 3) weaknesses.push('Three spells reduce persistent troops and counter-push bodies.');
    const gaps = counterCoverage.filter(answer => answer.score < 50);
    if (gaps.length) weaknesses.push(`Limited direct answers to ${gaps.slice(0, 2).map(g => cards[g.threatId].name).join(' and ')}.`);
    const archetypes = classifications(c, a, m), primary = archetypes.filter(label => label !== 'Hybrid').slice(0, 2);
    const profile = !unique.length ? 'No cards selected' : primary.length > 1 ? `${primary.join(' / ')} Hybrid` : primary[0] || 'Hybrid';
    return { ids: unique, valid: !errors.length, isComplete: unique.length === 8 && !errors.length, errors,
      ...summary, archetypes, profile, strengths, weaknesses,
      synergies, counterCoverage, winConditions: selected.filter(f => f.win).map(f => f.id), cheapCycle: selected.filter(f => f.cost <= cheapLimit).sort((a, b) => a.cost - b.cost).map(f => f.id) };
  }

  function cardRole(f) {
    if (f.spell) return f.card.dotDamage ? 'Area denial spell' : f.cost <= cheapLimit ? 'Cheap swarm-clear spell' : 'Heavy area damage spell';
    if (f.building) return 'Defensive anchor and structure-attacker pull';
    if (f.win) return f.flying ? 'Flying structure pressure with a defensive ring' : f.card.charger ? 'Fast charging win condition' : 'Durable structure-targeting tank';
    if (f.slow) return 'Ground control frontline';
    if (f.swarm) return f.flying ? 'Air swarm damage and counter-push' : 'Fast ground swarm and cycle pressure';
    if (f.card.splash) return 'Ranged splash support and anti-air';
    if (f.flying) return 'Flying skirmisher and anti-air support';
    if (f.range >= poolRange) return 'Ranged damage and anti-air support';
    return 'Ground frontline and defensive fighter';
  }

  function cardIntel(id) {
    if (!features[id]) return null;
    if (intelligence.has(id)) return structuredClone(intelligence.get(id));
    const f = features[id], suggestedUses = [];
    if (f.spell) {
      suggestedUses.push('Aim at clustered enemies to trade against several units.');
      if (f.card.dotDamage) suggestedUses.push('Cover a committed push or slow troops so they remain inside the zone.');
      if (f.towerSpell) suggestedUses.push('Include a Crown Tower in the impact when troop removal still provides value.');
      else suggestedUses.push('Target troops; this spell deals no structure damage.');
    } else {
      if (f.win) suggestedUses.push('Stage a push with troop support and spell coverage for defenders.');
      if (f.building) suggestedUses.push('Place centrally to pull structure attackers and cover a threatened lane.');
      if (f.troop && f.range >= poolRange && !f.win) suggestedUses.push('Deploy behind a durable frontline and preserve surviving ranged damage.');
      if (f.swarm) suggestedUses.push('Surround single-target threats; separate deployments from enemy splash.');
      if (f.flying) suggestedUses.push('Punish ground-only defenses while tracking observed anti-air cards.');
      if (f.slow) suggestedUses.push('Meet ground pressure early; repeated hits refresh the movement slow.');
      if (f.auraDps) suggestedUses.push('Keep nearby troops inside the ring; it pulses every few seconds and cannot replace ranged anti-air.');
      if (!suggestedUses.length) suggestedUses.push('Defend efficiently, then support the surviving fighter on a counter-push.');
    }
    const bestAgainst = ids.filter(other => other !== id && !features[other].spell && canDamage(id, other))
      .map(other => ({ id: other, score: counterScore(id, other), reason: counterReason(id, other) }))
      .sort((a, b) => b.score - a.score || a.id.localeCompare(b.id)).slice(0, 4);
    let weakAgainst;
    if (f.spell) {
      // Spells cannot be intercepted by attacking their nonexistent body. Their
      // practical resistance is surviving the full stationary effect instead.
      weakAgainst = troops.filter(t => t.id !== id && t.unitHp > spellDamage(f, t, true))
        .map(t => ({ id: t.id, score: Math.round(clamp(1 - spellDamage(f, t, true) / t.unitHp) * 100), reason: 'Survives the full spell effect; spread troops and leave persistent damage zones.' }))
        .sort((a, b) => b.score - a.score).slice(0, 4);
    } else {
      weakAgainst = ids.filter(other => other !== id && canDamage(other, id))
        .map(other => ({ id: other, score: counterScore(other, id), reason: counterReason(other, id) }))
        .sort((a, b) => b.score - a.score || a.id.localeCompare(b.id)).slice(0, 4);
    }
    const partners = ids.filter(other => other !== id).map(other => {
      const pair = synergy(id, other);
      return { id: other, score: pair.score, reason: pair.reasons.slice(0, 2).join(' ') };
    }).sort((a, b) => b.score - a.score || a.id.localeCompare(b.id)).slice(0, 4);
    const offensive = partners.find(p => !features[p.id].building) || partners[0] || null;
    const result = { id, role: cardRole(f), suggestedUses, bestAgainst, weakAgainst, partners,
      bestDefensiveAnswer: weakAgainst[0] || null, bestOffensivePartner: offensive };
    intelligence.set(id, result);
    return structuredClone(result);
  }

  function matchup(ownIds, enemyIds) {
    const own = analyzeDeck(ownIds), enemy = analyzeDeck(enemyIds);
    const defenses = (report, against) => against.ids.filter(id => !features[id].spell).map(id => {
      const answers = report.ids.map(answerId => ({ id: answerId, score: counterScore(answerId, id) })).sort((a, b) => b.score - a.score);
      return { id, score: answers[0]?.score || 0, answerId: answers[0]?.score ? answers[0].id : null,
        reason: answers[0]?.score ? counterReason(answers[0].id, id) : 'No compatible answer in this deck.' };
    });
    const ownAnswers = defenses(own, enemy), enemyAnswers = defenses(enemy, own);
    const weightedCoverage = entries => {
      const weight = entries.reduce((sum, entry) => sum + (features[entry.id].win ? 1.5 : 1), 0);
      return weight ? entries.reduce((sum, entry) => sum + entry.score * (features[entry.id].win ? 1.5 : 1), 0) / weight : 0;
    };
    let score = own.ids.length && enemy.ids.length ? Math.round(clamp((weightedCoverage(ownAnswers) - weightedCoverage(enemyAnswers)) * 1.4 + (own.metrics.synergy - enemy.metrics.synergy) * .12, -100, 100)) : 0;
    const label = score >= 12 ? 'Favorable mechanical coverage' : score <= -12 ? 'Unfavorable mechanical coverage' : 'Even mechanical coverage';
    const strategy = own.archetypes.includes('Split Lane') ? 'Split pressure after an observed heavy commitment; reserve an answer for the return lane.'
      : own.archetypes.includes('Cycle') || own.archetypes.includes('Bridge Pressure') ? 'Rotate cheap defense and pressure the lane whose observed counters are out of cycle.'
      : own.archetypes.includes('Beatdown') ? 'Build one supported lane from the back, keeping Aether for anti-air and swarm removal.'
      : own.archetypes.includes('Spell Control') ? 'Defend centrally, preserve support troops, and take spell value when troops overlap a Crown Tower.'
      : 'Defend the committed lane efficiently, then support surviving troops into a counter-push.';
    const threatList = ownAnswers.map(t => ({ ...t, score: 100 - t.score })).sort((a, b) => b.score - a.score).slice(0, 4);
    const reasons = [];
    if (ownAnswers.length) reasons.push(`Your best answers average ${Math.round(weightedCoverage(ownAnswers))}/100 against the opposing composition.`);
    if (enemyAnswers.length) reasons.push(`Their best answers average ${Math.round(weightedCoverage(enemyAnswers))}/100 against your composition.`);
    if (own.averages.cost < enemy.averages.cost) reasons.push('Your lower average cost supports faster rotations.');
    if (own.metrics.flying && enemy.metrics.sustainedAntiAir < 2) reasons.push('Flying pressure tests limited sustained anti-air.');
    return { score, label, advantage: label, laneStrategy: strategy, threats: threatList,
      counters: ownAnswers.filter(t => t.answerId).sort((a, b) => b.score - a.score).slice(0, 5).map(t => ({ id: t.answerId, againstId: t.id, score: t.score, reason: t.reason })),
      reasons, disclaimer: 'Composition advantage only, not a win rate. Placement, timing, cycle, tower state and player decisions determine the battle.' };
  }

  function enumerate() {
    if (candidates) return candidates;
    candidates = [];
    const choose = (offset, selected) => {
      if (selected.length === 8) {
        const summary = composition(selected.map(id => features[id]));
        const { counts: c, metrics: m } = summary;
        // Baseline coherence shared by all personalities.
        if (c.winConditions < 1 || c.spells < 1 || m.sustainedAntiAir < 2 || c.antiAir < 3 || m.defensiveTroops < 3 || m.rangedSupport < 1 || m.cheap < 2) return;
        m.defense = mean(threats.map(threat => Math.max(...selected.map(id => counterScore(id, threat)))));
        const scores = [];
        for (let i = 0; i < selected.length; i++) for (let j = i + 1; j < selected.length; j++) scores.push(synergy(selected[i], selected[j]).score);
        m.synergy = mean(scores);
        candidates.push({ ids: selected.slice(), ...summary });
        return;
      }
      for (let index = offset; index <= ids.length - (8 - selected.length); index++) choose(index + 1, [...selected, ids[index]]);
    };
    choose(0, []);
    return candidates;
  }

  function buildAiDeck(style, rng = Math.random) {
    const known = ['beatdown', 'aggro', 'control', 'cycle', 'split', 'spell_cycle', 'counter'];
    const normalized = String(style || '').toLowerCase().replace(/[ -]+/g, '_');
    style = ({ split_lane: 'split', spellcycle: 'spell_cycle', counter_push: 'counter', bridge_pressure: 'aggro' })[normalized] || normalized;
    if (!known.includes(style)) style = 'control';
    if (!styleCandidates.has(style)) {
      const ranked = enumerate().filter(deck => {
        const { counts: c, metrics: m, averages: a } = deck;
        if (style !== 'spell_cycle' && c.spells > 2) return false;
        if (style === 'beatdown') return m.heavyWinConditions >= 1 && m.rangedSupport >= 2 && a.cost >= 3.625 && a.cost <= 4.25;
        if (style === 'aggro') return m.fastWinConditions >= 1 && m.fast >= 3 && m.cheap >= 3 && a.cost <= 3.875;
        if (style === 'control') return c.buildings >= 1 && m.defense >= 65 && a.cost <= 4.125;
        if (style === 'cycle') return m.fastWinConditions >= 1 && m.cheap >= 4 && a.cost <= 3.375;
        if (style === 'split') return c.winConditions >= 2 && m.fast >= 2 && m.cheap >= 2 && a.cost <= 4.125;
        if (style === 'spell_cycle') return c.spells >= 2 && m.towerSpells >= 2 && c.buildings >= 1 && m.cheap >= 3 && a.cost <= 3.875;
        return c.buildings >= 1 && m.frontline >= 2 && m.defensiveTroops >= 4 && a.cost <= 4.125;
      }).map(deck => {
        const { counts: c, metrics: m, averages: a } = deck;
        let score = m.defense * .35 + m.synergy * .30 + Math.min(m.rangedSupport, 2) * 3 + Math.min(c.splash, 3) * 2;
        if (style === 'beatdown') score += m.heavyWinConditions * 8 + m.frontline * 3 + m.rangedSupport * 5 - Math.abs(a.cost - 3.9) * 9;
        if (style === 'aggro') score += m.fast * 5 + m.fastWinConditions * 8 + m.cheap * 3 - a.cost * 6;
        if (style === 'control') score += m.defense * .40 + c.buildings * 8 + c.spells * 3 + m.slow * 5 - a.cost * 3;
        if (style === 'cycle') score += m.cheap * 7 + m.fast * 4 - a.cost * 17 - m.cycleCost * 2;
        if (style === 'split') score += c.winConditions * 9 + m.fast * 4 + c.swarm * 4 + m.cheap * 3 - a.cost * 5;
        if (style === 'spell_cycle') score += m.towerSpells * 10 + m.cheap * 5 + m.defense * .2 - a.cost * 10 - m.cycleCost;
        if (style === 'counter') score += m.defense * .35 + m.frontline * 5 + m.slow * 6 + m.rangedSupport * 4 + c.swarm * 2 - a.cost * 4;
        return { ...deck, score };
      }).sort((a, b) => b.score - a.score || a.ids.join(',').localeCompare(b.ids.join(',')));
      if (!ranked.length) throw new Error(`The card library cannot produce a coherent ${style} deck.`);
      // Variety remains inside a narrow, fully constrained top score band.
      const best = ranked[0].score;
      styleCandidates.set(style, ranked.filter(deck => deck.score >= best - 7).slice(0, 24));
    }
    const pool = styleCandidates.get(style);
    let sample;
    try { sample = finite(Number(rng())); } catch { sample = .5; }
    const selected = pool[Math.floor(clamp(sample, 0, 1 - Number.EPSILON) * pool.length)].ids.slice();
    // Card order is shuffled independently without changing deck composition.
    for (let i = selected.length - 1; i > 0; i--) {
      let value;
      try { value = finite(Number(rng())); } catch { value = .5; }
      const j = Math.floor(clamp(value, 0, 1 - Number.EPSILON) * (i + 1));
      [selected[i], selected[j]] = [selected[j], selected[i]];
    }
    return selected;
  }

  return { cards, analyzeDeck, synergy, cardIntel, matchup, buildAiDeck, canDamage, counterScore };
}
