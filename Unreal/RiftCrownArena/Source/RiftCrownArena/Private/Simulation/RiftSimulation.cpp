#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>

namespace rift {
namespace {
constexpr double FixedDT = 1. / 60., Epsilon = 1e-9;
int Index(Team t) {
    return t == Team::Player ? 0 : 1;
}
Team Other(Team t) {
    return t == Team::Player ? Team::Enemy : Team::Player;
}
double Sign(Team t) {
    return t == Team::Player ? 1 : -1;
}
double Clamp(double n, double lo, double hi) {
    return std::max(lo, std::min(hi, n));
}
double Distance(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
Vec2 Difference(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.z - b.z};
}
bool Structure(const Entity &e) {
    return e.kind != EntityKind::Troop;
}
bool Crown(const Entity &e) {
    return e.kind == EntityKind::Core || e.kind == EntityKind::Guard;
}
bool Running(Phase p) {
    return p == Phase::Regulation || p == Phase::Overtime;
}
bool StyleKnown(const std::string &s) {
    return s == "beatdown" || s == "aggro" || s == "control" || s == "cycle" || s == "split" ||
           s == "spell_cycle" || s == "counter";
}
Card Troop(const char *id, const char *name, int cost, double hp, double damage, double interval,
           double speed, double range, double scale) {
    Card c;
    c.id = id;
    c.name = name;
    c.cost = cost;
    c.hp = hp;
    c.damage = damage;
    c.attackInterval = interval;
    c.moveSpeed = speed;
    c.range = range;
    c.scale = scale;
    return c;
}
Card Spell(const char *id, const char *name, int cost, double damage, double tower, double radius) {
    Card c;
    c.id = id;
    c.name = name;
    c.cost = cost;
    c.damage = damage;
    c.towerDamage = tower;
    c.spellRadius = radius;
    c.spell = true;
    return c;
}
} // namespace
const std::vector<Card> &Cards() {
    static const std::vector<Card> cards = []() {
        std::vector<Card> v;
        auto c = Troop("ironclad", "Ironclad", 3, 840, 96, 1, 2.25, 1.35, 1);
        v.push_back(c);
        c = Troop("ember_archer", "Ember Archer", 3, 423, 152, 1.55, 2.35, 6, .95);
        c.projectileSpeed = 16;
        c.canHitAir = true;
        v.push_back(c);
        c = Troop("twin_blades", "Twin Blades", 2, 262, 58, .72, 3.35, 1.2, .82);
        c.count = 2;
        v.push_back(c);
        c = Troop("boulderback", "Boulderback", 5, 1620, 118, 2, 1.3, 1.5, 1.22);
        c.structuresOnly = true;
        v.push_back(c);
        c = Troop("arc_mage", "Arc Mage", 4, 547, 120, 1, 2, 6.5, .96);
        c.projectileSpeed = 17;
        c.canHitAir = true;
        c.splash = 2.5;
        v.push_back(c);
        c = Troop("rambeast", "Rambeast", 4, 880, 140, 1.4, 2.6, 1.45, 1.06);
        c.structuresOnly = true;
        c.chargeDamage = 255;
        v.push_back(c);
        c = Troop("sky_manta", "Sky Manta", 3, 480, 77, .92, 3.05, 3.4, 1.05);
        c.projectileSpeed = 15;
        c.flying = true;
        c.canHitAir = true;
        v.push_back(c);
        c = Troop("vampire_bats", "Vampire Bats", 5, 174, 86, 1.05, 1.95, 2, .68);
        c.count = 5;
        c.flying = true;
        c.canHitAir = true;
        v.push_back(c);
        c = Troop("frost_fang", "Frost Fang", 5, 1155, 72, .8, 2.5, 1.1, 1.02);
        c.slowPct = .3;
        c.slowDuration = 2;
        v.push_back(c);
        c = Troop("storm_raven", "Storm Raven", 6, 1337, 251, 1.7, 1.18, 4.3, 1.2);
        c.flying = true;
        c.structuresOnly = true;
        c.projectileSpeed = 18;
        c.auraDamage = 82;
        c.auraInterval = 3;
        c.auraRadius = 2;
        c.stunDuration = .4;
        v.push_back(c);
        c = Spell("meteor_shards", "Meteor Shards", 5, 262, 0, 4.5);
        c.dotDamage = 40;
        c.dotDuration = 5;
        c.dotInterval = 1;
        v.push_back(c);
        c = Troop("archer_tower", "Archer Tower", 4, 850, 75, 1.1, 0, 7, 1);
        c.building = true;
        c.projectileSpeed = 18;
        c.canHitAir = true;
        c.lifetime = 25;
        c.footprint = 1.65;
        v.push_back(c);
        c = Spell("bullet_burst", "Bullet Burst", 2, 175, 55, 2.2);
        c.rounds = 7;
        v.push_back(c);
        c = Spell("nova_flask", "Nova Flask", 4, 375, 185, 3.25);
        v.push_back(c);
        return v;
    }();
    return cards;
}
const Card *FindCard(const std::string &id) {
    for (const auto &c : Cards())
        if (c.id == id)
            return &c;
    return nullptr;
}
const std::vector<std::string> &DefaultDeck() {
    static const std::vector<std::string> d{"ironclad", "ember_archer", "archer_tower", "boulderback",
                                            "arc_mage", "rambeast",     "sky_manta",    "nova_flask"};
    return d;
}
std::string TeamName(Team t) {
    return t == Team::Player ? "player" : "enemy";
}
std::string PhaseName(Phase p) {
    switch (p) {
    case Phase::Regulation:
        return "regulation";
    case Phase::Overtime:
        return "overtime";
    case Phase::Tiebreaker:
        return "tiebreaker";
    default:
        return "finished";
    }
}
Vec2 SnapToTile(Vec2 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.z))
        return {0, 0};
    return {Clamp(std::floor(p.x + 14) - 13.5, -13.5, 13.5), Clamp(std::floor(p.z + 21) - 20.5, -20.5, 20.5)};
}
bool ValidateDeck(const std::vector<std::string> &d) {
    if (d.size() != 8)
        return false;
    std::set<std::string> seen;
    for (const auto &id : d)
        if (!FindCard(id) || !seen.insert(id).second)
            return false;
    return true;
}
Match::Match(const MatchOptions &o) {
    state_.seed = state_.randomState = o.seed;
    state_.entities.reserve(128);
    for (int t = 0; t < 2; ++t) {
        Team team = t == 0 ? Team::Player : Team::Enemy;
        state_.ai[t].enabled = o.aiEnabled[t];
        state_.ai[t].style = StyleKnown(o.aiStyles[t]) ? o.aiStyles[t] : "control";
        const auto deck = ValidateDeck(o.decks[t])
                              ? o.decks[t]
                              : (o.aiEnabled[t] ? BuildAIDeck(state_.ai[t].style,
                                                              o.seed + static_cast<std::uint32_t>(t * 7919))
                                                : DefaultDeck());
        state_.decks[t] = deck;
        for (int h = 0; h < 4; ++h)
            state_.hands[t][h] = deck[h];
        state_.queues[t] = std::vector<std::string>(deck.begin() + 4, deck.end());
        for (const auto &c : Cards())
            state_.telemetry[t][c.id] = {};
        for (int h = 0; h < 4; ++h)
            state_.telemetry[t][deck[h]].openingEligible = 1;
        for (int lane : {0, -1, 1}) {
            Entity e;
            e.id = nextEntity_++;
            e.team = team;
            e.kind = lane == 0 ? EntityKind::Core : EntityKind::Guard;
            e.lane = lane;
            e.position = {lane == 0 ? 0 : lane * 8.2, Sign(team) * (lane == 0 ? 16.3 : 12.4)};
            e.facing = {0, -Sign(team)};
            e.maxHp = e.hp = lane == 0 ? 3600 : 2250;
            e.radius = lane == 0 ? 1.35 : 1.15;
            e.active = lane != 0;
            state_.entities.push_back(e);
        }
    }
    Emit("match_start").reason = "authoritative-native-v1";
}
double Match::Random() {
    state_.randomState += 0x6d2b79f5u;
    std::uint32_t t = state_.randomState;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.;
}
Entity *Match::Get(EntityId id) {
    for (auto &e : state_.entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
const Entity *Match::Get(EntityId id) const {
    for (const auto &e : state_.entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
Event &Match::Emit(const std::string &type, Team team) {
    events_.push_back({});
    Event &e = events_.back();
    e.sequence = nextEvent_++;
    e.time = state_.elapsed;
    e.type = type;
    e.team = team;
    return e;
}
std::vector<Event> Match::DrainEvents() {
    std::vector<Event> v;
    v.swap(events_);
    return v;
}
double Match::AetherGenerated(double from, double to) {
    if (!std::isfinite(from) || !std::isfinite(to) || to <= from)
        return 0;
    double amount = 0;
    for (const auto &segment :
         std::array<std::array<double, 3>, 3>{{{0, 120, 1}, {120, 240, 2}, {240, 300, 3}}})
        amount += std::max(0., std::min(to, segment[1]) - std::max(from, segment[0])) * segment[2] / 2.8;
    return amount;
}
void Match::Step(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || state_.phase == Phase::Finished)
        return;
    accumulator_ += seconds;
    while (accumulator_ + Epsilon >= FixedDT && state_.phase != Phase::Finished) {
        accumulator_ -= FixedDT;
        if (accumulator_ < 0 && accumulator_ > -Epsilon)
            accumulator_ = 0;
        FixedStep(FixedDT);
    }
}
void Match::FixedStep(double dt) {
    const double old = state_.elapsed;
    state_.elapsed += dt;
    state_.phaseElapsed += dt;
    if (state_.phase == Phase::Tiebreaker) {
        UpdateTiebreaker(dt);
        return;
    }
    state_.timeRemaining =
        std::max(0., (state_.phase == Phase::Regulation ? 180. : 120.) - state_.phaseElapsed);
    if (state_.timeRemaining < Epsilon) {
        if (state_.phase == Phase::Regulation) {
            if (state_.crowns[0] || state_.crowns[1]) {
                Finish(state_.crowns[0] == state_.crowns[1]  ? -1
                       : state_.crowns[0] > state_.crowns[1] ? 0
                                                             : 1,
                       "regulation");
                return;
            }
            state_.phase = Phase::Overtime;
            state_.phaseElapsed = 0;
            state_.timeRemaining = 120;
            Emit("phase").reason = "overtime";
        } else {
            BeginTiebreaker();
            return;
        }
    }
    const double generated = AetherGenerated(old, state_.elapsed);
    for (int i = 0; i < 2; ++i) {
        const double added = std::min(generated, 10 - state_.aether[i]);
        state_.aether[i] += added;
        const double leak = generated - added;
        state_.leaked[i] += leak;
        if (leak > Epsilon) {
            auto &e = Emit("aether_leak", i == 0 ? Team::Player : Team::Enemy);
            e.amount = leak;
            e.aetherAfter = state_.aether[i];
        }
        state_.ai[i].estimatedOpponentAether =
            std::min(10., state_.ai[i].estimatedOpponentAether + generated);
    }
    // Settle the previous interval before current attacks refresh sources. This
    // prevents entity update order from crediting a fresh caster with old uptime.
    for (const auto &victim : state_.entities) {
        if (victim.dead)
            continue;
        for (bool slow : {true, false}) {
            const PlayId source = slow ? victim.slowSource : victim.stunSource;
            auto credit = statusCredits_.find(source);
            if (!source || credit == statusCredits_.end())
                continue;
            auto &telemetry = state_.telemetry[Index(credit->second.team)][credit->second.card];
            const double until = slow ? victim.slowUntil : victim.stunUntil;
            const double from = slow ? victim.slowTrackedFrom : victim.stunTrackedFrom;
            const double exposure = std::max(0., state_.elapsed - std::max(old, from));
            const double affected = std::max(0., std::min(state_.elapsed, until) - std::max(old, from));
            if (slow) {
                telemetry.slowTrackedSeconds += exposure;
                telemetry.slowTime += affected;
            } else {
                telemetry.stunTrackedSeconds += exposure;
                telemetry.stunTime += affected;
            }
        }
    }
    // Alternate team evaluation from match seed, never from wall-frame order.
    const int first = static_cast<int>(state_.seed & 1u);
    UpdateAI(first == 0 ? Team::Player : Team::Enemy, dt);
    if (!Running(state_.phase))
        return;
    UpdateAI(first == 0 ? Team::Enemy : Team::Player, dt);
    if (!Running(state_.phase))
        return;
    std::vector<EntityId> order;
    for (EntityKind k : {EntityKind::Core, EntityKind::Guard, EntityKind::Building, EntityKind::Troop})
        for (const auto &e : state_.entities)
            if (e.kind == k && !e.dead)
                order.push_back(e.id);
    for (EntityId id : order) {
        if (!Running(state_.phase))
            return;
        UpdateEntity(id, dt);
    }
    if (!Running(state_.phase))
        return;
    for (std::size_t i = 0; i < state_.projectiles.size();) {
        auto &p = state_.projectiles[i];
        const auto *target = Get(p.target);
        p.remaining -= dt;
        if (!target || target->dead) {
            state_.projectiles.erase(state_.projectiles.begin() + i);
            continue;
        }
        if (p.remaining > Epsilon) {
            ++i;
            continue;
        }
        const auto projectile = p;
        state_.projectiles.erase(state_.projectiles.begin() + i);
        const Vec2 impact = target->position;
        Damage(projectile.target, projectile.damage, projectile.team, projectile.cardId, projectile.playId,
               projectile.source);
        if (!Running(state_.phase))
            return;
        if (projectile.splash > 0) {
            std::vector<EntityId> victims;
            for (const auto &e : state_.entities)
                if (!e.dead && e.team != projectile.team && e.id != projectile.target) {
                    const double extra = e.kind == EntityKind::Building ? e.radius * .35
                                         : Crown(e)                     ? e.radius * .25
                                                                        : 0;
                    if (Distance(e.position, impact) <= projectile.splash + extra)
                        victims.push_back(e.id);
                }
            for (auto id : victims) {
                Damage(id, projectile.damage, projectile.team, projectile.cardId, projectile.playId,
                       projectile.source, "splash");
                if (!Running(state_.phase))
                    return;
            }
        }
    }
    for (std::size_t i = 0; i < state_.hazards.size();) {
        auto &h = state_.hazards[i];
        const Card *c = FindCard(h.cardId);
        if (!c) {
            state_.hazards.erase(state_.hazards.begin() + i);
            continue;
        }
        int occupants = 0;
        for (const auto &e : state_.entities)
            if (!e.dead && e.kind == EntityKind::Troop && e.team != h.team &&
                Distance(e.position, h.position) <= h.radius + e.radius * .2)
                ++occupants;
        auto &tele = state_.telemetry[Index(h.team)][h.cardId];
        // AI may cast at this tick's endpoint; exposure begins at the cast,
        // rather than crediting occupants for time before the hazard existed.
        const double activeDt = std::max(0., std::min(state_.elapsed, h.expires) - std::max(old, h.born));
        tele.zoneOccupancy += occupants * activeDt;
        tele.zoneSeconds += activeDt;
        while (h.nextTick <= state_.elapsed + Epsilon && h.nextTick <= h.expires + Epsilon && h.ticks < 5) {
            std::vector<EntityId> victims;
            for (const auto &e : state_.entities)
                if (!e.dead && e.kind == EntityKind::Troop && e.team != h.team &&
                    Distance(e.position, h.position) <= h.radius + e.radius * .2)
                    victims.push_back(e.id);
            const auto copy = h;
            ++h.ticks;
            h.nextTick += c->dotInterval;
            ++tele.dotTicks;
            Emit("hazard_tick", h.team).playId = h.playId;
            for (auto id : victims) {
                Damage(id, c->dotDamage, copy.team, copy.cardId, copy.playId, 0, "dot");
                if (!Running(state_.phase))
                    return;
            }
        }
        if (state_.elapsed + Epsilon >= h.expires)
            state_.hazards.erase(state_.hazards.begin() + i);
        else
            ++i;
    }
    state_.entities.erase(std::remove_if(state_.entities.begin(), state_.entities.end(),
                                         [&](const Entity &e) {
                                             return e.dead && !Crown(e) &&
                                                    state_.elapsed - e.died >=
                                                        (e.kind == EntityKind::Building ? 1 : .42);
                                         }),
                          state_.entities.end());
}
void Match::Finish(int winner, const std::string &reason) {
    if (state_.phase == Phase::Finished)
        return;
    state_.phase = Phase::Finished;
    state_.winner = winner;
    state_.resultReason = reason;
    state_.timeRemaining = 0;
    state_.projectiles.clear();
    for (auto &team : state_.telemetry)
        for (auto &[id, t] : team)
            t.surviving = 0;
    for (const auto &e : state_.entities)
        if (!e.dead && !Crown(e)) {
            auto &t = state_.telemetry[Index(e.team)][e.cardId];
            ++t.surviving;
            t.lifetime += state_.elapsed - e.born;
        }
    auto &e = Emit("match_end", winner == 1 ? Team::Enemy : Team::Player);
    e.reason = reason;
    e.amount = winner;
    e.crowns = winner < 0 ? 0 : state_.crowns[winner];
}
void Match::BeginTiebreaker() {
    state_.phase = Phase::Tiebreaker;
    state_.phaseElapsed = 0;
    state_.timeRemaining = 0;
    state_.projectiles.clear();
    state_.hazards.clear();
    tieMin_.fill(std::numeric_limits<double>::max());
    tieTotal_.fill(0);
    for (const auto &e : state_.entities)
        if (Crown(e) && !e.dead) {
            tieMin_[Index(e.team)] = std::min(tieMin_[Index(e.team)], e.hp);
            tieTotal_[Index(e.team)] += e.hp;
        }
    Emit("phase").reason = "tiebreaker";
}
void Match::UpdateTiebreaker(double dt) {
    if (state_.phaseElapsed < .85 - Epsilon)
        return;
    const double activeDt = std::min(dt, std::max(0., state_.phaseElapsed - .85));
    std::array<bool, 2> down{};
    for (auto &e : state_.entities)
        if (Crown(e) && !e.dead) {
            e.hp = std::max(0., e.hp - 180 * activeDt);
            down[Index(e.team)] |= e.hp <= Epsilon;
        }
    if (!down[0] && !down[1])
        return;
    int winner = down[0] && !down[1]            ? 1
                 : down[1] && !down[0]          ? 0
                 : tieMin_[0] != tieMin_[1]     ? tieMin_[0] > tieMin_[1] ? 0 : 1
                 : tieTotal_[0] != tieTotal_[1] ? tieTotal_[0] > tieTotal_[1] ? 0 : 1
                 : Random() < .5                ? 0
                                                : 1;
    state_.crowns[winner] = 1;
    state_.crowns[1 - winner] = 0;
    bool first = true;
    for (auto &e : state_.entities)
        if (Crown(e) && Index(e.team) != winner && !e.dead && e.hp <= Epsilon) {
            e.dead = true;
            e.died = state_.elapsed;
            auto &event = Emit("tower_destroy", winner == 0 ? Team::Player : Team::Enemy);
            event.target = e.id;
            event.targetTeam = e.team;
            event.targetKind = e.kind;
            event.crowns = first ? 1 : 0;
            event.reason = "tiebreaker";
            first = false;
        }
    Finish(winner, "tiebreaker");
}
bool Match::CanPlace(Team team, const Card &c, Vec2 p, bool sandbox) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.z) || std::abs(p.x) > 14 || std::abs(p.z) > 21)
        return false;
    if (sandbox || c.spell)
        return true;
    auto legal = [&](Vec2 q) {
        if (std::abs(q.x) > 14 || std::abs(q.z) > 21)
            return false;
        if (q.z * Sign(team) >= 2.15)
            return true;
        const double depth = -q.z * Sign(team);
        if (depth < 2.25 || depth > 9.25 || std::abs(q.x) < 2 || std::abs(q.x) > 13.2)
            return false;
        const int lane = q.x < 0 ? -1 : 1;
        for (const auto &e : state_.entities)
            if (e.team != team && e.kind == EntityKind::Guard && e.lane == lane)
                return e.dead;
        return false;
    };
    if (!legal(p))
        return false;
    if (c.building) {
        const double half = c.footprint / 2;
        for (double x : {-half, half})
            for (double z : {-half, half})
                if (!legal({p.x + x, p.z + z}))
                    return false;
        for (const auto &e : state_.entities)
            if (!e.dead && Structure(e) &&
                Distance(p, e.position) < e.radius + c.footprint * .55 + (Crown(e) ? .45 : .35))
                return false;
    }
    return true;
}
bool Match::Play(Team team, int index, Vec2 p, const std::string &reason) {
    if (!Running(state_.phase) || index < 0 || index >= 4 || !std::isfinite(p.x) || !std::isfinite(p.z) ||
        std::abs(p.x) > 14 || std::abs(p.z) > 21)
        return false;
    const int t = Index(team);
    const Card *c = FindCard(state_.hands[t][index]);
    p = SnapToTile(p);
    if (!c || state_.aether[t] + Epsilon < c->cost || !CanPlace(team, *c, p))
        return false;
    const PlayId play = nextPlay_++;
    const double before = state_.aether[t];
    // Affordability permits floating-point residue at the exact cost boundary.
    // Never persist a negative bank after paying an affordable canonical cost.
    state_.aether[t] = std::max(0.0, state_.aether[t] - c->cost);
    state_.spent[t] += c->cost;
    auto &tele = state_.telemetry[t][c->id];
    tele.spent += c->cost;
    ++tele.plays;
    tele.placementX += p.x;
    tele.placementZ += p.z;
    const bool first = paidPlays_[t]++ == 0;
    const bool opening =
        paidPlays_[t] <= 4 &&
        std::find(state_.decks[t].begin(), state_.decks[t].begin() + 4, c->id) !=
            state_.decks[t].begin() + 4 &&
        std::find(openingPlayed_[t].begin(), openingPlayed_[t].end(), c->id) == openingPlayed_[t].end();
    if (first)
        ++tele.firstPlays;
    if (opening) {
        ++tele.openingPlays;
        openingPlayed_[t].push_back(c->id);
    }
    if (state_.phase == Phase::Overtime)
        ++tele.overtimePlays;
    state_.hands[t][index] = state_.queues[t].front();
    state_.queues[t].erase(state_.queues[t].begin());
    state_.queues[t].push_back(c->id);
    auto &e = Emit("card_play", team);
    e.cardId = c->id;
    e.playId = play;
    e.position = p;
    e.handIndex = index;
    e.reason = reason;
    e.amount = c->cost;
    e.aetherBefore = before;
    e.aetherAfter = state_.aether[t];
    e.handAfter = state_.hands[t];
    e.firstPlay = first;
    e.openingHand = opening;
    e.overtime = state_.phase == Phase::Overtime;
    auto &observer = state_.ai[1 - t];
    observer.estimatedOpponentAether = std::max(0., observer.estimatedOpponentAether - c->cost);
    observer.observedCycle.push_back(c->id);
    if (observer.observedCycle.size() > 12)
        observer.observedCycle.erase(observer.observedCycle.begin());
    Deploy(team, *c, p, play, false);
    return true;
}
bool Match::Spawn(Team team, const std::string &id, Vec2 p) {
    const Card *c = FindCard(id);
    if (!c || !Running(state_.phase) || !CanPlace(team, *c, p, true))
        return false;
    p = SnapToTile(p);
    const PlayId play = nextPlay_++;
    auto &e = Emit("card_play", team);
    e.cardId = id;
    e.playId = play;
    e.position = p;
    e.sandbox = true;
    e.reason = "developer_spawn";
    e.aetherBefore = e.aetherAfter = state_.aether[Index(team)];
    Deploy(team, *c, p, play, true);
    return true;
}
void Match::Deploy(Team team, const Card &c, Vec2 p, PlayId play, bool sandbox) {
    if (c.spell) {
        ApplySpell(team, c, p, play);
        return;
    }
    const std::vector<Vec2> offsets =
        c.count == 2   ? std::vector<Vec2>{{-.42, .12}, {.42, -.12}}
        : c.count == 5 ? std::vector<Vec2>{{-.86, -.18}, {0, -.42}, {.86, -.18}, {-.43, .38}, {.43, .38}}
                       : std::vector<Vec2>{{0, 0}};
    std::vector<EntityId> deployed;
    for (int i = 0; i < c.count; ++i) {
        Entity e;
        e.id = nextEntity_++;
        e.team = team;
        e.kind = c.building ? EntityKind::Building : EntityKind::Troop;
        e.cardId = c.id;
        e.playId = play;
        e.position = {Clamp(p.x + offsets[i].x, -12.2, 12.2), p.z + offsets[i].z};
        e.facing = {0, -Sign(team)};
        e.hp = e.maxHp = c.hp;
        e.radius = c.building ? c.footprint * .52 : .44 * c.scale;
        e.flying = c.flying;
        e.born = state_.elapsed;
        e.memberCount = c.count;
        e.cooldown = c.building ? .35 : 0;
        e.lane = e.position.x < 0 ? -1 : 1;
        e.stuckPosition = e.position;
        state_.entities.push_back(e);
        deployed.push_back(e.id);
        auto &t = state_.telemetry[Index(team)][c.id];
        ++t.spawns;
        if (c.building)
            t.buildingCapacity += c.lifetime;
        auto &event = Emit("entity_spawn", team);
        event.cardId = c.id;
        event.source = e.id;
        event.playId = play;
        event.position = e.position;
        event.hp = event.maxHp = c.hp;
        event.targetKind = e.kind;
        event.count = c.count;
        event.sandbox = sandbox;
    }
    for (auto id : deployed)
        PullDeployment(id);
}
void Match::ApplySpell(Team team, const Card &c, Vec2 p, PlayId play) {
    std::vector<EntityId> ids;
    for (const auto &e : state_.entities)
        if (!e.dead && e.team != team) {
            if (c.dotDamage > 0 && Structure(e))
                continue;
            const double padding = c.id == "bullet_burst" ? (Structure(e) ? e.radius * .25 : e.radius * .2)
                                   : c.dotDamage > 0      ? e.radius * .2
                                   : Structure(e)         ? e.radius * .4
                                                          : 0;
            if (Distance(e.position, p) <= c.spellRadius + padding)
                ids.push_back(e.id);
        }
    for (auto id : ids) {
        const auto *e = Get(id);
        Damage(id, e && Structure(*e) ? c.towerDamage : c.damage, team, c.id, play, 0, "initial");
        if (!Running(state_.phase))
            return;
    }
    if (c.dotDamage > 0) {
        Hazard h;
        h.playId = play;
        h.team = team;
        h.cardId = c.id;
        h.position = p;
        h.radius = c.spellRadius;
        h.born = state_.elapsed;
        h.nextTick = state_.elapsed + c.dotInterval;
        h.expires = state_.elapsed + c.dotDuration;
        state_.hazards.push_back(h);
    }
}
void Match::Damage(EntityId id, double amount, Team team, const std::string &card, PlayId play,
                   EntityId source, const std::string &kind) {
    Entity *t = Get(id);
    if (!t || t->dead || !Running(state_.phase) || t->team == team || !std::isfinite(amount) || amount <= 0)
        return;
    const double actual = std::min(t->hp, amount), overkill = std::max(0., amount - t->hp);
    t->hp -= actual;
    const Card *victim = FindCard(t->cardId);
    auto &event = Emit("damage", team);
    event.source = source;
    event.target = id;
    event.targetTeam = t->team;
    event.targetKind = t->kind;
    event.cardId = card;
    event.playId = play;
    event.amount = actual;
    event.requested = amount;
    event.overkill = overkill;
    event.hp = t->hp;
    event.maxHp = t->maxHp;
    event.position = t->position;
    event.count = t->memberCount;
    event.targetCost = victim ? static_cast<double>(victim->cost) / victim->count : 0;
    event.damageKind = kind;
    if (!card.empty()) {
        auto &tele = state_.telemetry[Index(team)][card];
        if (Crown(*t))
            tele.towerDamage += actual;
        else if (t->kind == EntityKind::Building)
            tele.buildingDamage += actual;
        else
            tele.troopDamage += actual;
        tele.overkill += overkill;
        if (kind == "initial")
            tele.initialDamage += actual;
        if (kind == "dot")
            tele.dotDamage += actual;
        if (kind == "aura")
            tele.auraDamage += actual;
        if (Crown(*t)) {
            towerCredits_[id][{team, card}] += actual;
            if (play && !connected_[play]) {
                connected_[play] = true;
                ++tele.connected;
            }
        }
        if (play) {
            auto &targets = castTargets_[play];
            if (std::find(targets.begin(), targets.end(), id) == targets.end()) {
                targets.push_back(id);
                ++tele.targets;
            }
            if (victim)
                tele.spellValue += actual / t->maxHp * victim->cost / victim->count;
        }
    }
    if (victim) {
        auto &v = state_.telemetry[Index(t->team)][t->cardId];
        v.damageTaken += actual;
        if (t->kind == EntityKind::Building)
            v.prevented += actual;
    }
    if (t->hp <= Epsilon)
        Kill(id, team, card, play, source);
}
void Match::Kill(EntityId id, Team team, const std::string &card, PlayId play, EntityId source) {
    Entity *t = Get(id);
    if (!t || t->dead)
        return;
    t->dead = true;
    t->hp = 0;
    t->died = state_.elapsed;
    auto &ev = Emit("death", team);
    ev.target = id;
    ev.source = source;
    ev.cardId = card;
    ev.playId = play;
    ev.targetTeam = t->team;
    ev.targetKind = t->kind;
    ev.position = t->position;
    ev.count = t->memberCount;
    const Card *v = FindCard(t->cardId);
    if (v) {
        auto &victim = state_.telemetry[Index(t->team)][t->cardId];
        ++victim.deaths;
        victim.lifetime += state_.elapsed - t->born;
        ev.targetCost = static_cast<double>(v->cost) / v->count;
        if (!card.empty()) {
            auto &killer = state_.telemetry[Index(team)][card];
            ++killer.kills;
            killer.killValue += ev.targetCost;
        }
    }
    if (!Crown(*t))
        return;
    const int award = t->kind == EntityKind::Core ? 3 : 1;
    const auto kind = t->kind;
    const auto owner = t->team;
    const double maxHp = t->maxHp;
    state_.crowns[Index(team)] = kind == EntityKind::Core ? 3 : state_.crowns[Index(team)] + 1;
    auto &e = Emit("tower_destroy", team);
    e.target = id;
    e.targetTeam = owner;
    e.targetKind = kind;
    e.source = source;
    e.cardId = card;
    e.playId = play;
    e.crowns = award;
    double total = 0;
    for (const auto &[key, d] : towerCredits_[id])
        total += d;
    for (const auto &[key, d] : towerCredits_[id])
        state_.telemetry[Index(key.first)][key.second].crownContribution +=
            award * d / std::max(maxHp, total);
    if (kind == EntityKind::Guard)
        for (auto &core : state_.entities)
            if (core.team == owner && core.kind == EntityKind::Core && !core.dead) {
                core.active = true;
                Emit("core_activate", owner).target = core.id;
            }
    if (kind == EntityKind::Core)
        Finish(Index(team), "core_destroyed");
    else if (state_.phase == Phase::Overtime)
        Finish(Index(team), "overtime_tower");
}
void Match::SetAether(Team team, double value) {
    if (!std::isfinite(value) || state_.phase == Phase::Finished)
        return;
    const int i = Index(team);
    const double previous = state_.aether[i];
    state_.aether[i] = Clamp(value, 0, 10);
    auto &e = Emit("aether_grant", team);
    e.amount = state_.aether[i] - previous;
    e.aetherBefore = previous;
    e.aetherAfter = state_.aether[i];
    e.sandbox = true;
}
bool Match::SetTowerHP(EntityId id, double hp) {
    auto *t = Get(id);
    if (!t || t->dead || !Crown(*t) || !std::isfinite(hp) || !Running(state_.phase))
        return false;
    t->hp = Clamp(hp, 0, t->maxHp);
    auto &e = Emit("tower_edit", t->team);
    e.target = id;
    e.hp = t->hp;
    e.sandbox = true;
    if (t->hp <= Epsilon)
        Kill(id, Other(t->team), "", 0, 0);
    return true;
}
void Match::ClearField() {
    if (!Running(state_.phase))
        return;
    state_.entities.erase(std::remove_if(state_.entities.begin(), state_.entities.end(),
                                         [](const Entity &e) { return !Crown(e); }),
                          state_.entities.end());
    state_.projectiles.clear();
    state_.hazards.clear();
    for (auto &ai : state_.ai) {
        ai.anchor = 0;
        ai.supports = 0;
        ai.decision = "BANK";
    }
    Emit("clear_field").sandbox = true;
}
void Match::SetAIEnabled(Team team, bool enabled) {
    auto &ai = state_.ai[Index(team)];
    ai.enabled = enabled;
    ai.think = 0;
    Decision(team, enabled ? "AI ENABLED" : "AI DISABLED", ai.style);
}
bool Match::SetAIStyle(Team team, const std::string &style) {
    if (!StyleKnown(style) || !Running(state_.phase))
        return false;
    const int t = Index(team);
    auto &ai = state_.ai[t];
    ai.style = style;
    ai.anchor = 0;
    ai.supports = 0;
    ai.think = 0;
    state_.decks[t] = BuildAIDeck(style, static_cast<std::uint32_t>(Random() * 4294967296.));
    for (int i = 0; i < 4; ++i)
        state_.hands[t][i] = state_.decks[t][i];
    state_.queues[t] = std::vector<std::string>(state_.decks[t].begin() + 4, state_.decks[t].end());
    Decision(team, "AI STYLE", style);
    return true;
}
bool Match::CanTarget(const Entity &s, const Entity &t) const {
    if (s.dead || t.dead || s.id == t.id || s.team == t.team)
        return false;
    if (Crown(s))
        return t.kind == EntityKind::Troop;
    const Card *c = FindCard(s.cardId);
    if (!c)
        return false;
    if (c->building)
        return t.kind == EntityKind::Troop && (!t.flying || c->canHitAir);
    if (c->structuresOnly)
        return Structure(t);
    return !t.flying || c->canHitAir;
}
bool Match::InSight(const Entity &s, const Entity &t, double bonus) const {
    const Vec2 delta = Difference(t.position, s.position);
    const double length = std::hypot(delta.x, delta.z);
    const double dot = delta.x * s.facing.x + delta.z * s.facing.z;
    return length <= (dot >= 0 ? 8. : 5.) + bonus + s.radius * .2 + t.radius + Epsilon;
}
double Match::CombatDistance(const Entity &s, const Entity &t) const {
    const double direct = Distance(s.position, t.position);
    const auto *c = FindCard(s.cardId);
    if (s.flying || Crown(s) || s.kind == EntityKind::Building || (c && c->projectileSpeed > 0))
        return direct;
    const int a = s.position.z > 1.65    ? 1
                  : s.position.z < -1.65 ? -1
                                         : 0,
              b = t.position.z > 1.65    ? 1
                  : t.position.z < -1.65 ? -1
                                         : 0;
    if (!a || !b || a == b)
        return direct;
    double best = std::numeric_limits<double>::infinity();
    for (int lane : {-1, 1})
        best = std::min(best, Distance(s.position, {lane * 7.2, a * 1.65}) + 3.3 +
                                  Distance(t.position, {lane * 7.2, b * 1.65}));
    return best;
}
} // namespace rift
