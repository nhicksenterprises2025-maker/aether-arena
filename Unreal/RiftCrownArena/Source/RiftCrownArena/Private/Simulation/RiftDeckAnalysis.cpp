#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>

namespace rift {
namespace {
double Clamp(double n, double a = 0, double b = 1) {
    return std::max(a, std::min(b, n));
}
double Round(double n) {
    return std::round(n * 100) / 100;
}
bool Troop(const Card &c) {
    return !c.spell && !c.building;
}
bool DirectAir(const Card &c) {
    return !c.spell && !c.structuresOnly && c.canHitAir;
}
bool Area(const Card &c) {
    return c.spell || c.splash > 0 || c.auraDamage > 0;
}
bool AntiAir(const Card &c) {
    return c.spell || DirectAir(c) || c.auraDamage > 0;
}
struct Pool {
    double hp = 0, range = 0, move = 0, cost = 0;
    int cheap = 0;
};
const Pool &P() {
    static const Pool p = []() {
        Pool v;
        int n = 0;
        for (const auto &c : Cards()) {
            v.cost += c.cost;
            if (Troop(c)) {
                ++n;
                v.hp += c.hp;
                v.range += c.range;
                v.move += c.moveSpeed;
            }
        }
        v.hp /= n;
        v.range /= n;
        v.move /= n;
        v.cost /= Cards().size();
        v.cheap = static_cast<int>(std::floor(v.cost));
        return v;
    }();
    return p;
}
bool Direct(const Card &a, const Card &b) {
    if (b.spell)
        return false;
    if (a.spell)
        return b.building ? a.towerDamage > 0 : a.damage > 0;
    if (a.building)
        return Troop(b) && (!b.flying || a.canHitAir);
    if (a.structuresOnly)
        return b.building;
    return !b.flying || a.canHitAir;
}
double DPS(const Card &c) {
    return c.attackInterval > 0 ? c.damage / c.attackInterval * c.count : 0;
}
double AttackDPS(const Card &a, const Card &b) {
    double d =
        Direct(a, b) ? DPS(a) * (Troop(b) && a.splash > 0 ? std::min<double>(b.count, 1 + a.splash) : 1) : 0;
    if (a.auraInterval > 0 && Troop(b))
        d += a.auraDamage / a.auraInterval * std::min<double>(b.count, 1 + a.auraRadius);
    return d;
}
double Charge(const Card &a, const Card &b) {
    return a.chargeDamage > 0 && b.building && Direct(a, b)
               ? std::max(0., a.chargeDamage - a.damage) * a.count
               : 0;
}
struct Metrics {
    int defensive = 0, fastWin = 0, heavyWin = 0, siege = 0, swarm = 0, slow = 0, towerSpells = 0, cycle = 0;
};
Metrics Extra(const std::vector<std::string> &deck) {
    Metrics m;
    std::vector<int> costs;
    for (const auto &id : deck)
        if (const auto *c = FindCard(id)) {
            costs.push_back(c->cost);
            m.defensive += Troop(*c) && !c->structuresOnly;
            m.fastWin += Troop(*c) && c->structuresOnly && c->moveSpeed >= P().move;
            m.heavyWin += Troop(*c) && c->structuresOnly && c->hp >= P().hp && c->moveSpeed < P().move;
            m.siege += Troop(*c) && c->structuresOnly && !c->flying && c->moveSpeed < P().move;
            m.swarm += c->count > 1;
            m.slow += c->slowPct > 0;
            m.towerSpells += c->spell && c->towerDamage > 0;
        }
    std::sort(costs.begin(), costs.end());
    for (std::size_t i = 0; i < std::min<std::size_t>(4, costs.size()); ++i)
        m.cycle += costs[i];
    return m;
}
double Random(std::uint32_t &s) {
    s += 0x6d2b79f5u;
    std::uint32_t t = s;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.;
}
} // namespace

double CounterScore(const Card &d, const Card &t) {
    if (t.spell || (!Direct(d, t) && !(d.auraDamage > 0 && Troop(t))))
        return 0;
    double result = 0;
    if (d.spell) {
        const double dwell = std::min(d.dotDuration, 2 * d.spellRadius / std::max(.5, t.moveSpeed));
        const double damage = t.building ? d.towerDamage : d.damage + d.dotDamage * dwell;
        result = Clamp(damage / std::max(1., t.hp)) * 57 + (damage >= t.hp ? 23 : 0) +
                 Clamp(t.cost / std::max(.1, static_cast<double>(d.cost)), 0, 2) * 10;
        if (t.building && damage < t.hp)
            result *= .72;
    } else {
        const double outgoing = AttackDPS(d, t), incoming = AttackDPS(t, d),
                     start = !Direct(d, t) ? d.auraInterval : .25;
        const double approach = d.building ? 0 : std::max(0., t.range - d.range) / std::max(.5, d.moveSpeed);
        const double loss = incoming * approach + Charge(t, d);
        const double casualty = d.count > 1 && incoming > 0 ? (Area(t) ? .56 : .76) : 1;
        const double killTime =
            std::max(1., t.hp * t.count - Charge(d, t)) / std::max(1., outgoing * casualty) + start +
            approach;
        const double decay = d.building && d.lifetime > 0 ? d.hp * d.count / d.lifetime : 0;
        const double survival =
            incoming + decay > 0 ? std::max(0., d.hp * d.count - loss) / (incoming + decay) : 30;
        result = 34 * Clamp(9 / std::max(1., killTime)) + 35 * Clamp(survival / std::max(.25, killTime)) +
                 12 * Clamp(t.cost / std::max(.1, static_cast<double>(d.cost)), 0, 1.5);
        if (t.flying && DirectAir(d))
            result += 4;
        if (t.count > 1 && d.splash > 0)
            result += 10;
        if (d.slowPct > 0 && !t.flying && Troop(t) && Direct(d, t))
            result += d.slowPct * Clamp(d.slowDuration / std::max(.1, d.attackInterval)) *
                      Clamp(t.moveSpeed / std::max(.1, P().move), .5, 1.5) *
                      (t.structuresOnly || t.chargeDamage > 0 ? 33 : 16);
        if (d.building && t.structuresOnly) {
            double diverted = std::max(0., d.hp * d.count - Charge(t, d)) / std::max(1., incoming + decay);
            result += 10 + Clamp(diverted / 7) * 14;
        }
        if (d.auraDamage > 0 && Troop(t))
            result += Clamp(d.stunDuration / std::max(.1, d.auraInterval)) * 28;
        if (!Direct(d, t) && d.auraDamage > 0)
            result *= .64;
    }
    return std::round(Clamp(result, 0, 100));
}
double PairSynergy(const Card &a, const Card &b) {
    if (a.id == b.id)
        return 0;
    double s = 32;
    auto tank = [](const Card &t, const Card &u) {
        return Troop(t) && t.hp >= P().hp && Troop(u) && !u.structuresOnly && u.range >= P().range;
    };
    if (tank(a, b) || tank(b, a))
        s += 19;
    if (a.structuresOnly != b.structuresOnly && Troop(a.structuresOnly ? b : a))
        s += 11;
    if (AntiAir(a) != AntiAir(b))
        s += DirectAir(a) || DirectAir(b) || a.spell || b.spell ? 9 : 4;
    if (Area(a) != Area(b))
        s += 9;
    if (a.spell != b.spell) {
        const Card &sp = a.spell ? a : b;
        const Card &u = a.spell ? b : a;
        if (u.structuresOnly || u.building || u.count > 1)
            s += 11;
        if (sp.dotDamage > 0 && u.slowPct > 0)
            s += 12;
    }
    if ((a.slowPct > 0 && Troop(b) && (b.structuresOnly || b.range > P().range)) ||
        (b.slowPct > 0 && Troop(a) && (a.structuresOnly || a.range > P().range)))
        s += 11;
    if ((a.auraDamage > 0 && Troop(b) && b.range < P().range) ||
        (b.auraDamage > 0 && Troop(a) && a.range < P().range))
        s += 8;
    if ((a.count > 1 && Troop(b) && b.hp >= P().hp) || (b.count > 1 && Troop(a) && a.hp >= P().hp))
        s += 7;
    if ((a.cost <= P().cheap) != (b.cost <= P().cheap))
        s += 5;
    int coverage = 0;
    for (const auto &t : Cards())
        if (Troop(t)) {
            double ac = CounterScore(a, t), bc = CounterScore(b, t);
            coverage += std::min(ac, bc) < 40 && std::max(ac, bc) >= 65;
        }
    if (coverage >= 2)
        s += std::min(10, coverage * 2);
    if (a.cost + b.cost > 10)
        s -= (a.cost + b.cost - 10) * 5;
    if (a.spell && b.spell)
        s -= 7;
    if (a.structuresOnly && b.structuresOnly)
        s -= 4;
    if (!a.spell && !b.spell && !a.canHitAir && !b.canHitAir && a.auraDamage == 0 && b.auraDamage == 0)
        s -= 6;
    return std::round(Clamp(s, 0, 100));
}
std::string CounterReason(const Card &d, const Card &t) {
    if (t.spell)
        return "A spell has no troop body to attack.";
    if (!Direct(d, t) && !(d.auraDamage > 0 && Troop(t)))
        return t.flying ? "Cannot damage this flying target." : "Target restrictions prevent damage.";
    if (d.spell) {
        const double damage =
            t.building ? d.towerDamage
                       : d.damage + d.dotDamage * std::min(d.dotDuration,
                                                           2 * d.spellRadius / std::max(.5, t.moveSpeed));
        return std::string(damage >= t.hp ? "Removes " : "Damages ") +
               (t.count > 1  ? "clustered swarm members"
                : t.building ? "the building"
                             : "the troop") +
               (d.dotDamage > 0 && !t.building ? "; damage over time depends on remaining inside the zone."
                                               : ".");
    }
    std::vector<std::string> reasons;
    if (d.building && t.structuresOnly)
        reasons.emplace_back("Pulls the structure attacker away from Crown Towers");
    if (d.chargeDamage > 0 && t.building)
        reasons.emplace_back("A running approach adds opening charge damage");
    if (t.flying && DirectAir(d))
        reasons.emplace_back("Direct attacks reach flying troops");
    if (d.flying && !Direct(t, d) && !(t.auraDamage > 0 && Troop(d)))
        reasons.emplace_back("Flies above this card's ground attacks");
    if (t.count > 1 && d.splash > 0)
        reasons.emplace_back("Splash hits clustered swarm members");
    if (d.slowPct > 0 && !t.flying)
        reasons.emplace_back("Refreshing movement slow delays ground pressure");
    if (d.auraDamage > 0 && Troop(t))
        reasons.emplace_back("Nearby troops take delayed ring damage and stun");
    if (d.range > t.range && Direct(d, t))
        reasons.emplace_back("Range creates a first-strike window");
    if (reasons.empty())
        reasons.emplace_back(d.count > 1 ? "Multiple units concentrate damage"
                                         : "HP and damage support a defensive trade");
    std::string result;
    for (const auto &reason : reasons) {
        if (!result.empty())
            result += "; ";
        result += reason;
    }
    return result + ".";
}
std::vector<std::string> PairSynergyReasons(const Card &a, const Card &b) {
    if (a.id == b.id)
        return {"Duplicate cards cannot share a deck."};
    std::vector<std::string> reasons;
    auto tank = [](const Card &t, const Card &u) {
        return Troop(t) && t.hp >= P().hp && Troop(u) && !u.structuresOnly && u.range >= P().range;
    };
    if (tank(a, b) || tank(b, a))
        reasons.emplace_back("A durable frontline protects ranged damage.");
    if (a.structuresOnly != b.structuresOnly && Troop(a.structuresOnly ? b : a))
        reasons.emplace_back("Troop damage clears defenders for the structure attacker.");
    if (AntiAir(a) != AntiAir(b))
        reasons.emplace_back(DirectAir(a) || DirectAir(b) || a.spell || b.spell
                                 ? "Adds damage against flying defenders."
                                 : "The nearby electric ring adds limited air coverage.");
    if (Area(a) != Area(b))
        reasons.emplace_back("Area damage complements single-target pressure.");
    if (a.spell != b.spell) {
        const auto &spell = a.spell ? a : b, &unit = a.spell ? b : a;
        if (unit.structuresOnly || unit.building || unit.count > 1)
            reasons.emplace_back(unit.building
                                     ? "Area spell damage supports a defensive building's single-target fire."
                                     : "A spell removes clustered defenders during a push.");
        if (spell.dotDamage > 0 && unit.slowPct > 0)
            reasons.emplace_back("Movement slow increases time inside the damage zone.");
    }
    if ((a.slowPct > 0 && Troop(b) && (b.structuresOnly || b.range > P().range)) ||
        (b.slowPct > 0 && Troop(a) && (a.structuresOnly || a.range > P().range)))
        reasons.emplace_back("Refreshing slow buys time for supporting damage or a structure push.");
    if ((a.auraDamage > 0 && Troop(b) && b.range < P().range) ||
        (b.auraDamage > 0 && Troop(a) && a.range < P().range))
        reasons.emplace_back("A nearby frontline keeps defenders in the ring's damage and stun radius.");
    if ((a.count > 1 && Troop(b) && b.hp >= P().hp) || (b.count > 1 && Troop(a) && a.hp >= P().hp))
        reasons.emplace_back("High-HP pressure screens multiple damage dealers.");
    if ((a.cost <= P().cheap) != (b.cost <= P().cheap))
        reasons.emplace_back("The cheaper card leaves Aether for support and rotation.");
    int coverage = 0;
    for (const auto &t : Cards())
        if (Troop(t)) {
            const double ac = CounterScore(a, t), bc = CounterScore(b, t);
            coverage += std::min(ac, bc) < 40 && std::max(ac, bc) >= 65;
        }
    if (coverage >= 2)
        reasons.emplace_back("Their defensive target coverage fills different gaps.");
    if (a.cost + b.cost > 10)
        reasons.emplace_back("The combined cost requires staging the push across regeneration.");
    if (a.spell && b.spell)
        reasons.emplace_back("Two spells need troops or a building to hold the lane.");
    if (a.structuresOnly && b.structuresOnly)
        reasons.emplace_back("Two structure attackers need separate troop support.");
    if (!a.spell && !b.spell && !a.canHitAir && !b.canHitAir && a.auraDamage == 0 && b.auraDamage == 0)
        reasons.emplace_back("The pair needs separate air coverage.");
    if (reasons.empty())
        reasons.emplace_back("Similar roles offer reliable rotation but limited complementary coverage.");
    return reasons;
}
CardIntelligence IntelligenceFor(const std::string &id) {
    CardIntelligence intel;
    const auto *card = FindCard(id);
    if (!card)
        return intel;
    const auto &c = *card;
    intel.id = id;
    if (c.spell)
        intel.role = c.dotDamage > 0       ? "Area denial spell"
                     : c.cost <= P().cheap ? "Cheap swarm-clear spell"
                                           : "Heavy area damage spell";
    else if (c.building)
        intel.role = "Defensive anchor and structure-attacker pull";
    else if (c.structuresOnly)
        intel.role = c.flying             ? "Flying structure pressure with a defensive ring"
                     : c.chargeDamage > 0 ? "Fast charging win condition"
                                          : "Durable structure-targeting tank";
    else if (c.slowPct > 0)
        intel.role = "Ground control frontline";
    else if (c.count > 1)
        intel.role = c.flying ? "Air swarm damage and counter-push" : "Fast ground swarm and cycle pressure";
    else if (c.splash > 0)
        intel.role = "Ranged splash support and anti-air";
    else if (c.flying)
        intel.role = "Flying skirmisher and anti-air support";
    else if (c.range >= P().range)
        intel.role = "Ranged damage and anti-air support";
    else
        intel.role = "Ground frontline and defensive fighter";
    auto &uses = intel.suggestedUses;
    if (c.spell) {
        uses.emplace_back("Aim at clustered enemies to trade against several units.");
        if (c.dotDamage > 0)
            uses.emplace_back("Cover a committed push or slow troops so they remain inside the zone.");
        uses.emplace_back(c.towerDamage > 0
                              ? "Include a Crown Tower in the impact when troop removal still provides value."
                              : "Target troops; this spell deals no structure damage.");
    } else {
        if (c.structuresOnly)
            uses.emplace_back("Stage a push with troop support and spell coverage for defenders.");
        if (c.building)
            uses.emplace_back("Place centrally to pull structure attackers and cover a threatened lane.");
        if (Troop(c) && c.range >= P().range && !c.structuresOnly)
            uses.emplace_back("Deploy behind a durable frontline and preserve surviving ranged damage.");
        if (c.count > 1)
            uses.emplace_back("Surround single-target threats; separate deployments from enemy splash.");
        if (c.flying)
            uses.emplace_back("Punish ground-only defenses while tracking observed anti-air cards.");
        if (c.slowPct > 0)
            uses.emplace_back("Meet ground pressure early; repeated hits refresh the movement slow.");
        if (c.auraDamage > 0)
            uses.emplace_back("Keep nearby troops inside the ring; it pulses every few seconds and cannot "
                              "replace ranged anti-air.");
        if (uses.empty())
            uses.emplace_back("Defend efficiently, then support the surviving fighter on a counter-push.");
    }
    for (const auto &other : Cards())
        if (other.id != id) {
            if (!other.spell && (Direct(c, other) || (c.auraDamage > 0 && Troop(other))))
                intel.bestAgainst.push_back({other.id, CounterReason(c, other), CounterScore(c, other)});
            if (c.spell) {
                const double damage = c.damage + c.dotDamage * c.dotDuration;
                if (Troop(other) && other.hp > damage)
                    intel.weakAgainst.push_back(
                        {other.id,
                         "Survives the full spell effect; spread troops and leave persistent damage zones.",
                         std::round(Clamp(1 - damage / other.hp) * 100)});
            } else if (Direct(other, c) || (other.auraDamage > 0 && Troop(c)))
                intel.weakAgainst.push_back({other.id, CounterReason(other, c), CounterScore(other, c)});
            const auto reasons = PairSynergyReasons(c, other);
            std::string reason = reasons.front();
            if (reasons.size() > 1)
                reason += " " + reasons[1];
            intel.partners.push_back({other.id, reason, PairSynergy(c, other)});
        }
    auto rank = [](std::vector<CardRelation> &rows) {
        std::sort(rows.begin(), rows.end(), [](const CardRelation &a, const CardRelation &b) {
            return a.score != b.score ? a.score > b.score : a.id < b.id;
        });
        if (rows.size() > 4)
            rows.resize(4);
    };
    rank(intel.bestAgainst);
    rank(intel.weakAgainst);
    rank(intel.partners);
    if (!intel.weakAgainst.empty())
        intel.bestDefensiveAnswer = intel.weakAgainst.front();
    for (const auto &partner : intel.partners)
        if (const auto *p = FindCard(partner.id); p && !p->building) {
            intel.bestOffensivePartner = partner;
            break;
        }
    if (intel.bestOffensivePartner.id.empty() && !intel.partners.empty())
        intel.bestOffensivePartner = intel.partners.front();
    return intel;
}
DeckAnalysis AnalyzeDeck(const std::vector<std::string> &input) {
    DeckAnalysis d;
    std::set<std::string> seen;
    std::vector<const Card *> cards;
    std::vector<std::string> ids;
    int entities = 0;
    for (const auto &id : input)
        if (const Card *c = FindCard(id); c && seen.insert(id).second) {
            cards.push_back(c);
            ids.push_back(id);
            d.averageCost += c->cost;
            if (c->cost < 11)
                ++d.costCurve[c->cost];
            d.spells += c->spell;
            d.buildings += c->building;
            d.troops += Troop(*c);
            d.air += Troop(*c) && c->flying;
            d.ground += Troop(*c) && !c->flying;
            d.antiAir += AntiAir(*c);
            d.sustainedAntiAir += DirectAir(*c);
            d.winConditions += Troop(*c) && c->structuresOnly;
            d.cheap += c->cost <= P().cheap;
            d.ranged += Troop(*c) && !c->structuresOnly && c->range >= P().range;
            d.frontline += Troop(*c) && c->hp >= P().hp;
            d.splash += Area(*c);
            d.fast += Troop(*c) && c->moveSpeed >= P().move;
            if (!c->spell) {
                ++entities;
                d.averageHP += c->hp * c->count;
                d.deploymentDPS += DPS(*c);
                d.averageRange += c->range;
            }
        }
    if (cards.empty())
        return d;
    d.averageCost = Round(d.averageCost / cards.size());
    if (entities) {
        d.averageHP = Round(d.averageHP / entities);
        d.deploymentDPS = Round(d.deploymentDPS / entities);
        d.averageRange = Round(d.averageRange / entities);
    }
    int threats = 0;
    for (const auto &t : Cards())
        if (Troop(t)) {
            ++threats;
            double best = 0;
            for (const auto *c : cards)
                best = std::max(best, CounterScore(*c, t));
            d.defenseScore += best;
        }
    d.defenseScore = Round(d.defenseScore / threats);
    int pairs = 0;
    for (std::size_t i = 0; i < cards.size(); ++i)
        for (std::size_t j = i + 1; j < cards.size(); ++j) {
            ++pairs;
            d.synergy += PairSynergy(*cards[i], *cards[j]);
        }
    if (pairs)
        d.synergy = Round(d.synergy / pairs);
    const auto m = Extra(ids);
    auto label = [&](bool condition, const char *name) {
        if (condition)
            d.archetypes.emplace_back(name);
    };
    label(m.heavyWin && d.frontline >= 2 && d.ranged >= 1, "Beatdown");
    label(d.buildings && d.sustainedAntiAir >= 2 && d.spells, "Control");
    label(d.averageCost <= P().cost - .35 && d.cheap >= 4 && d.winConditions, "Cycle");
    label(m.fastWin && d.fast >= 2 && d.cheap >= 3, "Bridge Pressure");
    label((d.winConditions >= 2 && d.fast >= 2) || (m.swarm >= 2 && m.fastWin), "Split Lane");
    label(d.air >= 3 || (d.air >= 2 && m.heavyWin && d.antiAir >= 3), "Air Pressure");
    label(m.siege > 0, "Siege");
    label(d.spells >= 2 && m.towerSpells && d.defenseScore >= 55, "Spell Control");
    label(d.buildings && m.defensive >= 4 && d.defenseScore >= 65, "Defensive");
    if (d.archetypes.empty() || d.archetypes.size() > 1)
        d.archetypes.emplace_back("Hybrid");
    if (d.sustainedAntiAir >= 3 && d.spells)
        d.strengths.emplace_back("Strong sustained anti-air with spell backup.");
    if (d.ranged >= 2 && d.winConditions)
        d.strengths.emplace_back("Multiple ranged supports protect structure pressure.");
    if (d.averageHP >= P().hp * 1.15)
        d.strengths.emplace_back("High defensive HP per deployment.");
    if (d.splash >= 3)
        d.strengths.emplace_back("Several ways to clear clustered swarms.");
    if (d.cheap >= 4)
        d.strengths.emplace_back("Cheap cards create fast rotations.");
    if (d.buildings)
        d.strengths.emplace_back("A defensive building diverts structure attackers.");
    if (m.slow && d.splash)
        d.strengths.emplace_back("Movement control gives area damage time to work.");
    if (d.air >= 2)
        d.strengths.emplace_back("Flying pressure bypasses ground-only defenders.");
    if (d.winConditions >= 2)
        d.strengths.emplace_back("Multiple win conditions support lane changes.");
    if (d.averageCost > P().cost + .25)
        d.weaknesses.emplace_back("Expensive average cost demands careful Aether banking.");
    if (d.cheap < 3)
        d.weaknesses.emplace_back("Limited cheap cycle makes missed trades expensive.");
    if (d.sustainedAntiAir < 2)
        d.weaknesses.emplace_back(
            "Thin sustained anti-air; spells and short-range auras need careful timing.");
    if (d.splash < 2)
        d.weaknesses.emplace_back("Limited swarm clear against clustered pressure.");
    if (!d.spells)
        d.weaknesses.emplace_back("No spell coverage for urgent removals or clustered defenders.");
    if (!d.winConditions)
        d.weaknesses.emplace_back("No dedicated structure attacker to anchor Crown Tower pressure.");
    if (!d.ranged && d.winConditions)
        d.weaknesses.emplace_back("Structure attackers lack ranged troop support.");
    if (d.spells >= 3)
        d.weaknesses.emplace_back("Three spells reduce persistent troops and counter-push bodies.");
    return d;
}
std::vector<std::string> BuildAIDeck(const std::string &requested, std::uint32_t seed) {
    const std::string style = requested == "beatdown" || requested == "aggro" || requested == "cycle" ||
                                      requested == "split" || requested == "spell_cycle" ||
                                      requested == "counter"
                                  ? requested
                                  : "control";
    struct Candidate {
        std::vector<std::string> ids;
        double score = 0;
    };
    static std::mutex cacheMutex;
    static std::map<std::string, std::vector<Candidate>> cache;
    std::lock_guard<std::mutex> guard(cacheMutex);
    auto &pool = cache[style];
    if (pool.empty())
        for (unsigned mask = 0; mask < (1u << Cards().size()); ++mask) {
            int bits = 0;
            for (unsigned n = mask; n; n &= n - 1)
                ++bits;
            if (bits != 8)
                continue;
            std::vector<std::string> ids;
            for (std::size_t i = 0; i < Cards().size(); ++i)
                if (mask & (1u << i))
                    ids.push_back(Cards()[i].id);
            const auto d = AnalyzeDeck(ids);
            const auto m = Extra(ids);
            if (d.winConditions < 1 || d.spells < 1 || d.sustainedAntiAir < 2 || d.antiAir < 3 ||
                m.defensive < 3 || d.ranged < 1 || d.cheap < 2)
                continue;
            if (style != "spell_cycle" && d.spells > 2)
                continue;
            bool legal = false;
            if (style == "beatdown")
                legal = m.heavyWin >= 1 && d.ranged >= 2 && d.averageCost >= 3.625 && d.averageCost <= 4.25;
            else if (style == "aggro")
                legal = m.fastWin >= 1 && d.fast >= 3 && d.cheap >= 3 && d.averageCost <= 3.875;
            else if (style == "control")
                legal = d.buildings >= 1 && d.defenseScore >= 65 && d.averageCost <= 4.125;
            else if (style == "cycle")
                legal = m.fastWin >= 1 && d.cheap >= 4 && d.averageCost <= 3.375;
            else if (style == "split")
                legal = d.winConditions >= 2 && d.fast >= 2 && d.cheap >= 2 && d.averageCost <= 4.125;
            else if (style == "spell_cycle")
                legal = d.spells >= 2 && m.towerSpells >= 2 && d.buildings >= 1 && d.cheap >= 3 &&
                        d.averageCost <= 3.875;
            else
                legal = d.buildings >= 1 && d.frontline >= 2 && m.defensive >= 4 && d.averageCost <= 4.125;
            if (!legal)
                continue;
            double score = d.defenseScore * .35 + d.synergy * .30 + std::min(d.ranged, 2) * 3 +
                           std::min(d.splash, 3) * 2;
            if (style == "beatdown")
                score += m.heavyWin * 8 + d.frontline * 3 + d.ranged * 5 - std::abs(d.averageCost - 3.9) * 9;
            if (style == "aggro")
                score += d.fast * 5 + m.fastWin * 8 + d.cheap * 3 - d.averageCost * 6;
            if (style == "control")
                score +=
                    d.defenseScore * .40 + d.buildings * 8 + d.spells * 3 + m.slow * 5 - d.averageCost * 3;
            if (style == "cycle")
                score += d.cheap * 7 + d.fast * 4 - d.averageCost * 17 - m.cycle * 2;
            if (style == "split")
                score += d.winConditions * 9 + d.fast * 4 + m.swarm * 4 + d.cheap * 3 - d.averageCost * 5;
            if (style == "spell_cycle")
                score +=
                    m.towerSpells * 10 + d.cheap * 5 + d.defenseScore * .2 - d.averageCost * 10 - m.cycle;
            if (style == "counter")
                score += d.defenseScore * .35 + d.frontline * 5 + m.slow * 6 + d.ranged * 4 + m.swarm * 2 -
                         d.averageCost * 4;
            pool.push_back({ids, score});
        }
    if (pool.empty())
        return DefaultDeck();
    std::sort(pool.begin(), pool.end(), [](const Candidate &a, const Candidate &b) {
        return a.score != b.score ? a.score > b.score : a.ids < b.ids;
    });
    const double best = pool.front().score;
    pool.erase(
        std::remove_if(pool.begin(), pool.end(), [&](const Candidate &c) { return c.score < best - 7; }),
        pool.end());
    if (pool.size() > 24)
        pool.resize(24);
    auto ids = pool[static_cast<std::size_t>(Random(seed) * pool.size())].ids;
    for (std::size_t i = ids.size() - 1; i > 0; --i)
        std::swap(ids[i], ids[static_cast<std::size_t>(Random(seed) * (i + 1))]);
    return ids;
}
} // namespace rift
