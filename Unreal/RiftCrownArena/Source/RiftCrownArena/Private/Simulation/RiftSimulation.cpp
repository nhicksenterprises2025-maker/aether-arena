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
 } // namespace
#include "RiftCardManifest.generated.inc"
const std::vector<Card> &Cards() {
    static const std::vector<Card> cards = BuildManifestCards();
    return cards;
}
const Card *FindCard(const std::string &id) {
    for (const auto &c : Cards())
        if (c.id == id)
            return &c;
    return nullptr;
}
const std::vector<Card> &EntityDefinitions() {
    static const std::vector<Card> definitions = BuildManifestEntities();
    return definitions;
}
const Card *FindEntityDefinition(const std::string &id) {
    if (const Card *card = FindCard(id))
        return card;
    for (const auto &definition : EntityDefinitions())
        if (definition.id == id)
            return &definition;
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
    return {Clamp(std::floor(p.x + arena::HalfWidth) - arena::LastTileX,
                  -arena::LastTileX, arena::LastTileX),
            Clamp(std::floor(p.z + arena::HalfHeight) - arena::LastTileZ,
                  -arena::LastTileZ, arena::LastTileZ)};
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
            e.position = {lane == 0 ? 0 : lane * arena::GuardX,
                          Sign(team) * (lane == 0 ? arena::CoreDepth : arena::GuardDepth)};
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
            auto &sourceRecords = state_.entityTelemetry[Index(credit->second.team)];
            auto entity = sourceRecords.find(credit->second.source);
            if (entity != sourceRecords.end()) {
                auto &observed = entity->second.observed;
                if (slow) {
                    observed.slowTrackedSeconds += exposure;
                    observed.slowTime += affected;
                } else {
                    observed.stunTrackedSeconds += exposure;
                    observed.stunTime += affected;
                }
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
    bodyStepStarts_.clear();
    for (const auto &entity : state_.entities)
        if (!entity.dead)
            bodyStepStarts_.emplace(entity.id, entity.position);
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
    UpdateShockwaves();
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
            const Card *card = FindEntityDefinition(projectile.cardId);
            std::vector<EntityId> victims;
            for (const auto &e : state_.entities)
                if (!e.dead && e.team != projectile.team && e.id != projectile.target && card &&
                    CanHit(*card, e)) {
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
    // Resolve fixed-area casts against positions after this tick's movement.
    // Remove the pending cast first: lethal damage may finish the match and
    // clear every remaining cast while ApplySpell is still on the stack.
    for (std::size_t i = 0; i < state_.spellCasts.size();) {
        if (state_.spellCasts[i].impactAt > state_.elapsed + Epsilon) {
            ++i;
            continue;
        }
        const SpellCast cast = state_.spellCasts[i];
        state_.spellCasts.erase(state_.spellCasts.begin() + i);
        const Card *card = FindCard(cast.cardId);
        if (!card)
            continue;
        auto &event = Emit("spell_impact", cast.team);
        event.playId = cast.playId;
        event.cardId = cast.cardId;
        event.position = cast.position;
        event.until = cast.impactAt;
        ApplySpell(cast.team, *card, cast.position, cast.playId);
        if (!Running(state_.phase))
            return;
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
            if (!e.dead && e.team != h.team && (e.kind == EntityKind::Troop || c->dotStructureDamage > 0) &&
                Distance(e.position, h.position) <= h.radius + e.radius * .2)
                ++occupants;
        auto &tele = state_.telemetry[Index(h.team)][h.cardId];
        // A delayed spell may land at this tick's endpoint; exposure begins at
        // impact, rather than crediting occupants before the hazard existed.
        const double activeDt = std::max(0., std::min(state_.elapsed, h.expires) - std::max(old, h.born));
        tele.zoneOccupancy += occupants * activeDt;
        tele.zoneSeconds += activeDt;
        while (h.nextTick <= state_.elapsed + Epsilon && h.nextTick <= h.expires + Epsilon && h.ticks < 5) {
            std::vector<EntityId> victims;
            for (const auto &e : state_.entities)
                if (!e.dead && e.team != h.team && (e.kind == EntityKind::Troop || c->dotStructureDamage > 0) &&
                    Distance(e.position, h.position) <= h.radius + e.radius * .2)
                    victims.push_back(e.id);
            const auto copy = h;
            ++h.ticks;
            h.nextTick += c->dotInterval;
            ++tele.dotTicks;
            Emit("hazard_tick", h.team).playId = h.playId;
            for (auto id : victims) {
                const Entity *victim = Get(id);
                Damage(id, victim && Structure(*victim) ? c->dotStructureDamage : c->dotDamage,
                       copy.team, copy.cardId, copy.playId, 0, "dot");
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
    state_.shockwaves.clear();
    state_.spellCasts.clear();
    for (auto &team : state_.telemetry)
        for (auto &[id, t] : team)
            t.surviving = t.childSurviving = 0;
    for (const auto &e : state_.entities)
        if (!e.dead && !Crown(e)) {
            auto &t = state_.telemetry[Index(e.team)][e.attributionCardId.empty() ? e.cardId : e.attributionCardId];
            if (e.parentEntityId) ++t.childSurviving;
            else ++t.surviving;
            t.lifetime += state_.elapsed - e.born;
            auto &observed = state_.entityTelemetry[Index(e.team)][e.id].observed;
            observed.surviving = 1;
            observed.lifetime += state_.elapsed - e.born;
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
    state_.shockwaves.clear();
    state_.hazards.clear();
    state_.spellCasts.clear();
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
    if (!std::isfinite(p.x) || !std::isfinite(p.z) || std::abs(p.x) > arena::HalfWidth ||
        std::abs(p.z) > arena::HalfHeight)
        return false;
    if (sandbox || c.spell)
        return true;
    auto legal = [&](Vec2 q) {
        if (std::abs(q.x) > arena::HalfWidth || std::abs(q.z) > arena::HalfHeight)
            return false;
        if (q.z * Sign(team) >= 2.15)
            return true;
        const double depth = -q.z * Sign(team);
        if (depth < 2.25 || depth > arena::PocketMaxDepth || std::abs(q.x) < 2 ||
            std::abs(q.x) > arena::PocketOuterX)
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
        std::abs(p.x) > arena::HalfWidth || std::abs(p.z) > arena::HalfHeight)
        return false;
    const int t = Index(team);
    const Card *c = FindCard(state_.hands[t][index]);
    p = SnapToTile(p);
    if (!c || state_.aether[t] + Epsilon < c->cost || !CanPlace(team, *c, p))
        return false;
    std::vector<Vec2> positions;
    // Reserve every body atomically before spending or cycling the hand. A full
    // deployment area cannot consume a card while silently losing a member.
    if (!PlanDeployment(team, *c, p, false, positions))
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
    e.until = c->castDelay > 0 ? state_.elapsed + c->castDelay : 0;
    auto &observer = state_.ai[1 - t];
    observer.estimatedOpponentAether = std::max(0., observer.estimatedOpponentAether - c->cost);
    observer.observedCycle.push_back(c->id);
    if (observer.observedCycle.size() > 12)
        observer.observedCycle.erase(observer.observedCycle.begin());
    Deploy(team, *c, p, play, false, positions);
    return true;
}
bool Match::Spawn(Team team, const std::string &id, Vec2 p) {
    const Card *c = FindCard(id);
    if (!c || !Running(state_.phase) || !CanPlace(team, *c, p, true))
        return false;
    p = SnapToTile(p);
    std::vector<Vec2> positions;
    if (!PlanDeployment(team, *c, p, true, positions))
        return false;
    const PlayId play = nextPlay_++;
    auto &e = Emit("card_play", team);
    e.cardId = id;
    e.playId = play;
    e.position = p;
    e.sandbox = true;
    e.reason = "developer_spawn";
    e.aetherBefore = e.aetherAfter = state_.aether[Index(team)];
    e.until = c->castDelay > 0 ? state_.elapsed + c->castDelay : 0;
    Deploy(team, *c, p, play, true, positions);
    return true;
}
bool Match::PlanDeployment(Team team, const Card &c, Vec2 p, bool sandbox,
                           std::vector<Vec2> &positions) const {
    positions.clear();
    if (c.spell)
        return true;
    if (c.count < 1)
        return false;
    const double radius = c.collisionRadius;
    const double spacing = std::max(1.12, radius * 2 + .06);
    const bool centerSplit = std::abs(p.x) <= .5 + Epsilon;
    const bool establishedSideFormation = !centerSplit && (c.count == 2 || (c.count == 5 && c.flying));
    std::vector<Vec2> offsets;
    offsets.reserve(static_cast<std::size_t>(c.count));
    if (c.count == 1)
        offsets.push_back({0, 0});
    else if (c.count == 2) {
        // Tile centers nearest the divider are +/- .5. Give two-unit cards
        // enough lateral spread for one member to land on either side.
        const double half = std::max(radius + .03, centerSplit ? .6 : .42);
        offsets = {{-half, .12}, {half, -.12}};
    } else if (c.count == 5 && establishedSideFormation) {
        // Keep existing side-lane flying formations stable; only a placement
        // at the divider needs the wider balanced split formation.
        offsets = {{-.86, -.18}, {0, -.42}, {.86, -.18}, {-.43, .38}, {.43, .38}};
    } else if (c.count == 5) {
        // A staggered, radius-aware formation keeps all five bodies clear
        // before reservation and naturally splits a center placement 2/3.
        offsets = {{-spacing, -.38 * spacing}, {0, -.38 * spacing}, {spacing, -.38 * spacing},
                   {-.5 * spacing, .486 * spacing}, {.5 * spacing, .486 * spacing}};
    } else if (c.id == "stampede" && c.count == 15) {
        // Five bodies across, three ranks deep. Individual landing positions
        // still determine bridge choice; the central column remains on the
        // side of the snapped drop instead of overriding member lanes.
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 5; ++column)
                offsets.push_back({(column - 2.) * spacing, (row - 1.) * spacing});
    } else {
        // Every member gets its own offset, including large swarms. Short
        // final rows are centered instead of biasing the swarm to one lane.
        const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(c.count))));
        const int rows = (c.count + columns - 1) / columns;
        for (int row = 0; row < rows; ++row) {
            const int width = std::min(columns, c.count - row * columns);
            for (int column = 0; column < width; ++column)
                offsets.push_back({(column - (width - 1) * .5) * spacing,
                                   (row - (rows - 1) * .5) * spacing});
        }
    }
    if ((c.id == "mini_stampede" || c.id == "stampede") && !sandbox) {
        // At the first legal tile, the three forward hogs otherwise request
        // the forbidden river strip and can be independently sent to distant
        // free contacts. Keep the whole formation anchored to the hand drop.
        double minimum = 1e9, maximum = -1e9;
        for (const auto &offset : offsets) {
            minimum = std::min(minimum, p.z * Sign(team) + offset.z);
            maximum = std::max(maximum, p.z * Sign(team) + offset.z);
        }
        const double low = p.z * Sign(team) > 0 ? 2.15 : -arena::PocketMaxDepth;
        const double high = p.z * Sign(team) > 0 ? arena::HalfHeight - .40 - radius * .72 : -2.25;
        const double shift = minimum < low ? low - minimum : maximum > high ? high - maximum : 0;
        for (auto &offset : offsets)
            offset.z += shift;
    }
    positions.reserve(static_cast<std::size_t>(c.count));
    std::vector<Entity> planned;
    planned.reserve(static_cast<std::size_t>(c.count));
    for (int i = 0; i < c.count; ++i) {
        Entity body;
        body.id = nextEntity_ + static_cast<EntityId>(i);
        body.team = team;
        body.kind = c.building ? EntityKind::Building : EntityKind::Troop;
        body.cardId = c.id;
        body.radius = radius;
        body.flying = c.flying;
        body.facing = {0, -Sign(team)};
        const Vec2 requested{Clamp(p.x + offsets[i].x, -arena::DeploymentMaxX, arena::DeploymentMaxX),
                             p.z + offsets[static_cast<std::size_t>(i)].z *
                                       (establishedSideFormation ? 1. : Sign(team))};
        if (!ResolveBodyPlacement(requested, body, sandbox, planned, body.position)) {
            positions.clear();
            return false;
        }
        positions.push_back(body.position);
        planned.push_back(body);
    }
    return true;
}
void Match::Deploy(Team team, const Card &c, Vec2 p, PlayId play, bool sandbox,
                   const std::vector<Vec2> &positions) {
    if (c.spell) {
        if (c.castDelay > 0) {
            SpellCast cast;
            cast.playId = play;
            cast.team = team;
            cast.cardId = c.id;
            cast.position = p;
            cast.born = state_.elapsed;
            cast.impactAt = state_.elapsed + c.castDelay;
            state_.spellCasts.push_back(cast);
            auto &event = Emit("spell_cast", team);
            event.playId = play;
            event.cardId = c.id;
            event.position = p;
            event.until = cast.impactAt;
            event.sandbox = sandbox;
        } else {
            ApplySpell(team, c, p, play);
        }
        return;
    }
    std::vector<EntityId> deployed;
    for (int i = 0; i < c.count; ++i) {
        Entity e;
        e.id = nextEntity_++;
        e.team = team;
        e.kind = c.building ? EntityKind::Building : EntityKind::Troop;
        e.cardId = c.id;
        e.playId = play;
        e.position = positions[static_cast<std::size_t>(i)];
        e.facing = {0, -Sign(team)};
        e.hp = e.maxHp = c.hp;
        e.shieldHP = e.maxShieldHP = c.shieldHP;
        e.attributionCardId = c.id;
        e.radius = c.collisionRadius;
        e.flying = c.flying;
        e.born = state_.elapsed;
        e.memberCount = c.count;
        e.cooldown = c.initialAttackCooldown;
        e.lane = e.position.x < 0 ? -1 : 1;
        e.stuckPosition = e.position;
        state_.entities.push_back(e);
        EntityTelemetry record;
        record.id = e.id;
        record.playId = play;
        record.cardId = record.attributionCardId = c.id;
        record.observed.spawns = 1;
        if (c.building) record.observed.buildingCapacity = c.lifetime;
        state_.entityTelemetry[Index(team)].emplace(e.id, std::move(record));
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
        event.shieldHP = event.maxShieldHP = c.shieldHP;
        event.attributionCardId = c.id;
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
            if (c.dotDamage > 0 && c.dotStructureDamage <= 0 && Structure(e))
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
    if (c.invisibilityDuration > 0)
        ApplyInvisibility(team, c, p, play);
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
    const Card *victim = FindEntityDefinition(t->cardId);
    const auto attackerRecord = state_.entityTelemetry[Index(team)].find(source);
    const std::string creditCard = attackerRecord == state_.entityTelemetry[Index(team)].end()
                                       ? card : attackerRecord->second.attributionCardId;
    const std::string victimCredit = t->attributionCardId.empty() ? t->cardId : t->attributionCardId;
    if (t->shieldHP > 0) {
        const double shieldLoss = std::min(t->shieldHP, amount);
        const bool protect = victim && victim->shieldOverflowProtected;
        const double overflow = protect ? std::max(0., amount - shieldLoss) : 0;
        t->shieldHP -= shieldLoss;
        if (t->shieldHP <= Epsilon) {
            t->shieldHP = 0;
            t->shieldBrokenAt = state_.elapsed;
        }
        auto &event = Emit("shield_damage", team);
        event.source = source;
        event.target = id;
        event.targetTeam = t->team;
        event.targetKind = t->kind;
        event.cardId = card;
        event.playId = play;
        event.amount = shieldLoss;
        event.requested = amount;
        event.shieldOverflowBlocked = overflow;
        event.shieldHP = t->shieldHP;
        event.maxShieldHP = t->maxShieldHP;
        event.hp = t->hp;
        event.maxHp = t->maxHp;
        event.position = t->position;
        event.damageKind = kind;
        event.attributionCardId = creditCard;
        if (attackerRecord != state_.entityTelemetry[Index(team)].end())
            event.parentEntityId = attackerRecord->second.parentEntityId;
        auto &taken = state_.telemetry[Index(t->team)][victimCredit];
        taken.shieldDamageTaken += shieldLoss;
        taken.shieldAbsorbed += shieldLoss + overflow;
        taken.overflowPrevented += overflow;
        auto &observed = state_.entityTelemetry[Index(t->team)][id].observed;
        observed.shieldDamageTaken += shieldLoss;
        observed.shieldAbsorbed += shieldLoss + overflow;
        observed.overflowPrevented += overflow;
        if (!creditCard.empty()) {
            auto &attacker = state_.telemetry[Index(team)][creditCard];
            attacker.shieldDamage += shieldLoss;
            if (play) {
                auto &targets = castTargets_[play];
                if (std::find(targets.begin(), targets.end(), id) == targets.end()) {
                    targets.push_back(id);
                    ++attacker.targets;
                }
            }
        }
        if (attackerRecord != state_.entityTelemetry[Index(team)].end())
            attackerRecord->second.observed.shieldDamage += shieldLoss;
        amount = protect ? 0 : amount - shieldLoss;
        if (amount <= Epsilon) return;
    }
    const double actual = std::min(t->hp, amount), overkill = std::max(0., amount - t->hp);
    t->hp -= actual;
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
    event.attributionCardId = creditCard;
    if (attackerRecord != state_.entityTelemetry[Index(team)].end())
        event.parentEntityId = attackerRecord->second.parentEntityId;
    if (!creditCard.empty()) {
        auto &tele = state_.telemetry[Index(team)][creditCard];
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
            towerCredits_[id][{team, creditCard}] += actual;
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
    if (attackerRecord != state_.entityTelemetry[Index(team)].end()) {
        auto &observed = attackerRecord->second.observed;
        if (Crown(*t)) observed.towerDamage += actual;
        else if (t->kind == EntityKind::Building) observed.buildingDamage += actual;
        else observed.troopDamage += actual;
        observed.overkill += overkill;
        if (kind == "initial") observed.initialDamage += actual;
        if (kind == "dot") observed.dotDamage += actual;
        if (kind == "aura") observed.auraDamage += actual;
    }
    if (victim) {
        auto &v = state_.telemetry[Index(t->team)][victimCredit];
        v.damageTaken += actual;
        if (t->kind == EntityKind::Building)
            v.prevented += actual;
        auto &observed = state_.entityTelemetry[Index(t->team)][id].observed;
        observed.damageTaken += actual;
        if (t->kind == EntityKind::Building) observed.prevented += actual;
    }
    if (t->hp > Epsilon && victim && victim->healthCloakThreshold > 0 &&
        !t->healthCloakTriggered && t->hp <= t->maxHp * victim->healthCloakThreshold + Epsilon) {
        t->healthCloakTriggered = true;
        const EntityId cloakedId = t->id;
        const Team cloakedTeam = t->team;
        const PlayId cloakedPlay = t->playId;
        CloakBody(*t, *victim, cloakedPlay, cloakedId);
        DropCloakedLocks(cloakedTeam, *victim, {cloakedId}, cloakedPlay);
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
    const Entity deceased = *t; // Child insertion may reallocate the entity vector.
    const Card *v = FindEntityDefinition(t->cardId);
    if (v) {
        auto &victim = state_.telemetry[Index(t->team)][t->attributionCardId.empty() ? t->cardId : t->attributionCardId];
        if (t->parentEntityId) ++victim.childDeaths;
        else ++victim.deaths;
        victim.lifetime += state_.elapsed - t->born;
        auto &observed = state_.entityTelemetry[Index(t->team)][id].observed;
        ++observed.deaths;
        observed.lifetime += state_.elapsed - t->born;
        ev.targetCost = static_cast<double>(v->cost) / v->count;
        if (!card.empty()) {
            auto attacker = state_.entityTelemetry[Index(team)].find(source);
            const std::string credit = attacker == state_.entityTelemetry[Index(team)].end()
                                           ? card : attacker->second.attributionCardId;
            auto &killer = state_.telemetry[Index(team)][credit];
            ++killer.kills;
            killer.killValue += ev.targetCost;
            if (attacker != state_.entityTelemetry[Index(team)].end()) {
                ++attacker->second.observed.kills;
                attacker->second.observed.killValue += ev.targetCost;
            }
        }
    }
    if (!Crown(deceased)) {
        if (v && v->deathSpawnCount > 0 && !v->deathSpawnId.empty())
            SpawnDeathChildren(deceased, *v);
        return;
    }
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
void Match::SpawnDeathChildren(const Entity &parent, const Card &definition) {
    const Card *child = FindEntityDefinition(definition.deathSpawnId);
    if (!child || child->spell || child->building || definition.deathSpawnCount <= 0)
        return;
    std::vector<Entity> planned;
    planned.reserve(static_cast<std::size_t>(definition.deathSpawnCount));
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(definition.deathSpawnCount))));
    const int rows = (definition.deathSpawnCount + columns - 1) / columns;
    const double pitch = 2 * child->collisionRadius + .06;
    for (int n = 0; n < definition.deathSpawnCount; ++n) {
        Entity body;
        body.id = nextEntity_ + static_cast<EntityId>(n);
        body.parentEntityId = parent.id;
        body.attributionCardId = parent.attributionCardId.empty() ? parent.cardId : parent.attributionCardId;
        body.cardId = child->id;
        body.team = parent.team;
        body.kind = EntityKind::Troop;
        body.playId = parent.playId;
        body.radius = child->collisionRadius;
        body.flying = child->flying;
        body.facing = parent.facing;
        body.hp = body.maxHp = child->hp;
        body.shieldHP = body.maxShieldHP = child->shieldHP;
        body.born = state_.elapsed;
        body.memberCount = definition.deathSpawnCount;
        body.cooldown = child->initialAttackCooldown;
        const Vec2 requested{parent.position.x + (n % columns - (columns - 1) * .5) * pitch,
                             parent.position.z + (n / columns - (rows - 1) * .5) * pitch};
        if (!ResolveBodyPlacement(requested, body, false, planned, body.position)) {
            auto &failure = Emit("death_spawn_blocked", parent.team);
            failure.source = parent.id;
            failure.playId = parent.playId;
            failure.cardId = parent.cardId;
            failure.count = definition.deathSpawnCount;
            return; // Never insert an overlapping or partially paid formation.
        }
        body.lane = body.position.x < 0 ? -1 : 1;
        body.stuckPosition = body.position;
        planned.push_back(body);
    }
    for (const auto &body : planned) {
        ++nextEntity_;
        state_.entities.push_back(body);
        EntityTelemetry record;
        record.id = body.id;
        record.parentEntityId = parent.id;
        record.playId = parent.playId;
        record.cardId = child->id;
        record.attributionCardId = body.attributionCardId;
        record.observed.spawns = 1;
        state_.entityTelemetry[Index(body.team)].emplace(body.id, std::move(record));
        ++state_.telemetry[Index(body.team)][body.attributionCardId].childSpawns;
        auto &event = Emit("entity_spawn", body.team);
        event.source = body.id;
        event.parentEntityId = parent.id;
        event.attributionCardId = body.attributionCardId;
        event.playId = body.playId;
        event.cardId = body.cardId;
        event.position = body.position;
        event.targetKind = body.kind;
        event.hp = event.maxHp = body.hp;
        event.shieldHP = event.maxShieldHP = body.shieldHP;
        event.count = definition.deathSpawnCount;
        event.reason = "death_spawn";
    }
    for (const auto &body : planned)
        PullDeployment(body.id);
}
void Match::ApplyInvisibility(Team team, const Card &card, Vec2 point, PlayId play) {
    std::vector<EntityId> cloaked;
    for (auto &body : state_.entities) {
        if (body.dead || body.team != team || body.kind != EntityKind::Troop)
            continue;
        if (!card.globalFriendlyInvisibility && Distance(body.position, point) > card.spellRadius + Epsilon)
            continue;
        cloaked.push_back(body.id);
        CloakBody(body, card, play);
    }
    DropCloakedLocks(team, card, cloaked, play);
}
void Match::CloakBody(Entity &body, const Card &card, PlayId play, EntityId source) {
    body.invisibleUntil = std::max(body.invisibleUntil, state_.elapsed + card.invisibilityDuration);
    body.invisibilitySource = source;
    body.invisibilityPlayId = play;
    body.invisibilityCardId = card.id;
    auto &event = Emit("invisibility_apply", body.team);
    event.source = source;
    event.target = body.id;
    event.targetTeam = body.team;
    event.cardId = card.id;
    event.playId = play;
    event.until = body.invisibleUntil;
    event.amount = card.invisibilityDuration;
    event.position = body.position;
    if (source) event.reason = "health_threshold";
}
void Match::DropCloakedLocks(Team team, const Card &card, const std::vector<EntityId> &cloaked, PlayId play) {
    if (!card.breakExistingLocks)
        return;
    for (auto &enemy : state_.entities) {
        if (enemy.dead || enemy.team == team)
            continue;
        auto hidden = [&](EntityId id) { return std::find(cloaked.begin(), cloaked.end(), id) != cloaked.end(); };
        if (!hidden(enemy.target) && !hidden(enemy.hardLock) && !hidden(enemy.forcedTarget))
            continue;
        const EntityId dropped = hidden(enemy.target) ? enemy.target
                               : hidden(enemy.hardLock) ? enemy.hardLock : enemy.forcedTarget;
        enemy.target = enemy.hardLock = enemy.forcedTarget = 0;
        enemy.forcedUntil = 0;
        enemy.path.clear();
        enemy.repathClock = enemy.scanClock = 0;
        auto &event = Emit("target_drop", team);
        event.source = enemy.id;
        event.target = dropped;
        event.targetTeam = team;
        event.cardId = card.id;
        event.playId = play;
        event.reason = "invisibility";
        event.until = state_.elapsed + card.invisibilityDuration;
    }
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
    state_.shockwaves.clear();
    state_.hazards.clear();
    state_.spellCasts.clear();
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
std::uint8_t TargetMaskFor(const Card &c) {
    if (c.targetMask)
        return c.targetMask;
    if (c.spell)
        return c.towerDamage > 0 ? targets::All : targets::GroundTroop | targets::AirTroop;
    if (c.structuresOnly)
        return targets::Structure;
    return targets::GroundTroop | (c.canHitAir ? targets::AirTroop : 0) |
           (c.building ? 0 : targets::Structure);
}
bool CanHit(const Card &c, const Entity &t) {
    const std::uint8_t bit = Structure(t) ? targets::Structure
                              : t.flying ? targets::AirTroop : targets::GroundTroop;
    return (TargetMaskFor(c) & bit) != 0;
}
bool Match::CanTarget(const Entity &s, const Entity &t) const {
    if (s.dead || t.dead || s.id == t.id || s.team == t.team)
        return false;
    if (t.kind == EntityKind::Troop && t.invisibleUntil > state_.elapsed + Epsilon)
        return false;
    if (Crown(s))
        return t.kind == EntityKind::Troop;
    const Card *c = FindEntityDefinition(s.cardId);
    if (!c)
        return false;
    return CanHit(*c, t);
}
bool Match::InSight(const Entity &s, const Entity &t) const {
    const Vec2 delta = Difference(t.position, s.position);
    const double length = std::hypot(delta.x, delta.z);
    const double dot = delta.x * s.facing.x + delta.z * s.facing.z;
    return length <= (dot >= 0 ? detection::Front : detection::Rear) + Epsilon;
}
double Match::CombatDistance(const Entity &s, const Entity &t) const {
    const double direct = Distance(s.position, t.position);
    const auto *c = FindEntityDefinition(s.cardId);
    if (s.flying || Crown(s) || s.kind == EntityKind::Building || (c && c->projectileSpeed > 0))
        return direct;
    const int a = s.position.z > arena::RiverHalfWidth ? 1
                  : s.position.z < -arena::RiverHalfWidth ? -1 : 0;
    const int b = t.position.z > arena::RiverHalfWidth ? 1
                  : t.position.z < -arena::RiverHalfWidth ? -1 : 0;
    if (!a || !b || a == b)
        return direct;
    const int lane = s.bridge ? s.bridge : s.position.x < 0 ? -1 : s.position.x > 0 ? 1
                                                                                 : s.lane < 0 ? -1 : 1;
    return Distance(s.position, {lane * arena::BridgeCenterX, a * arena::RiverHalfWidth}) +
           arena::RiverHalfWidth * 2 +
           Distance(t.position, {lane * arena::BridgeCenterX, b * arena::RiverHalfWidth});
}
} // namespace rift
