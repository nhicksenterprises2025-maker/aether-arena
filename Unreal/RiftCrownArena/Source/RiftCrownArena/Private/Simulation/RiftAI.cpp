#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace rift {
namespace {
int I(Team t) {
    return t == Team::Player ? 0 : 1;
}
double S(Team t) {
    return t == Team::Player ? 1 : -1;
}
double Clamp(double n, double a, double b) {
    return std::max(a, std::min(b, n));
}
double D(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
bool Crown(const Entity &e) {
    return e.kind == EntityKind::Core || e.kind == EntityKind::Guard;
}
struct Style {
    double bank, desperate, reserve, defense, punish, trade;
    int supports;
    std::vector<std::string> push, support, overflow;
};
const Style &Brain(const std::string &s) {
    static const std::map<std::string, Style> styles{
        {"beatdown",
         {9.9,
          7.3,
          2.2,
          1,
          .58,
          .34,
          3,
          {"boulderback", "storm_raven", "rambeast", "ironclad", "arc_mage"},
          {"arc_mage", "ember_archer", "storm_raven", "vampire_bats", "sky_manta", "frost_fang", "ironclad"},
          {"ironclad", "vampire_bats", "ember_archer", "twin_blades"}}},
        {"aggro",
         {7.8,
          6,
          1,
          .76,
          1,
          .74,
          2,
          {"rambeast", "storm_raven", "ironclad", "twin_blades", "boulderback"},
          {"storm_raven", "vampire_bats", "ember_archer", "sky_manta", "frost_fang", "twin_blades",
           "arc_mage"},
          {"vampire_bats", "twin_blades", "ember_archer", "ironclad"}}},
        {"control",
         {9.2,
          6.8,
          2.8,
          1.22,
          .5,
          .16,
          2,
          {"boulderback", "storm_raven", "ironclad", "rambeast", "arc_mage"},
          {"arc_mage", "frost_fang", "ember_archer", "vampire_bats", "ironclad", "sky_manta"},
          {"ember_archer", "ironclad", "twin_blades"}}},
        {"cycle",
         {6.4,
          5.2,
          .8,
          .9,
          .92,
          .48,
          1,
          {"rambeast", "twin_blades", "ironclad", "sky_manta", "ember_archer"},
          {"ember_archer", "twin_blades", "sky_manta", "frost_fang", "arc_mage"},
          {"twin_blades", "ember_archer", "ironclad", "bullet_burst"}}},
        {"split",
         {8,
          6.1,
          1.4,
          .88,
          1.08,
          .60,
          1,
          {"rambeast", "storm_raven", "ironclad", "vampire_bats", "twin_blades"},
          {"ember_archer", "sky_manta", "twin_blades", "frost_fang"},
          {"twin_blades", "vampire_bats", "ember_archer", "ironclad"}}},
        {"spell_cycle",
         {7.4,
          5.8,
          1.5,
          1.02,
          .62,
          .38,
          1,
          {"ironclad", "frost_fang", "rambeast", "sky_manta"},
          {"ember_archer", "arc_mage", "sky_manta"},
          {"bullet_burst", "twin_blades", "ember_archer", "ironclad"}}},
        {"counter",
         {9,
          6.5,
          2.5,
          1.3,
          .72,
          .22,
          3,
          {"boulderback", "frost_fang", "ironclad", "storm_raven", "rambeast"},
          {"arc_mage", "ember_archer", "vampire_bats", "sky_manta", "frost_fang"},
          {"ember_archer", "ironclad", "twin_blades"}}}};
    auto it = styles.find(s);
    return it == styles.end() ? styles.at("control") : it->second;
}
int Priority(const std::vector<std::string> &list, const std::string &id) {
    auto it = std::find(list.begin(), list.end(), id);
    return it == list.end() ? 0 : static_cast<int>(list.end() - it);
}
} // namespace
void Match::Decision(Team team, const std::string &label, const std::string &reason) {
    auto &ai = state_.ai[I(team)];
    const bool changed = ai.decision != label || ai.reason != reason;
    ai.decision = label;
    ai.reason = reason;
    if (changed) {
        auto &e = Emit("ai_decision", team);
        e.reason = label + ": " + reason;
        e.cardId = ai.style;
    }
}
bool Match::AIPlay(Team team, int index, Vec2 p, const std::string &reason, double reserve) {
    const int i = I(team);
    if (index < 0 || index >= 4)
        return false;
    const auto *c = FindCard(state_.hands[i][index]);
    if (!c || c->cost > state_.aether[i] - reserve + 1e-9)
        return false;
    p.x = Clamp(p.x, -arena::DeploymentMaxX, arena::DeploymentMaxX);
    p.z = Clamp(p.z, -arena::HalfHeight + 1.2, arena::HalfHeight - 1.2);
    p = SnapToTile(p);
    if (!CanPlace(team, *c, p)) {
        p.z = S(team) * Clamp(p.z * S(team), 2.2, arena::CoreDepth - 1.1);
        p = SnapToTile(p);
    }
    return Play(team, index, p, reason);
}
void Match::UpdateAI(Team team, double dt) {
    const int side = I(team);
    auto &ai = state_.ai[side];
    if (!ai.enabled)
        return;
    ai.think -= dt;
    if (ai.think > 1e-9 || !(state_.phase == Phase::Regulation || state_.phase == Phase::Overtime))
        return;
    ai.think = .22 + Random() * .20;
    const auto &style = Brain(ai.style);
    const double sign = S(team), bank = state_.aether[side];
    // The sole opponent economy input is the estimate updated from public plays.
    // Never access state_.hands[1-side], queues, decks or aether[1-side].
    std::vector<Entity> threats;
    std::array<double, 2> laneScores{};
    double threatTotal = 0, pressure = 0;
    bool imminent = false;
    Entity primary;
    double bestThreat = -1;
    for (const auto &e : state_.entities)
        if (!e.dead && e.kind == EntityKind::Troop) {
            const auto *c = FindCard(e.cardId);
            if (!c)
                continue;
            if (e.team != team && e.position.z * sign > .35) {
                threats.push_back(e);
                const double depth = Clamp((e.position.z * sign - 1.5) / 12, 0, 1.35);
                const double value = c->cost * (.65 + depth * 1.45) * (c->structuresOnly ? 1.45 : 1) *
                                     (.55 + .45 * e.hp / e.maxHp);
                laneScores[e.lane < 0 ? 0 : 1] += value;
                threatTotal += value;
                const auto *target = Get(e.target);
                imminent |= e.position.z * sign > 8 ||
                            (target && Crown(*target) && D(e.position, target->position) < 5.6);
                if (value > bestThreat) {
                    bestThreat = value;
                    primary = e;
                }
            } else if (e.team == team && -e.position.z * sign > 1.2)
                pressure +=
                    c->cost * (.65 + Clamp((-e.position.z * sign - 1.5) / 12, 0, 1.3)) * e.hp / e.maxHp;
        }
    const int threatLane = laneScores[0] > laneScores[1] ? -1 : 1;
    struct Choice {
        int index;
        const Card *card;
    };
    std::vector<Choice> available;
    for (int h = 0; h < 4; ++h)
        if (const auto *c = FindCard(state_.hands[side][h]); c && c->cost <= bank + 1e-9)
            available.push_back({h, c});
    auto observedCounter = [&](const Card &candidate) {
        std::set<std::string> seen;
        for (std::size_t n = ai.observedCycle.size(); n-- > 0;) {
            if (!seen.insert(ai.observedCycle[n]).second)
                continue;
            const auto *c = FindCard(ai.observedCycle[n]);
            if (c && ai.observedCycle.size() - 1 - n >= 4 && CounterScore(*c, candidate) >= 55)
                return true;
        }
        return false;
    };
    auto attackLane = [&]() {
        std::vector<const Entity *> guards;
        for (const auto &e : state_.entities)
            if (!e.dead && e.team != team && e.kind == EntityKind::Guard)
                guards.push_back(&e);
        if (guards.empty())
            return Random() < .5 ? -1 : 1;
        std::sort(guards.begin(), guards.end(),
                  [](auto a, auto b) { return a->hp / a->maxHp < b->hp / b->maxHp; });
        if (guards.size() == 1 || guards[0]->hp / guards[0]->maxHp + .13 < guards[1]->hp / guards[1]->maxHp ||
            Random() < .78)
            return guards[0]->lane;
        return guards[1]->lane;
    };
    auto pocket = [&](int lane, const Card &c, bool deep, Vec2 &point) {
        for (const auto &e : state_.entities)
            if (e.team != team && e.kind == EntityKind::Guard && e.lane == lane && e.dead) {
                point =
                    SnapToTile({lane * (2 + (arena::PocketOuterX - 2) * (deep ? .62 : .42)), -sign * (deep ? 8.25 : 3.25)});
                return CanPlace(team, c, point);
            }
        return false;
    };
    auto anchorAfterPlay = [&](int lane) {
        EntityId id = 0;
        for (const auto &e : state_.entities)
            if (!e.dead && e.team == team && e.kind == EntityKind::Troop && e.id > id)
                id = e.id;
        ai.anchor = id;
        ai.pushLane = lane;
        ai.supports = 0;
        ai.phase = "support";
    };
    struct SpellPick {
        int index = -1, count = 0;
        double value = -1e9;
        Vec2 point;
        const Card *card = nullptr;
    };
    auto bestSpell = [&](const std::vector<Entity> &targets) {
        SpellPick best;
        for (auto choice : available)
            if (choice.card->spell)
                for (const auto &center : targets) {
                    int count = 0;
                    double value = 0;
                    for (const auto &v : targets)
                        if (D(v.position, center.position) <= choice.card->spellRadius) {
                            ++count;
                            const auto *c = FindCard(v.cardId);
                            double potential =
                                choice.card->damage + choice.card->dotDamage * choice.card->dotDuration * .72;
                            value += (c ? c->cost : 3) * std::min(1., potential / std::max(1., v.hp));
                        }
                    for (const auto &t : state_.entities)
                        if (!t.dead && t.team == team && Crown(t) &&
                            D(t.position, center.position) < choice.card->spellRadius + 2) {
                            value += .7;
                            break;
                        }
                    value -= choice.card->cost * .18;
                    if (value > best.value)
                        best = {choice.index, count, value, center.position, choice.card};
                }
        return best;
    };
    if ((ai.style == "spell_cycle" || ai.style == "cycle") && state_.elapsed - ai.lastSpell >= 2.2) {
        const Entity *weak = nullptr;
        for (const auto &e : state_.entities)
            if (!e.dead && e.team != team && Crown(e) && (!weak || e.hp < weak->hp))
                weak = &e;
        if (weak) {
            const bool low = weak->hp / weak->maxHp < .34;
            if (low || (ai.style == "spell_cycle" && bank >= 8.8)) {
                Choice best{-1, nullptr};
                for (auto ch : available)
                    if (ch.card->spell && ch.card->towerDamage > 0 &&
                        (!best.card ||
                         ch.card->towerDamage / ch.card->cost > best.card->towerDamage / best.card->cost))
                        best = ch;
                if (best.card && AIPlay(team, best.index, weak->position, "spell_cycle")) {
                    ai.lastSpell = state_.elapsed;
                    Decision(team, "SPELL CYCLE", best.card->name);
                    return;
                }
            }
        }
    }
    if (style.punish > .65 && !threats.empty() && !imminent && bank >= 4 &&
        ai.estimatedOpponentAether <= 4.4 && state_.elapsed - ai.lastPunish >= 4.5) {
        for (const std::string &id :
             {"rambeast", "storm_raven", "twin_blades", "vampire_bats", "ironclad", "sky_manta"})
            for (auto ch : available)
                if (ch.card->id == id && !ch.card->spell && !ch.card->building &&
                    ch.card->cost <= bank - style.reserve * .4) {
                    const int lane = -threatLane;
                    Vec2 p{lane * (id == "rambeast" ? 6.9 : 6.6), sign * (id == "rambeast" ? 11.8 : 13.6)};
                    if (AIPlay(team, ch.index, p, "punish", style.reserve * .4)) {
                        ai.lastPunish = state_.elapsed;
                        anchorAfterPlay(lane);
                        Decision(team, "OPPOSITE-LANE PUNISH", ch.card->name);
                        return;
                    }
                }
    }
    const bool trade =
        !imminent && threatTotal <= 5.3 && pressure >= 3.2 && pressure - threatTotal > 1.1 - style.trade;
    if (!trade && !threats.empty() && (imminent || threatTotal >= 4.4 / std::max(.65, style.defense))) {
        auto spell = bestSpell(threats);
        if (spell.card &&
            ((spell.count >= 2 && spell.value >= 2.35) || spell.count >= 3 ||
             (spell.card->id == "bullet_burst" && spell.value >= 1.65)) &&
            AIPlay(team, spell.index, spell.point, "spell_defense")) {
            ai.phase = "defend";
            Decision(team, "SPELL DEFENSE", spell.card->name);
            return;
        }
        bool tank = false;
        for (const auto &e : threats)
            if (e.lane == threatLane) {
                const auto *c = FindCard(e.cardId);
                tank |= (c && c->structuresOnly) || e.hp > 850;
            }
        for (auto ch : available)
            if (ch.card->building && (tank || threatTotal >= 6)) {
                Vec2 p{threatLane * 7., sign * Clamp(primary.position.z * sign + 3.4, 4, 12.8)};
                if (CanPlace(team, *ch.card, SnapToTile(p)) &&
                    AIPlay(team, ch.index, p, "building_defense")) {
                    ai.phase = "defend";
                    Decision(team, "BUILDING DEFENSE", tank ? "tank pull" : "heavy pressure");
                    return;
                }
            }
        Choice best{-1, nullptr};
        double bestScore = -1e9;
        for (auto ch : available)
            if (!ch.card->spell) {
                const auto &c = *ch.card;
                double score = std::max(0, 7 - c.cost) * .52 - c.cost * .18;
                bool compatible = false;
                int air = 0;
                for (const auto &t : threats) {
                    const auto *tc = FindCard(t.cardId);
                    if (!tc)
                        continue;
                    double answer = CounterScore(c, *tc);
                    compatible |= answer > 0;
                    score += answer / 100 * 2.4;
                    air += t.flying;
                    if (tc->structuresOnly &&
                        (c.id == "frost_fang" || c.id == "vampire_bats" || c.id == "archer_tower"))
                        score += 1.8;
                }
                if (!compatible)
                    continue;
                if (c.building) {
                    const auto *pc = FindCard(primary.cardId);
                    score += pc && pc->structuresOnly ? 3.4 : 1.1;
                }
                if (c.splash > 0)
                    score += std::max(0, static_cast<int>(threats.size()) - 1) * 1.2;
                if (air && c.canHitAir)
                    score += air * 1.15;
                if (air && !c.canHitAir)
                    score -= 4;
                if (threats.size() > static_cast<std::size_t>(air) && c.id == "frost_fang")
                    score += 1.2;
                if (score > bestScore) {
                    bestScore = score;
                    best = ch;
                }
            }
        if (best.card) {
            Vec2 p{primary.lane * 7 + (Random() - .5) * 1.15,
                   sign * Clamp(primary.position.z * sign + 2.35, 3.35, 13.6)};
            if (AIPlay(team, best.index, p, "defend")) {
                ai.phase = "defend";
                Decision(team, "DEFEND", best.card->name);
                return;
            }
        }
    }
    if (trade && !threats.empty())
        Decision(team, "DAMAGE TRADE", "Visible counterpressure outweighs approach threat");
    auto support = [&]() {
        const Entity *anchor = Get(ai.anchor);
        if (!anchor || anchor->dead) {
            ai.anchor = 0;
            ai.supports = 0;
            ai.phase = "bank";
            return false;
        }
        const Entity copy = *anchor;
        const auto *ac = FindCard(copy.cardId);
        if (!ac)
            return false;
        const double advance = -copy.position.z * sign;
        if (ai.supports >= style.supports && advance > 1.2) {
            ai.phase = "bank";
            return false;
        }
        std::vector<Entity> defenders;
        for (const auto &e : state_.entities)
            if (!e.dead && e.kind == EntityKind::Troop && e.team != team && e.lane == ai.pushLane &&
                std::abs(e.position.z - copy.position.z) < 8)
                defenders.push_back(e);
        auto spell = bestSpell(defenders);
        if (spell.card && defenders.size() >= (spell.card->id == "bullet_burst" ? 1u : 2u) &&
            advance > -3.5) {
            Vec2 p{};
            for (const auto &e : defenders) {
                p.x += e.position.x;
                p.z += e.position.z;
            }
            p.x /= defenders.size();
            p.z /= defenders.size();
            if (AIPlay(team, spell.index, p, "support_spell")) {
                ++ai.supports;
                return true;
            }
        }
        Choice best{-1, nullptr};
        double score = -1e9;
        for (auto ch : available)
            if (!ch.card->spell && !ch.card->building && !ch.card->structuresOnly) {
                double value = PairSynergy(*ac, *ch.card) / 15 + Priority(style.support, ch.card->id) * .35;
                for (const auto &e : defenders)
                    if (const auto *c = FindCard(e.cardId))
                        value += CounterScore(*ch.card, *c) / 100 * 2;
                if (value > score) {
                    score = value;
                    best = ch;
                }
            }
        if (!best.card)
            return false;
        const double reserve = advance < -5.5 ? style.reserve : 0;
        Vec2 p{ai.pushLane * 6.8 + (Random() - .5), -sign * Clamp(advance - 2.5, -14.7, -3.5)};
        if (advance > 5.5)
            pocket(ai.pushLane, *best.card, true, p);
        if (AIPlay(team, best.index, p, "support", reserve)) {
            ++ai.supports;
            return true;
        }
        return false;
    };
    if (ai.phase == "defend" && threats.empty() &&
        bank >= (ai.style == "counter"   ? 4.5
                 : ai.style == "control" ? 5.5
                                         : 6.5)) {
        const Entity *survivor = nullptr;
        for (const auto &e : state_.entities)
            if (!e.dead && e.team == team && e.kind == EntityKind::Troop &&
                e.hp / e.maxHp > (ai.style == "counter" ? .26 : .38) && -e.position.z * sign > -11.5 &&
                -e.position.z * sign < 1.2) {
                const auto *c = FindCard(e.cardId);
                if (c && !c->structuresOnly && (!survivor || e.hp / e.maxHp > survivor->hp / survivor->maxHp))
                    survivor = &e;
            }
        if (survivor) {
            ai.anchor = survivor->id;
            ai.pushLane = survivor->lane;
            ai.supports = 0;
            ai.phase = "support";
            Decision(team, "COUNTER-PUSH", survivor->cardId);
            if (support())
                return;
        }
    }
    if (ai.phase == "support") {
        if (support()) {
            Decision(team, "SUPPORT PUSH", "Support deployed behind surviving anchor");
            return;
        }
        if (const auto *a = Get(ai.anchor); a && !a->dead && bank < std::max(5.6, style.desperate + .4))
            return;
    }
    if (ai.style == "split" && bank >= 7 && Random() < .42) {
        const int lane = -ai.pushLane;
        for (auto ch : available)
            if (ch.card->id == "rambeast" || ch.card->id == "vampire_bats" || ch.card->id == "twin_blades" ||
                ch.card->id == "sky_manta" || ch.card->id == "ironclad")
                if (AIPlay(team, ch.index, {lane * 6.6, sign * 12.6}, "split", 1)) {
                    Decision(team, "SPLIT-LANE PRESSURE", ch.card->name);
                    return;
                }
    }
    auto overflow = [&]() {
        Choice best{-1, nullptr};
        for (auto ch : available)
            if (!ch.card->spell && !ch.card->building &&
                (!best.card ||
                 Priority(style.overflow, ch.card->id) > Priority(style.overflow, best.card->id) ||
                 (Priority(style.overflow, ch.card->id) == Priority(style.overflow, best.card->id) &&
                  ch.card->cost < best.card->cost)))
                best = ch;
        if (!best.card)
            return false;
        const int lane = attackLane();
        Vec2 p{lane * 6.7 + (Random() - .5) * .7, sign * 14.5};
        if (ai.style == "aggro")
            pocket(lane, *best.card, false, p);
        return AIPlay(team, best.index, p, "overflow");
    };
    if (state_.crowns[side] > state_.crowns[1 - side] && state_.phase == Phase::Regulation &&
        state_.timeRemaining < 42) {
        if (bank >= 9.92 && overflow())
            Decision(team, "SAFE CYCLE", "Leading late, preventing overflow");
        else
            Decision(team, "HOLD LEAD", "Reserve for visible defense");
        return;
    }
    auto push = [&]() {
        const int lane = attackLane();
        Choice best{-1, nullptr};
        double score = -1e9;
        for (auto ch : available)
            if (!ch.card->spell && !ch.card->building) {
                double value = Priority(style.push, ch.card->id) + (ch.card->structuresOnly ? 3 : 0) -
                               (observedCounter(*ch.card) ? 5 : 0);
                if (value > score) {
                    score = value;
                    best = ch;
                }
            }
        if (!best.card)
            return false;
        Vec2 p{lane * (best.card->id == "rambeast" ? 6.9 : 6.5) + (Random() - .5) * .55,
               sign * (best.card->id == "rambeast"      ? 13.1
                       : best.card->id == "boulderback" ? 14.65
                                                        : 14.)};
        const double chance = ai.style == "aggro" ? .72 : ai.style == "control" ? .46 : .36;
        if (best.card->id != "boulderback" && Random() < chance)
            pocket(lane, *best.card, best.card->id != "rambeast", p);
        if (!AIPlay(team, best.index, p, "push"))
            return false;
        anchorAfterPlay(lane);
        return true;
    };
    if (bank >= style.bank) {
        if (push()) {
            Decision(team, "START PUSH", "Banked coherent pressure");
            return;
        }
        if (bank >= 9.85 && overflow()) {
            Decision(team, "OVERFLOW CYCLE", "Aether cap");
            return;
        }
    }
    if (((state_.crowns[side] < state_.crowns[1 - side] && state_.timeRemaining < 38) ||
         state_.phase == Phase::Overtime) &&
        bank >= style.desperate && push()) {
        Decision(team, "DESPERATION PUSH", "Clock pressure");
        return;
    }
    ai.phase = "bank";
    Decision(team, "BANK", "Saving Aether; opponent estimated from observed plays");
}
} // namespace rift
