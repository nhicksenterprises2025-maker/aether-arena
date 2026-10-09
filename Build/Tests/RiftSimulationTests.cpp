#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rift;
namespace {
int passed = 0;
std::uint64_t collisionPairChecks = 0, collisionSweepChecks = 0;
int collisionPaidFixtures = 0, collisionPaidPlays = 0, collisionMembers = 0;
double collisionMinimumGap = 1e9, collisionMinimumSweepGap = 1e9;
void Check(bool yes, const std::string &message) {
    if (!yes)
        throw std::runtime_error(message);
}
void Near(double a, double b, double tolerance, const std::string &message) {
    Check(std::abs(a - b) <= tolerance,
          message + " (" + std::to_string(a) + " vs " + std::to_string(b) + ")");
}
MatchOptions Quiet() {
    MatchOptions o;
    o.aiEnabled = {false, false};
    o.decks = {DefaultDeck(), DefaultDeck()};
    return o;
}
const Entity &Tower(const Match &m, Team team, EntityKind kind, int lane = 0) {
    for (const auto &e : m.State().entities)
        if (e.team == team && e.kind == kind && e.lane == lane)
            return e;
    throw std::runtime_error("tower missing");
}
const Entity &Unit(const Match &m, const std::string &id, Team team) {
    for (const auto &e : m.State().entities)
        if (e.cardId == id && e.team == team && !e.dead)
            return e;
    throw std::runtime_error("unit missing " + id);
}
int Count(const Match &m, const std::string &id, Team team, bool living = true) {
    int n = 0;
    for (const auto &e : m.State().entities)
        n += e.cardId == id && e.team == team && (!living || !e.dead);
    return n;
}
const Entity *FindEntity(const Match &m, EntityId id) {
    for (const auto &e : m.State().entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
const Entity &ById(const Match &m, EntityId id) {
    if (const auto *entity = FindEntity(m, id))
        return *entity;
    throw std::runtime_error("entity missing " + std::to_string(id));
}
double SegmentDistance(Vec2 from, Vec2 to, Vec2 point) {
    const double x = to.x - from.x, z = to.z - from.z, length = x * x + z * z;
    const double along = length > 1e-12
                             ? std::clamp(((point.x - from.x) * x + (point.z - from.z) * z) / length,
                                          0., 1.)
                             : 0.;
    return std::hypot(from.x + x * along - point.x, from.z + z * along - point.z);
}
void ClearOwnStructures(const Match &m, const Entity &unit, Vec2 from) {
    const double edge = .40 + unit.radius * .72;
    Check(std::abs(unit.position.x) <= arena::HalfWidth - edge + 1e-8 &&
              std::abs(unit.position.z) <= arena::HalfHeight - edge + 1e-8,
          "ground member stays inside radius-aware arena bounds");
    for (const auto &e : m.State().entities)
        if (!e.dead && e.team == unit.team && e.kind != EntityKind::Troop)
            Check(SegmentDistance(from, unit.position, e.position) + 1e-8 >=
                      e.radius + unit.radius + .22,
                  "ground movement clears entire own-structure segment");
    if (std::abs(unit.position.z) < 1.65 + .28) {
        const double half = std::max(.34, 2.1 - .16 - unit.radius * .92);
        Check(std::abs(unit.position.x + 7.2) <= half + 1e-8 ||
                  std::abs(unit.position.x - 7.2) <= half + 1e-8,
              "ground unit stays on a radius-aware bridge");
    }
}
// Observational checks deliberately do not call the production collision or
// navigation helpers: a clear path result alone cannot establish body contact.
bool PhysicalPair(const Entity &a, const Entity &b) {
    if (a.dead || b.dead || a.id == b.id)
        return false;
    const bool airA = a.kind == EntityKind::Troop && a.flying;
    const bool airB = b.kind == EntityKind::Troop && b.flying;
    return airA == airB;
}
void BodyClearance(const Match &m, const std::string &label) {
    const auto &entities = m.State().entities;
    for (std::size_t i = 0; i < entities.size(); ++i)
        for (std::size_t j = i + 1; j < entities.size(); ++j) {
            const auto &a = entities[i], &b = entities[j];
            if (!PhysicalPair(a, b))
                continue;
            const double gap = std::hypot(a.position.x - b.position.x, a.position.z - b.position.z) -
                               a.radius - b.radius;
            collisionMinimumGap = std::min(collisionMinimumGap, gap);
            ++collisionPairChecks;
            const double required = a.kind != EntityKind::Troop || b.kind != EntityKind::Troop ? .22 : .02;
            Check(gap >= required - 1e-8, label + " body separation " + a.cardId + "/" + b.cardId +
                                            " gap=" + std::to_string(gap));
        }
}
void SweptBodyClearance(const Match &m, const std::vector<Entity> &before, const std::string &label) {
    BodyClearance(m, label);
    for (const auto &start : before)
        if (!start.dead && start.kind == EntityKind::Troop)
            if (const auto *after = FindEntity(m, start.id))
                Check(std::hypot(after->position.x - start.position.x, after->position.z - start.position.z) <=
                          FindCard(start.cardId)->moveSpeed / 60. + 1e-6,
                      label + " never teleports or exceeds authored movement speed " + start.cardId);
    for (std::size_t i = 0; i < before.size(); ++i)
        for (std::size_t j = i + 1; j < before.size(); ++j) {
            const auto &a = before[i], &b = before[j];
            if (!PhysicalPair(a, b))
                continue;
            const auto &afterA = ById(m, a.id), &afterB = ById(m, b.id);
            if (afterA.dead || afterB.dead)
                continue;
            const Vec2 from{a.position.x - b.position.x, a.position.z - b.position.z};
            const Vec2 to{afterA.position.x - afterB.position.x, afterA.position.z - afterB.position.z};
            const double x = to.x - from.x, z = to.z - from.z, length = x * x + z * z;
            const double along = length > 0 ? std::clamp(-(from.x * x + from.z * z) / length, 0., 1.) : 0.;
            const double gap = std::hypot(from.x + along * x, from.z + along * z) - a.radius - b.radius;
            collisionMinimumSweepGap = std::min(collisionMinimumSweepGap, gap);
            ++collisionSweepChecks;
            const double required = a.kind != EntityKind::Troop || b.kind != EntityKind::Troop ? .22 : .02;
            Check(gap >= required - 1e-8, label + " swept relative separation " + a.cardId + "/" +
                                          b.cardId + " gap=" + std::to_string(gap));
        }
}
MatchOptions CollisionOptions(const std::string &first, const std::string &second) {
    auto options = Quiet();
    std::vector<std::string> deck{first};
    if (second != first)
        deck.push_back(second);
    for (const auto &card : Cards())
        if (card.id != first && card.id != second && deck.size() < 8)
            deck.push_back(card.id);
    options.decks = {deck, deck};
    return options;
}
std::vector<Entity> PaidCollisionCard(Match &m, Team team, const std::string &id, Vec2 point) {
    const int side = int(team);
    int handIndex = -1;
    for (int n = 0; n < 4; ++n)
        if (m.State().hands[side][n] == id)
            handIndex = n;
    Check(handIndex >= 0, "collision fixture card is actually in the paid hand " + id);
    const auto *card = FindCard(id);
    const auto hand = m.State().hands[side];
    const auto queue = m.State().queues[side];
    const auto spent = m.State().spent[side];
    const auto eventStart = m.Events().size();
    m.SetAether(team, 10);
    Check(m.Play(team, handIndex, point), "collision fixture paid deployment accepted " + id);
    ++collisionPaidPlays;
    Near(m.State().aether[side], 10 - card->cost, 1e-9, "collision pays canonical cost");
    Near(m.State().spent[side], spent + card->cost, 1e-9, "collision pays once");
    for (int n = 0; n < 4; ++n)
        Check(m.State().hands[side][n] == (n == handIndex ? queue[0] : hand[n]), "collision cycles selected slot only");
    Check(m.State().queues[side].back() == id, "collision queues selected card once");
    int plays = 0;
    std::vector<Entity> members;
    for (std::size_t n = eventStart; n < m.Events().size(); ++n) {
        const auto &event = m.Events()[n];
        if (event.type == "card_play") {
            ++plays;
            Check(event.cardId == id && !event.sandbox, "collision remains a real paid cast");
            Near(std::hypot(event.position.x - SnapToTile(point).x, event.position.z - SnapToTile(point).z),
                 0, 1e-9, "collision play keeps the requested tile");
        }
        if (event.type == "entity_spawn") {
            const auto &entity = ById(m, event.source);
            Check(entity.cardId == id && entity.team == team && entity.playId == event.playId && !event.sandbox,
                  "collision spawn preserves every actual paid member identity");
            Near(std::hypot(event.position.x - entity.position.x, event.position.z - entity.position.z),
                 0, 1e-9, "spawn event records resolved body position");
            Near(entity.hp, card->hp, 0, "collision keeps canonical per-member HP");
            Near(entity.radius, card->building ? card->footprint * .52 : card->scale * .44,
                 1e-12, "collision keeps canonical per-member radius");
            members.push_back(entity);
        }
    }
    Check(plays == 1 && int(members.size()) == (card->spell ? 0 : card->count), "collision no missing or extra paid members");
    collisionMembers += int(members.size());
    return members;
}
void Test(const char *name, const std::function<void()> &fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
}
void Same(const Match &a, const Match &b) {
    const auto &x = a.State();
    const auto &y = b.State();
    Check(x.phase == y.phase && x.winner == y.winner && x.randomState == y.randomState,
          "deterministic phase/RNG");
    Near(x.elapsed, y.elapsed, 1e-9, "deterministic elapsed");
    Check(x.spellCasts.size() == y.spellCasts.size(), "deterministic pending cast count");
    for (std::size_t n = 0; n < x.spellCasts.size(); ++n) {
        const auto &c = x.spellCasts[n];
        const auto &d = y.spellCasts[n];
        Check(c.playId == d.playId && c.team == d.team && c.cardId == d.cardId,
              "deterministic pending cast identity");
        Near(c.position.x, d.position.x, 0, "deterministic cast X");
        Near(c.position.z, d.position.z, 0, "deterministic cast Z");
        Near(c.born, d.born, 1e-9, "deterministic cast start");
        Near(c.impactAt, d.impactAt, 1e-9, "deterministic cast deadline");
    }
    Check(x.entities.size() == y.entities.size(), "deterministic entity count");
    for (std::size_t n = 0; n < x.entities.size(); ++n) {
        const auto &e = x.entities[n];
        const auto &f = y.entities[n];
        Check(e.id == f.id && e.target == f.target && e.hardLock == f.hardLock && e.dead == f.dead,
              "deterministic target/death");
        Near(e.hp, f.hp, 1e-8, "deterministic HP");
        Near(e.position.x, f.position.x, 1e-8, "deterministic X");
        Near(e.position.z, f.position.z, 1e-8, "deterministic Z");
    }
    for (int i = 0; i < 2; ++i) {
        Near(x.aether[i], y.aether[i], 1e-8, "deterministic Aether");
        Check(x.hands[i] == y.hands[i], "deterministic hand");
    }
    Check(a.Events().size() == b.Events().size(), "deterministic events");
    for (std::size_t n = 0; n < a.Events().size(); ++n) {
        const auto &e = a.Events()[n];
        const auto &f = b.Events()[n];
        Check(e.sequence == f.sequence && e.type == f.type && e.cardId == f.cardId &&
                  e.playId == f.playId && e.target == f.target,
              "deterministic event identity and order");
        Near(e.time, f.time, 1e-9, "deterministic event time");
        Near(e.until, f.until, 1e-9, "deterministic event deadline");
        Near(e.amount, f.amount, 1e-8, "deterministic event amount");
    }
}
} // namespace
int main() {
    try {
        Test("complete exact roster and swarm DPS", [] {
            Check(Cards().size() == 14, "14 cards");
            const std::vector<double> hp{840, 423, 262, 1620, 547, 880, 480, 174, 1155, 1337, 0, 850, 0, 0},
                damage{96, 152, 58, 118, 120, 140, 77, 86, 72, 251, 262, 75, 175, 375};
            for (std::size_t n = 0; n < Cards().size(); ++n) {
                Near(Cards()[n].hp, hp[n], 0, "roster HP");
                Near(Cards()[n].damage, damage[n], 0, "roster damage");
            }
            Near(FindCard("rambeast")->chargeDamage, 255, 0, "charge");
            Check(FindCard("vampire_bats")->count == 5, "five Bats");
            Check(FindCard("twin_blades")->count == 2, "two Blades");
            Near(FindCard("archer_tower")->footprint, 1.65, 0, "building footprint");
            Near(FindCard("nova_flask")->towerDamage, 185, 0, "nova structures");
            for (const auto &card : Cards())
                Near(card.castDelay,
                     card.id == "meteor_shards" ? .75 : card.id == "bullet_burst" ? .30 : 0,
                     0, "only Meteor and Bullet have a cast delay");
        });
        Test("deck validation rejects duplicates and unknown", [] {
            Check(ValidateDeck(DefaultDeck()), "valid default");
            auto d = DefaultDeck();
            d[0] = d[1];
            Check(!ValidateDeck(d), "duplicate rejection");
            d[0] = "__proto__";
            Check(!ValidateDeck(d), "unknown rejection");
            d.pop_back();
            Check(!ValidateDeck(d), "exactly eight");
        });
        Test("half-integer snapping and finite bounds", [] {
            Near(SnapToTile({0, 0}).x, .5, 0, "X snap");
            Near(SnapToTile({0, 0}).z, .5, 0, "Z snap");
            Match m(Quiet());
            Check(!m.Play(Team::Player, 0, {100, 100}), "out of arena rejected before clamp");
            Check(!m.Spawn(Team::Enemy, "ironclad", {NAN, 0}), "NaN placement rejected");
        });
        Test("start towers dormant Core guard-only activation", [] {
            Match m(Quiet());
            const auto id = Tower(m, Team::Enemy, EntityKind::Core).id;
            Check(!Tower(m, Team::Enemy, EntityKind::Core).active, "Core initially dormant");
            m.Spawn(Team::Player, "nova_flask", {0, -arena::CoreDepth});
            Near(Tower(m, Team::Enemy, EntityKind::Core).hp, 3415, 0, "Core takes spell damage");
            Check(!Tower(m, Team::Enemy, EntityKind::Core).active, "Core hit does not activate");
            Check(m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Guard, -1).id, 0), "destroy Guard");
            Check(Tower(m, Team::Enemy, EntityKind::Core).active, "own Guard destruction activates Core");
            Check(Tower(m, Team::Enemy, EntityKind::Core).id == id, "stable tower identity");
            Check(m.State().crowns[0] == 1, "Guard one crown");
        });
        Test("own bank and lane pocket deployment", [] {
            Match m(Quiet());
            const auto *c = FindCard("ironclad");
            Check(m.CanPlace(Team::Player, *c, {7.5, 2.5}), "own bank");
            Check(!m.CanPlace(Team::Player, *c, {7.5, 1.5}), "river");
            Check(!m.CanPlace(Team::Player, *c, {-7.5, -5.5}), "intact pocket");
            m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Guard, -1).id, 0);
            Check(m.CanPlace(Team::Player, *c, {-7.5, -5.5}), "destroyed lane pocket");
            Check(!m.CanPlace(Team::Player, *c, {7.5, -5.5}), "other lane still locked");
            Check(!m.CanPlace(Team::Player, *c, {-.5, -5.5}), "Core strip excluded");
            Check(!m.CanPlace(Team::Player, *c, {-7.5, -11.5}), "deep backfield excluded");
        });
        Test("building full footprint and collision", [] {
            Match m(Quiet());
            const auto *c = FindCard("archer_tower");
            Check(!m.CanPlace(Team::Player, *c, {7.5, 2.5}), "footprint crosses bank");
            Check(m.CanPlace(Team::Player, *c, {7.5, 4.5}), "full footprint fits");
            Check(!m.CanPlace(Team::Player, *c, {8.5, 13.5}), "Tower overlap");
            m.Spawn(Team::Player, c->id, {7.5, 4.5});
            Check(!m.CanPlace(Team::Player, *c, {7.5, 5.5}), "building overlap");
            Check(m.CanPlace(Team::Player, *FindCard("nova_flask"), {0, -20.5}), "spell any board region");
        });
        Test("paid hand rotation and invalid play no cost", [] {
            Match m(Quiet());
            const auto original = m.State().hands[0];
            Check(!m.Play(Team::Player, 0, {0, -3}), "invalid enemy-side play");
            Near(m.State().aether[0], 5, 0, "invalid no spend");
            Check(m.State().hands[0] == original, "invalid no cycle");
            Check(m.Play(Team::Player, 0, {5, 7}), "valid paid deployment");
            Near(m.State().aether[0], 2, 0, "pay exact cost");
            Check(m.State().hands[0][0] == "arc_mage", "next queue front");
            Check(m.State().queues[0].back() == "ironclad", "played appended");
            Check(m.State().telemetry[0].at("ironclad").openingPlays == 1, "opening cast counted");
            Check(m.State().telemetry[0].at("ironclad").openingEligible == 1, "opening denominator");
        });
        Test("DEV free spawn no bank or cycle", [] {
            Match m(Quiet());
            auto hand = m.State().hands[1];
            m.Spawn(Team::Enemy, "vampire_bats", {0, 0});
            Check(Count(m, "vampire_bats", Team::Enemy) == 5, "full swarm");
            Near(m.State().aether[1], 5, 0, "free bank unchanged");
            Check(m.State().hands[1] == hand, "free hand unchanged");
            Check(m.Events().back().type == "entity_spawn", "spawn events");
            Check(m.State().telemetry[1].at("vampire_bats").spent == 0, "free cost excluded");
        });
        Test("Bullet Burst one hit seven VFX five member kills", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "vampire_bats", {0, 0});
            m.Spawn(Team::Player, "bullet_burst", {0, 0});
            Check(Count(m, "vampire_bats", Team::Enemy) == 5, "no hits while volley travels");
            m.Step(.30);
            Check(Count(m, "vampire_bats", Team::Enemy) == 0, "kills all five Bats");
            const auto &t = m.State().telemetry[0].at("bullet_burst");
            Near(t.troopDamage, 870, 0, "actual HP removal only");
            Near(t.kills, 5, 0, "member kills");
            Near(t.killValue, 5, 0, "swarm kill Aether value");
            Near(t.overkill, 5, 0, "overkill separate");
            Check(t.targets == 5, "unique per cast targets");
        });
        Test("Nova no friendly fire and exact structures", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "boulderback", {0, 0});
            m.Spawn(Team::Player, "boulderback", {0, 0});
            m.Spawn(Team::Player, "nova_flask", {0, 0});
            Near(Unit(m, "boulderback", Team::Enemy).hp, 1245, 0, "375 troop damage");
            Near(Unit(m, "boulderback", Team::Player).hp, 1620, 0, "friendly immune");
            m.Spawn(Team::Player, "bullet_burst", {0, -arena::CoreDepth});
            Near(Tower(m, Team::Enemy, EntityKind::Core).hp, 3600, 0, "Bullet damage is delayed");
            m.Step(.30);
            Near(Tower(m, Team::Enemy, EntityKind::Core).hp, 3545, 0, "55 structure damage");
            Check(m.State().telemetry[0].at("bullet_burst").connected == 1,
                  "delayed Crown connection credited once");
        });
        Test("Meteor initial and inclusive fifth DOT tick no structures", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "boulderback", {8.5, 10.5});
            m.Spawn(Team::Player, "meteor_shards", {8.5, 10.5});
            Near(m.State().telemetry[0].at("meteor_shards").initialDamage, 0, 0, "no initial damage in flight");
            Check(m.State().hazards.empty(), "no DOT zone in flight");
            m.Step(.75);
            Near(m.State().telemetry[0].at("meteor_shards").initialDamage, 262, 0, "initial262");
            m.Step(5);
            const auto &t = m.State().telemetry[0].at("meteor_shards");
            Check(t.dotTicks == 5, "five scheduled ticks");
            Near(t.dotDamage, 200, 1e-8, "five40damage ticks");
            Near(t.towerDamage, 0, 0, "no structure damage");
            Near(t.zoneSeconds, 5, 1e-8, "exact five-second zone exposure");
            Check(m.State().hazards.empty(), "expired zone after final tick");
        });
        Test("AI Meteor zone exposure begins at its delayed impact endpoint", [] {
            auto options = Quiet();
            options.aiEnabled[0] = true;
            options.aiStyles[0] = "control";
            options.decks[0] = {"meteor_shards", "ironclad",   "rambeast", "boulderback",
                                "archer_tower",  "nova_flask", "arc_mage", "vampire_bats"};
            Match m(options);
            for (int index = 0; index < 3; ++index)
                m.Spawn(Team::Enemy, "boulderback", {double(index) * .2, 10});
            m.SetAIEnabled(Team::Player, true);
            constexpr double tick = 1. / 60;
            m.Step(tick);
            Check(m.State().spellCasts.size() == 1, "visible clustered threat causes an AI Meteor cast");
            Check(m.State().hazards.empty(), "AI Meteor creates no zone before impact");
            const auto &born = m.State().telemetry[0].at("meteor_shards");
            Check(born.plays == 1, "one paid AI cast");
            Near(born.zoneSeconds, 0, 0, "no exposure before the hazard was born");
            Near(born.zoneOccupancy, 0, 0, "no target occupancy before the hazard was born");
            m.SetAIEnabled(Team::Player, false);
            m.Step(.75);
            Check(m.State().spellCasts.empty() && m.State().hazards.size() == 1, "AI Meteor lands after .75s");
            Near(m.State().hazards.front().born, m.State().elapsed, 1e-9,
                 "hazard born at the impact endpoint");
            Near(m.State().telemetry[0].at("meteor_shards").zoneSeconds, 0, 0,
                 "windup and impact tick have no zone exposure");
            m.Step(tick);
            const auto &active = m.State().telemetry[0].at("meteor_shards");
            Near(active.zoneSeconds, tick, 1e-9, "one tick of actual zone exposure");
            Check(active.zoneOccupancy > 0 && active.zoneOccupancy <= 3 * tick + 1e-9,
                  "occupancy uses the same clamped active interval");
            m.Step(5 - tick);
            const auto &finished = m.State().telemetry[0].at("meteor_shards");
            Near(finished.zoneSeconds, 5, 1e-8, "complete AI zone lasts exactly five seconds");
            Check(finished.dotTicks == 5 && m.State().hazards.empty(),
                  "inclusive five damage ticks and expiry are preserved");
        });
        Test("paid spell windups spend and cycle immediately but hit at exact deadlines", [] {
            auto options = Quiet();
            options.decks[0] = {"bullet_burst", "meteor_shards", "nova_flask", "ironclad",
                                "arc_mage", "archer_tower", "sky_manta", "rambeast"};
            Match m(options);
            m.SetAether(Team::Player, 10);
            Check(m.Spawn(Team::Enemy, "boulderback", {.5, 8.5}), "spell target");
            Check(m.Play(Team::Player, 0, {.5, 8.5}), "paid Bullet cast");
            Check(m.Play(Team::Player, 1, {.5, 8.5}), "paid Meteor cast");
            Near(m.State().spent[0], 7, 0, "full spell costs paid at cast");
            Near(m.State().aether[0], 3, 0, "both costs removed immediately");
            Check(m.State().hands[0][0] == "arc_mage" && m.State().hands[0][1] == "archer_tower",
                  "both hand slots cycle at cast");
            Check(m.State().spellCasts.size() == 2, "both windups are represented in state");
            Near(Unit(m, "boulderback", Team::Enemy).hp, 1620, 0, "no HP removed during initial windup");
            constexpr double tick = 1. / 60;
            m.Step(.30 - tick);
            Near(m.State().telemetry[0].at("bullet_burst").initialDamage, 0, 0,
                 "Bullet cannot hit before tick18");
            m.Step(tick);
            Near(m.State().telemetry[0].at("bullet_burst").initialDamage, 175, 0,
                 "Bullet hits on tick18");
            Check(m.State().spellCasts.size() == 1 && m.State().hazards.empty(),
                  "Meteor continues flying after Bullet lands");
            m.Step(.75 - .30 - tick);
            Near(m.State().telemetry[0].at("meteor_shards").initialDamage, 0, 0,
                 "Meteor cannot hit before tick45");
            m.Step(tick);
            Near(m.State().telemetry[0].at("meteor_shards").initialDamage, 262, 0,
                 "Meteor hits on tick45");
            Check(m.State().spellCasts.empty() && m.State().hazards.size() == 1,
                  "Meteor creates its zone only at impact");
            Near(m.State().hazards.front().born, .75, 1e-9, "zone born at impact");
            Near(m.State().hazards.front().nextTick, 1.75, 1e-9, "first DOT tick one second after impact");
            Near(m.State().hazards.front().expires, 5.75, 1e-9, "zone gets its full five seconds");
            std::array<int, 2> plays{}, casts{}, impacts{};
            std::array<PlayId, 2> ids{};
            for (const auto &event : m.Events()) {
                const int spell = event.cardId == "bullet_burst" ? 0 : event.cardId == "meteor_shards" ? 1 : -1;
                if (spell < 0)
                    continue;
                const double delay = spell == 0 ? .30 : .75;
                if (event.type == "card_play") {
                    ++plays[spell];
                    ids[spell] = event.playId;
                    Near(event.time, 0, 0, "paid card event at cast");
                    Near(event.until, delay, 0, "card event includes impact deadline");
                } else if (event.type == "spell_cast") {
                    ++casts[spell];
                    Check(event.playId == ids[spell], "windup keeps paid play identity");
                    Near(event.until, delay, 0, "windup event deadline");
                } else if (event.type == "spell_impact") {
                    ++impacts[spell];
                    Check(event.playId == ids[spell], "impact keeps paid play identity");
                    Near(event.time, delay, 1e-9, "impact event simulation time");
                } else if (event.type == "damage") {
                    Check(event.playId == ids[spell], "damage keeps paid play identity");
                    Near(event.time, delay, 1e-9, "initial damage occurs at impact");
                }
            }
            Check(plays == std::array<int, 2>{1, 1} && casts == plays && impacts == plays,
                  "one play, windup and impact event per spell");
            for (const auto *id : {"bullet_burst", "meteor_shards"}) {
                const auto &telemetry = m.State().telemetry[0].at(id);
                Check(telemetry.plays == 1 && telemetry.targets == 1 && telemetry.connected == 0 &&
                          telemetry.spellValue > 0,
                      "telemetry counts the paid cast and its delayed hit once");
            }
        });
        Test("moving enemies can dodge or enter each fixed spell area during windup", [] {
            for (const auto *id : {"bullet_burst", "meteor_shards"}) {
                const auto *card = FindCard(id);
                const auto *enteringId = card->id == "bullet_burst" ? "sky_manta" : "boulderback";
                const Vec2 dodgePoint{7.5, card->id == "bullet_burst" ? 3.5 : 1.5};
                const Vec2 enterPoint{7.5, card->id == "bullet_burst" ? 8.5 : 10.5};
                Match dodge(Quiet()), enter(Quiet());
                Check(dodge.Spawn(Team::Enemy, "boulderback", {7.5, 5.5}), "moving dodge target");
                Check(enter.Spawn(Team::Enemy, enteringId, {7.5, 5.5}), "moving enter target");
                const auto inside = [&](const Entity &entity, Vec2 point) {
                    return std::hypot(entity.position.x - point.x, entity.position.z - point.z) <=
                           card->spellRadius + entity.radius * .2;
                };
                Check(inside(Unit(dodge, "boulderback", Team::Enemy), dodgePoint), "dodger starts inside");
                Check(!inside(Unit(enter, enteringId, Team::Enemy), enterPoint), "entrant starts outside");
                Check(dodge.Spawn(Team::Player, id, dodgePoint) && enter.Spawn(Team::Player, id, enterPoint),
                      "fixed-area casts accepted");
                dodge.Step(card->castDelay);
                enter.Step(card->castDelay);
                Check(!inside(Unit(dodge, "boulderback", Team::Enemy), dodgePoint), "dodger leaves before impact");
                Check(inside(Unit(enter, enteringId, Team::Enemy), enterPoint), "entrant reaches area at impact");
                Near(dodge.State().telemetry[0].at(id).initialDamage, 0, 0, "spell misses departed target");
                Near(enter.State().telemetry[0].at(id).initialDamage, card->damage, 0, "spell hits arriving target");
                Check(dodge.State().telemetry[0].at(id).targets == 0 &&
                          enter.State().telemetry[0].at(id).targets == 1,
                      "targets are selected at impact rather than cast");
                int missImpacts = 0;
                for (const auto &event : dodge.Events())
                    missImpacts += event.type == "spell_impact" && event.cardId == id;
                Check(missImpacts == 1, "a missed spell still emits its impact animation event");
            }
        });
        Test("a delayed cast can hit enemies deployed after casting and never damages allies", [] {
            for (const auto *id : {"bullet_burst", "meteor_shards"}) {
                Match m(Quiet());
                const auto *card = FindCard(id);
                Check(m.Spawn(Team::Player, id, {.5, 8.5}), "cast into empty area");
                m.Step(card->castDelay / 2);
                Check(m.Spawn(Team::Enemy, "boulderback", {.5, 8.5}), "new enemy deployed during windup");
                Check(m.Spawn(Team::Player, "boulderback", {.5, 8.5}), "ally deployed during windup");
                m.Step(card->castDelay / 2);
                Near(m.State().telemetry[0].at(id).initialDamage, card->damage, 0,
                     "new enemy is found at impact");
                Near(Unit(m, "boulderback", Team::Player).hp, 1620, 0, "friendly spell immunity preserved");
            }
        });
        Test("pending spells pause and remain deterministic across frame chunks", [] {
            Match a(Quiet()), b(Quiet());
            for (auto *match : {&a, &b}) {
                match->Spawn(Team::Enemy, "boulderback", {.5, 8.5});
                match->Spawn(Team::Player, "meteor_shards", {.5, 8.5});
                match->Spawn(Team::Player, "bullet_burst", {.5, 8.5});
            }
            a.Step(.20);
            for (int n = 0; n < 24; ++n)
                b.Step(1. / 120);
            Same(a, b);
            const auto events = a.Events().size();
            const double elapsed = a.State().elapsed;
            for (int n = 0; n < 120; ++n)
                a.Step(0);
            Near(a.State().elapsed, elapsed, 0, "pause freezes cast clock");
            Check(a.State().spellCasts.size() == 2 && a.Events().size() == events,
                  "paused windups cannot resolve or emit hits");
            a.Step(.55);
            for (int n = 0; n < 66; ++n)
                b.Step(1. / 120);
            Same(a, b);
            Check(a.State().spellCasts.empty() && a.State().hazards.size() == 1,
                  "both clocks agree after exact impact");
            a.Step(5);
            for (int n = 0; n < 100; ++n)
                b.Step(.05);
            Same(a, b);
        });
        Test("field clear, result and tiebreaker cancel pending spells", [] {
            Match clear(Quiet());
            clear.Spawn(Team::Player, "meteor_shards", {.5, .5});
            clear.Spawn(Team::Player, "bullet_burst", {.5, .5});
            clear.ClearField();
            Check(clear.State().spellCasts.empty(), "DEV clear cancels both windups");
            clear.Step(1);
            for (const auto &event : clear.Events())
                Check(event.type != "spell_impact", "cleared casts cannot land later");
            Match finished(Quiet());
            finished.Spawn(Team::Player, "bullet_burst", {0, -arena::CoreDepth});
            finished.SetTowerHP(Tower(finished, Team::Enemy, EntityKind::Core).id, 0);
            Check(finished.State().phase == Phase::Finished && finished.State().spellCasts.empty(),
                  "result cancels pending cast");
            finished.Step(1);
            for (const auto &event : finished.Events())
                Check(event.type != "spell_impact", "no cast lands after a result");
            Match tie(Quiet());
            tie.Step(299.9);
            tie.Spawn(Team::Player, "meteor_shards", {.5, .5});
            tie.Step(.1);
            Check(tie.State().phase == Phase::Tiebreaker && tie.State().spellCasts.empty(),
                  "tiebreaker cancels unfinished windup");
            tie.Step(1);
            for (const auto &event : tie.Events())
                Check(event.type != "spell_impact", "no cast lands during tiebreaker");
            Match lethal(Quiet());
            lethal.SetTowerHP(Tower(lethal, Team::Enemy, EntityKind::Core).id, 55);
            lethal.Spawn(Team::Player, "bullet_burst", {0, -arena::CoreDepth});
            lethal.Spawn(Team::Enemy, "bullet_burst", {0, arena::CoreDepth});
            lethal.Step(.30);
            Check(lethal.State().phase == Phase::Finished && lethal.State().spellCasts.empty(),
                  "lethal impact safely clears another due cast");
            Near(Tower(lethal, Team::Player, EntityKind::Core).hp, 3600, 0,
                 "another simultaneous cast cannot damage after result");
        });
        Test("front rear sight and legal targeting", [] {
            Match m(Quiet());
            Entity s;
            s.team = Team::Player;
            s.id = 100;
            s.cardId = "ironclad";
            s.facing = {0, -1};
            Entity t;
            t.team = Team::Enemy;
            t.id = 101;
            t.radius = 0;
            t.position = {0, -7.9};
            Check(m.InSight(s, t), "front8");
            t.position = {0, 5.1};
            Check(!m.InSight(s, t), "rear5");
            t.position = {0, 4.9};
            Check(m.InSight(s, t), "rear visible");
            t.flying = true;
            Check(!m.CanTarget(s, t), "ground cannot hit air");
            s.cardId = "ember_archer";
            Check(m.CanTarget(s, t), "archer anti-air");
            s.cardId = "storm_raven";
            Check(!m.CanTarget(s, t), "Raven direct structure-only");
            s.cardId = "archer_tower";
            t.kind = EntityKind::Guard;
            t.flying = false;
            Check(!m.CanTarget(s, t), "defensive building troop-only");
        });
        Test("Crown hardlock survives defender before next hit", [] {
            Match m(Quiet());
            m.Spawn(Team::Player, "ironclad", {-8.5, -11.5});
            m.Step(.05);
            auto tower = Tower(m, Team::Enemy, EntityKind::Guard, -1).id;
            Check(Unit(m, "ironclad", Team::Player).hardLock == tower, "locks upon reaching attack range");
            m.Spawn(Team::Enemy, "ironclad", {-7.5, -11.5});
            m.Step(.05);
            Check(Unit(m, "ironclad", Team::Player).target == tower, "fresh defender cannot pull hardlock");
        });
        Test("travel target pull before Crown range", [] {
            Match m(Quiet());
            m.Spawn(Team::Player, "ironclad", {7.5, -5.5});
            m.Step(.02);
            Check(Unit(m, "ironclad", Team::Player).hardLock == 0, "travel no hardlock");
            m.Spawn(Team::Enemy, "ironclad", {7.5, -5.5});
            const auto &u = Unit(m, "ironclad", Team::Player);
            Check(u.forcedTarget != 0, "fresh defender pull");
            Check(u.target == Unit(m, "ironclad", Team::Enemy).id, "pulled target");
        });
        Test("structure-only building pull and hardlock", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "boulderback", {7.5, -6.5});
            m.Step(.02);
            m.Spawn(Team::Player, "archer_tower", {7.5, -4.5});
            m.Step(.03);
            const auto first = Unit(m, "archer_tower", Team::Player).id;
            Check(Unit(m, "boulderback", Team::Enemy).hardLock == first, "structure-only building lock");
            m.Spawn(Team::Player, "archer_tower", {8.5, -6.5});
            m.Step(.05);
            Check(Unit(m, "boulderback", Team::Enemy).target == first, "new building cannot pull lock");
        });
        Test("Frost nonstacking slow and honest uptime", [] {
            Match m(Quiet());
            m.Spawn(Team::Player, "frost_fang", {7.5, 5.5});
            m.Spawn(Team::Enemy, "boulderback", {7.5, 6.5});
            m.Step(.1);
            Near(Unit(m, "boulderback", Team::Enemy).slowPct, .3, 0, "30 percent");
            m.Step(1.5);
            const auto &t = m.State().telemetry[0].at("frost_fang");
            Check(t.slowTime > 0 && t.slowTrackedSeconds >= t.slowTime, "affected time and exposure");
            Near(Unit(m, "boulderback", Team::Enemy).slowPct, .3, 0, "refresh never stack");
        });
        Test("Raven delayed troop-only ring and stun", [] {
            Match m(Quiet());
            m.Spawn(Team::Player, "frost_fang", {9.5, 10.5});
            m.Spawn(Team::Enemy, "frost_fang", {9.5, 11.5});
            m.Spawn(Team::Enemy, "storm_raven", {7.5, 10.5});
            m.Step(2.9);
            Near(m.State().telemetry[1].at("storm_raven").auraDamage, 0, 0, "not before3s");
            m.Step(.2);
            const auto &t = m.State().telemetry[1].at("storm_raven");
            Near(t.auraDamage, 82, 1e-8, "ring82");
            Check(t.stunned >= 1, "stun applications");
            Check(t.stunTime > 0 && t.stunTrackedSeconds >= t.stunTime, "credited status exposure");
        });
        Test("Archer lifetime25s and absorption denominators", [] {
            Match m(Quiet());
            m.Spawn(Team::Player, "archer_tower", {7.5, 5.5});
            m.Step(24.9);
            Near(Unit(m, "archer_tower", Team::Player).hp, 3.4, .0001, "34HPpersecond");
            m.Step(.1);
            Check(Count(m, "archer_tower", Team::Player) == 0, "expires25");
            const auto &t = m.State().telemetry[0].at("archer_tower");
            Near(t.buildingCapacity, 25, 0, "capacity denominator");
            Near(t.buildingLifetime, 25, 1e-7, "actual lifetime");
        });
        Test("Affordable floating residue cannot produce a negative paid bank", [] {
            MatchOptions o;
            o.aiEnabled = {false, false};
            o.decks = {DefaultDeck(), DefaultDeck()};
            Match m(o);
            m.SetAether(Team::Player, 3.0 - 5e-10);
            Check(m.Play(Team::Player, 0, {0, 8.5}), "existing affordability epsilon accepts residue");
            Near(m.State().aether[0], 0, 0, "paid bank clamps numerical residue to zero");
            Near(m.State().spent[0], 3, 0, "canonical full cost is still paid");
            Match denied(o);
            denied.SetAether(Team::Player, 3.0 - 1e-5);
            Check(!denied.Play(Team::Player, 0, {0, 8.5}), "genuine insufficient Aether stays rejected");
            Near(denied.State().spent[0], 0, 0, "rejected card does not spend");
        });
        Test("Aether boundary integral exact 180/300 budget", [] {
            Near(Match::AetherGenerated(0, 180) + 5, 90.7142857142857, 1e-9, "regulation budget");
            Near(Match::AetherGenerated(0, 300) + 5, 197.857142857143, 1e-9, "full budget");
            Near(Match::AetherGenerated(119.9, 120.1), .3 / 2.8, 1e-9, "split1xto2x");
            Near(Match::AetherGenerated(239.9, 240.1), .5 / 2.8, 1e-9, "split2xto3x");
        });
        Test("only score0-0 enters overtime", [] {
            Match m(Quiet());
            m.Step(180);
            Check(m.State().phase == Phase::Overtime, "0-0 OT");
            Near(m.State().timeRemaining, 120, 1e-8, "OT120");
            m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Guard, 1).id, 0);
            Check(m.State().phase == Phase::Finished && m.State().winner == 0, "first OT tower wins");
            Match n(Quiet());
            n.SetTowerHP(Tower(n, Team::Enemy, EntityKind::Guard, -1).id, 0);
            n.SetTowerHP(Tower(n, Team::Player, EntityKind::Guard, 1).id, 0);
            n.Step(180);
            Check(n.State().phase == Phase::Finished && n.State().winner == -1, "nonzero tied draw no OT");
        });
        Test("tiebreaker freezes combat .85s then seeded one crown", [] {
            Match m(Quiet());
            m.SetTowerHP(Tower(m, Team::Player, EntityKind::Core).id, 10);
            m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Core).id, 10);
            m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Guard, 1).id, 2000);
            m.Step(300);
            Check(m.State().phase == Phase::Tiebreaker, "TB after300");
            const auto hand = m.State().hands[0];
            Check(!m.Play(Team::Player, 0, {0, 5}), "TB rejects cast");
            m.Step(.85);
            Near(Tower(m, Team::Player, EntityKind::Core).hp, 10, 1e-8, "delay no drain");
            m.Step(.1);
            Check(m.State().phase == Phase::Finished && m.State().winner == 0,
                  "initial min tie uses totalHP");
            Check(m.State().crowns[0] == 1 && m.State().crowns[1] == 0, "TB Core loss one crown");
            for (const auto &[id, t] : m.State().telemetry[0])
                Near(t.crownContribution, 0, 0, "TB uncredited");
            Check(hand == m.State().hands[0], "frozen hand");
        });
        Test("finished result rejects damage deployment and progress", [] {
            Match m(Quiet());
            m.SetTowerHP(Tower(m, Team::Enemy, EntityKind::Core).id, 0);
            const auto elapsed = m.State().elapsed;
            const auto events = m.Events().size();
            Check(!m.Spawn(Team::Player, "nova_flask", {0, 0}), "no postresult spawn");
            Check(!m.Play(Team::Player, 0, {0, 5}), "no postresult paid cast");
            m.Step(20);
            Near(m.State().elapsed, elapsed, 0, "finished clock frozen");
            Check(m.Events().size() == events, "single result");
        });
        Test("both bridge routes legal and ground progress", [] {
            Match m(Quiet());
            for (int lane : {-1, 1}) {
                m.Spawn(Team::Player, "boulderback", {lane * 7.5, 5.5});
                m.Spawn(Team::Enemy, "boulderback", {lane * 7.5, -5.5});
            }
            for (int n = 0; n < 600; ++n) {
                m.Step(1. / 60);
                for (const auto &e : m.State().entities)
                    if (!e.dead && e.kind == EntityKind::Troop && std::abs(e.position.z) < 1.93)
                        Check(std::abs(e.position.x - 7.2) < 1.94 || std::abs(e.position.x + 7.2) < 1.94,
                              "no illegal river walking");
            }
            bool crossed = false;
            for (const auto &e : m.State().entities)
                if (!e.dead && e.kind == EntityKind::Troop)
                    crossed |= e.team == Team::Player ? e.position.z < 0 : e.position.z > 0;
            Check(crossed, "large units cross bridge");
        });
        Test("expanded arena moves every tower back one tile and keeps full new edge placement", [] {
            Match m(Quiet());
            Check(arena::Width == 30 && arena::Height == 44, "new 30 by 44 physical board");
            for (Team team : {Team::Player, Team::Enemy}) {
                const double sign = team == Team::Player ? 1. : -1.;
                Near(Tower(m, team, EntityKind::Core).position.z, sign * 17.3, 0, "Core moves back exactly one tile");
                for (int lane : {-1, 1}) {
                    Near(Tower(m, team, EntityKind::Guard, lane).position.z, sign * 13.4, 0, "Guard moves back exactly one tile");
                    Near(Tower(m, team, EntityKind::Guard, lane).position.x, lane * 8.2, 0, "Guard lateral position preserved");
                }
                Check(m.CanPlace(team, *FindCard("ironclad"), {14.5, sign * 21.5}), "new outer corner accepts ordinary ground card");
                m.SetAether(team, 10);
                Check(m.Play(team, 0, {-14.5, sign * 21.5}), "new outer paid corner accepted");
                ClearOwnStructures(m, Unit(m, "ironclad", team), Unit(m, "ironclad", team).position);
                Check(m.Spawn(team, "sky_manta", {14.5, sign * 21.5}), "new outer DEV corner accepted");
            }
            Near(SnapToTile({15, 22}).x, 14.5, 0, "positive X tile reaches widened edge");
            Near(SnapToTile({15, 22}).z, 21.5, 0, "positive Z tile reaches deeper edge");
            Near(SnapToTile({-15, -22}).x, -14.5, 0, "negative X tile reaches widened edge");
            Near(SnapToTile({-15, -22}).z, -21.5, 0, "negative Z tile reaches deeper edge");
            Check(!m.Spawn(Team::Player, "ironclad", {15.001, 10}), "outside new width rejected");
            Check(!m.Spawn(Team::Enemy, "ironclad", {0, -22.001}), "outside new depth rejected");
            Near(arena::PocketOuterX, 14.2, 1e-12, "pocket widens with board");
            Near(arena::PocketMaxDepth, 10.25, 1e-12, "pocket retreats with Guard");
        });
        Test("all ground cards retain their bridge side and advance to Core through a destroyed lane", [] {
            int fixtures = 0, members = 0;
            for (const auto &card : Cards()) {
                if (card.flying || card.spell || card.building)
                    continue;
                auto options = Quiet();
                auto deck = DefaultDeck();
                deck.erase(std::remove(deck.begin(), deck.end(), card.id), deck.end());
                deck.insert(deck.begin(), card.id);
                deck.resize(8);
                options.decks = {deck, deck};
                for (Team team : {Team::Player, Team::Enemy})
                    for (int lane : {-1, 1})
                        for (int mode = 0; mode < 3; ++mode) {
                            Match m(options);
                            const double sign = team == Team::Player ? 1. : -1.;
                            const Team other = team == Team::Player ? Team::Enemy : Team::Player;
                            const auto sameGuard = Tower(m, other, EntityKind::Guard, lane).id;
                            const auto oppositeGuard = Tower(m, other, EntityKind::Guard, -lane).id;
                            const auto core = Tower(m, other, EntityKind::Core).id;
                            if (mode)
                                Check(m.SetTowerHP(sameGuard, 0), "selected enemy lane Guard destroyed");
                            const EntityId expected = mode ? core : sameGuard;
                            const Vec2 drop{lane * 7.5, sign * (mode == 2 ? -5.5 : 5.5)};
                            m.SetAether(team, 10);
                            Check(m.Play(team, 0, drop), "ordinary own-lane or unlocked-pocket deployment succeeds");
                            std::vector<EntityId> ids;
                            for (const auto &e : m.State().entities)
                                if (e.kind == EntityKind::Troop)
                                    ids.push_back(e.id);
                            std::vector<bool> crossed(ids.size(), false);
                            m.Step(1. / 60);
                            for (EntityId id : ids)
                                Check(ById(m, id).target == expected, "default advance targets same Guard or exposed Core");
                            const auto present = [&](EntityId id) -> const Entity * {
                                for (const auto &e : m.State().entities)
                                    if (e.id == id)
                                        return &e;
                                return nullptr;
                            };
                            for (int n = 0; n < 480; ++n) {
                                std::vector<Vec2> prior(ids.size());
                                for (std::size_t i = 0; i < ids.size(); ++i)
                                    if (const auto *e = present(ids[i]))
                                        prior[i] = e->position;
                                m.Step(1. / 60);
                                for (std::size_t i = 0; i < ids.size(); ++i) {
                                    const auto *current = present(ids[i]);
                                    if (!current)
                                        continue; // Normal deaths can retire a pocket attacker after its advance.
                                    const auto &e = *current;
                                    ClearOwnStructures(m, e, prior[i]);
                                    Check(e.target != oppositeGuard, "opposite surviving Guard cannot redirect this lane");
                                    if (std::abs(e.position.z) < arena::RiverHalfWidth + .28) {
                                        const double half = std::max(.34, arena::BridgeWidth / 2 - .16 - e.radius * .92);
                                        Check(std::abs(e.position.x - lane * arena::BridgeCenterX) <= half + 1e-8,
                                              "actual river traversal uses only the deployment side bridge");
                                        Check(e.bridge == lane, "bridge commitment stays stable while crossing");
                                    }
                                    crossed[i] = crossed[i] || e.position.z * sign < -arena::RiverHalfWidth;
                                }
                            }
                            for (bool reached : crossed)
                                Check(reached, "each ground member makes progress onto the enemy bank");
                            Check(!ById(m, oppositeGuard).dead, "opposite Guard remains a real standing alternative");
                            fixtures++;
                            members += static_cast<int>(ids.size());
                        }
            }
            Check(fixtures == 84 && members == 96, "all seven ground cards, both teams, both lane states and pockets");
        });
        Test("continuous routes to an opposite target still use the current side bridge", [] {
            int routes = 0;
            for (const auto &card : Cards()) {
                if (card.flying || card.spell || card.building)
                    continue;
                for (Team team : {Team::Player, Team::Enemy})
                    for (int lane : {-1, 1}) {
                        Match m(Quiet());
                        Entity source;
                        source.id = 9000;
                        source.team = team;
                        source.cardId = card.id;
                        source.radius = .44 * card.scale;
                        source.lane = lane;
                        source.position = {lane * 7.5, team == Team::Player ? 5.5 : -5.5};
                        const auto &target = Tower(m, team == Team::Player ? Team::Enemy : Team::Player,
                                                   EntityKind::Guard, -lane);
                        const auto path = m.FindPath(source, target);
                        Check(!path.empty(), "opposite target has an obstacle-aware route");
                        bool river = false;
                        Vec2 prior = source.position;
                        for (Vec2 point : path) {
                            if (std::abs(point.z) < arena::RiverHalfWidth + .28) {
                                Check(point.x * lane > 0, "default path crosses the bridge on the source side");
                                river = true;
                            }
                            if (prior.z * point.z <= 0 && std::abs(point.z - prior.z) > 1e-9) {
                                const double along = -prior.z / (point.z - prior.z);
                                const double x = prior.x + (point.x - prior.x) * along;
                                Check(x * lane > 0, "compressed segment crosses only the current-side bridge");
                                river = true;
                            }
                            prior = point;
                        }
                        Check(river, "route includes the actual river crossing");
                        routes++;
                    }
            }
            Check(routes == 28, "all ground cards mirror the current-side route policy");
        });
        Test("destroyed-lane Core advances stay deterministic across frame chunks", [] {
            auto options = Quiet();
            options.seed = 20261011;
            Match a(options), b(options);
            for (Match *m : {&a, &b})
                for (Team team : {Team::Player, Team::Enemy}) {
                    const double sign = team == Team::Player ? 1. : -1.;
                    const Team other = team == Team::Player ? Team::Enemy : Team::Player;
                    m->SetTowerHP(Tower(*m, other, EntityKind::Guard, -1).id, 0);
                    m->SetAether(team, 10);
                    Check(m->Play(team, 0, {-7.5, sign * 5.5}), "paid exposed-lane advance");
                    Check(m->Spawn(team, "twin_blades", {-7.5, -sign * 5.5}), "pocket swarm advance");
                }
            a.Step(8);
            for (int n = 0; n < 960; ++n)
                b.Step(1. / 120);
            Same(a, b);
        });
        Test("paid ground members clear both teams Core and Guards without changing costs or events", [] {
            int fixtures = 0, members = 0;
            for (const auto &card : Cards()) {
                if (card.flying || card.spell || card.building)
                    continue;
                auto options = Quiet();
                auto deck = DefaultDeck();
                deck.erase(std::remove(deck.begin(), deck.end(), card.id), deck.end());
                deck.insert(deck.begin(), card.id);
                deck.resize(8);
                options.decks = {deck, deck};
                for (Team team : {Team::Player, Team::Enemy})
                    for (int lane : {-1, 0, 1})
                        for (double behind : {0., 1.2}) {
                            Match m(options);
                            const Entity tower = Tower(m, team, lane ? EntityKind::Guard : EntityKind::Core,
                                                       lane);
                            const double sign = team == Team::Player ? 1. : -1.;
                            const Vec2 drop{tower.position.x, tower.position.z + sign * behind};
                            m.SetAether(team, 10);
                            Check(m.CanPlace(team, card, SnapToTile(drop)), "original hand drop remains legal");
                            Check(m.Play(team, 0, drop), "paid tower-adjacent drop accepted");
                            const int side = team == Team::Player ? 0 : 1;
                            Near(m.State().aether[side], 10. - card.cost, 0, "one canonical cost paid");
                            Near(m.State().spent[side], card.cost, 0, "one paid cast recorded");
                            Check(m.State().hands[side][0] == deck[4], "hand cycles exactly once");
                            Check(m.State().telemetry[side].at(card.id).spawns ==
                                      static_cast<std::uint64_t>(card.count),
                                  "all ground swarm members preserved");
                            std::vector<Entity> spawned;
                            for (const auto &e : m.State().entities)
                                if (e.kind == EntityKind::Troop && e.cardId == card.id) {
                                    spawned.push_back(e);
                                    ClearOwnStructures(m, e, e.position);
                                    Check(m.CanPlace(team, card, e.position), "resolved paid member stays in legal zone");
                                    bool actualEvent = false;
                                    for (const auto &event : m.Events())
                                        if (event.type == "entity_spawn" && event.source == e.id) {
                                            Near(event.position.x, e.position.x, 0, "spawn event records actual X");
                                            Near(event.position.z, e.position.z, 0, "spawn event records actual Z");
                                            Check(event.playId == e.playId && !event.sandbox,
                                                  "spawn retains paid play identity");
                                            actualEvent = true;
                                        }
                                    Check(actualEvent, "resolved member emits its normal spawn event");
                                }
                            Check(spawned.size() == static_cast<std::size_t>(card.count), "full member count");
                            for (int n = 0; n < 240; ++n) {
                                std::vector<Vec2> before;
                                for (const auto &e : spawned)
                                    before.push_back(ById(m, e.id).position);
                                m.Step(1. / 60);
                                for (std::size_t i = 0; i < spawned.size(); ++i)
                                    ClearOwnStructures(m, ById(m, spawned[i].id), before[i]);
                            }
                            for (const auto &e : spawned) {
                                const auto &after = ById(m, e.id);
                                Check(std::hypot(after.position.x - e.position.x,
                                                 after.position.z - e.position.z) > 1.,
                                      "every tower-adjacent member makes real progress");
                                ++members;
                            }
                            ++fixtures;
                        }
            }
            Check(fixtures == 84 && members == 96, "all seven ground cards and Twin Blades matrix");
        });
        Test("continuous tower perimeter paths clear corners and blocked rounded start cells", [] {
            int paths = 0;
            for (Team team : {Team::Player, Team::Enemy})
                for (const auto *id : {"ironclad", "boulderback"})
                    for (int lane : {-1, 0, 1}) {
                        Match m(Quiet());
                        const auto tower = Tower(m, team, lane ? EntityKind::Guard : EntityKind::Core, lane);
                        const Team other = team == Team::Player ? Team::Enemy : Team::Player;
                        const auto target = Tower(m, other, EntityKind::Guard, lane ? lane : 1);
                        for (int n = 0; n < 64; ++n) {
                            Entity source;
                            source.id = 9000;
                            source.cardId = id;
                            source.team = team;
                            source.radius = .44 * FindCard(id)->scale;
                            const double angle = n * 6.28318530717958647692 / 64.;
                            const double clearance = tower.radius + source.radius + .221;
                            source.position = {tower.position.x + std::cos(angle) * clearance,
                                               tower.position.z + std::sin(angle) * clearance};
                            const auto path = m.FindPath(source, target, target.lane);
                            Check(!path.empty(), "legal continuous perimeter has a route");
                            Vec2 prior = source.position;
                            for (Vec2 point : path) {
                                source.position = point;
                                ClearOwnStructures(m, source, prior);
                                prior = point;
                            }
                            ++paths;
                        }
                    }
            Check(paths == 768, "both teams and both body sizes around all crown footprints");
        });
        Test("paid rear rows and corner drops resolve each ground member before its first tick", [] {
            int fixtures = 0, members = 0;
            const std::vector<Vec2> points{{.5, arena::LastTileZ}, {-13.5, arena::LastTileZ},
                                           {13.5, arena::LastTileZ}, {-arena::LastTileX, arena::LastTileZ},
                                           {arena::LastTileX, arena::LastTileZ}, {-13.5, 8.5}, {13.5, 8.5}};
            for (const auto &card : Cards()) {
                if (card.flying || card.spell || card.building)
                    continue;
                auto options = Quiet();
                auto deck = DefaultDeck();
                deck.erase(std::remove(deck.begin(), deck.end(), card.id), deck.end());
                deck.insert(deck.begin(), card.id);
                deck.resize(8);
                options.decks = {deck, deck};
                for (Team team : {Team::Player, Team::Enemy})
                    for (Vec2 point : points) {
                        const double sign = team == Team::Player ? 1. : -1.;
                        point.z *= sign;
                        Match m(options);
                        m.SetAether(team, 10);
                        Check(m.CanPlace(team, card, point), "original paid rear/side/corner drop stays legal");
                        Check(m.Play(team, 0, point), "paid edge drop succeeds");
                        const int side = team == Team::Player ? 0 : 1;
                        Near(m.State().aether[side], 10. - card.cost, 0, "edge drop pays once");
                        Check(m.State().hands[side][0] == deck[4], "edge drop cycles once");
                        std::vector<Entity> spawned;
                        for (const auto &e : m.State().entities)
                            if (e.kind == EntityKind::Troop) {
                                ClearOwnStructures(m, e, e.position);
                                Check(m.CanPlace(team, card, e.position), "edge member keeps its legal paid zone");
                                spawned.push_back(e);
                                bool actualEvent = false;
                                for (const auto &event : m.Events())
                                    if (event.type == "entity_spawn" && event.source == e.id) {
                                        Near(event.position.x, e.position.x, 0, "edge spawn records actual X");
                                        Near(event.position.z, e.position.z, 0, "edge spawn records actual Z");
                                        actualEvent = true;
                                    }
                                Check(actualEvent, "edge correction is visible in the authoritative spawn event");
                            }
                        Check(spawned.size() == static_cast<std::size_t>(card.count), "edge preserves member count");
                        for (int n = 0; n < 300; ++n) {
                            std::vector<Vec2> prior;
                            for (const auto &e : spawned)
                                prior.push_back(ById(m, e.id).position);
                            m.Step(1. / 60);
                            for (std::size_t i = 0; i < spawned.size(); ++i)
                                ClearOwnStructures(m, ById(m, spawned[i].id), prior[i]);
                        }
                        for (const auto &e : spawned) {
                            const auto &after = ById(m, e.id);
                            Check((e.position.z - after.position.z) * sign > 1.,
                                  "every edge member makes inward progress without a first-tick recovery snap");
                            ++members;
                        }
                        ++fixtures;
                    }
            }
            Check(fixtures == 98 && members == 112, "all ground cards mirrored across rear/side/corner cases");
        });
        Test("legal ground drops air spells buildings and DEV river fixtures retain their positions", [] {
            for (Team team : {Team::Player, Team::Enemy}) {
                const double sign = team == Team::Player ? 1. : -1.;
                Match m(Quiet());
                Check(m.Spawn(team, "ironclad", {7.5, sign * 5.5}), "legal DEV ground drop");
                Near(Unit(m, "ironclad", team).position.x, 7.5, 0, "legal ground X unchanged");
                Near(Unit(m, "ironclad", team).position.z, sign * 5.5, 0, "legal ground Z unchanged");
                Check(m.Spawn(team, "sky_manta", {.5, sign * 16.5}), "flying tower-center fixture");
                Near(Unit(m, "sky_manta", team).position.x, .5, 0, "air X unchanged");
                Near(Unit(m, "sky_manta", team).position.z, sign * 16.5, 0, "air can still fly over towers");
                Check(m.Spawn(team, "archer_tower", {.5, sign * 8.5}), "existing legal DEV building fixture");
                Near(Unit(m, "archer_tower", team).position.x, .5, 0, "building X unchanged");
                Near(Unit(m, "archer_tower", team).position.z, sign * 8.5, 0, "legal fixed building position unchanged");
                Check(m.Spawn(team, "meteor_shards", {.5, sign * 16.5}), "existing fixed spell fixture");
                Near(m.State().spellCasts.back().position.x, .5, 0, "spell area X unchanged");
                Near(m.State().spellCasts.back().position.z, sign * 16.5, 0, "spell area Z unchanged");
                Match river(Quiet());
                Check(river.Spawn(team, "ironclad", {.5, .5}), "DEV river fixture still accepted");
                Near(Unit(river, "ironclad", team).position.x, .5, 0, "DEV river X unchanged at spawn");
                Near(Unit(river, "ironclad", team).position.z, .5, 0, "DEV river Z unchanged at spawn");
            }
        });
        Test("a building placed over an existing ground troop cannot leave it trapped", [] {
            for (Team team : {Team::Player, Team::Enemy}) {
                const double sign = team == Team::Player ? 1. : -1.;
                Match m(Quiet());
                Check(m.Spawn(team, "ironclad", {.5, sign * 8.5}), "ground recovery fixture");
                const auto troop = Unit(m, "ironclad", team).id;
                Check(m.Spawn(team, "archer_tower", {.5, sign * 8.5}), "new solid footprint over troop");
                m.Step(1. / 60);
                const Vec2 recovered = ById(m, troop).position;
                ClearOwnStructures(m, ById(m, troop), recovered);
                for (int n = 0; n < 120; ++n) {
                    const Vec2 prior = ById(m, troop).position;
                    m.Step(1. / 60);
                    ClearOwnStructures(m, ById(m, troop), prior);
                }
                const auto &after = ById(m, troop);
                Check(std::hypot(after.position.x - recovered.x, after.position.z - recovered.z) > 1.,
                      "recovered troop resumes routed movement");
            }
        });
        Test("tower-adjacent placement recovery remains fixed-step deterministic", [] {
            auto options = Quiet();
            options.seed = 20261010;
            Match a(options), b(options);
            for (Match *m : {&a, &b})
                for (Team team : {Team::Player, Team::Enemy}) {
                    const double sign = team == Team::Player ? 1. : -1.;
                    m->SetAether(team, 10);
                    Check(m->Play(team, 0, {.5, sign * 17.5}), "paid original Core repro");
                    Check(m->Spawn(team, "twin_blades", {8.5, sign * 13.5}), "Guard swarm repro");
                    Check(m->Spawn(team, "boulderback", {-.5, sign * 16.5}), "large Core repro");
                }
            a.Step(4);
            for (int n = 0; n < 480; ++n)
                b.Step(1. / 120);
            Same(a, b);
        });
        Test("pause zero and frame independent fixed-step determinism", [] {
            auto o = Quiet();
            o.seed = 42;
            Match a(o), b(o);
            a.Spawn(Team::Player, "rambeast", {7.5, 10.5});
            b.Spawn(Team::Player, "rambeast", {7.5, 10.5});
            a.Spawn(Team::Enemy, "frost_fang", {7.5, -10.5});
            b.Spawn(Team::Enemy, "frost_fang", {7.5, -10.5});
            a.Step(12);
            for (int n = 0; n < 1440; ++n)
                b.Step(1. / 120);
            Same(a, b);
            const auto time = a.State().elapsed;
            a.Step(0);
            Near(a.State().elapsed, time, 0, "pause");
        });
        Test("all seven coherent AI decks", [] {
            for (const std::string &s :
                 {"beatdown", "aggro", "control", "cycle", "split", "spell_cycle", "counter"})
                for (unsigned n = 0; n < 8; ++n) {
                    auto d = BuildAIDeck(s, 100 + n);
                    Check(ValidateDeck(d), "AI valid eight");
                    auto a = AnalyzeDeck(d);
                    Check(a.winConditions >= 1 && a.spells >= 1 && a.sustainedAntiAir >= 2 &&
                              a.antiAir >= 3 && a.cheap >= 2,
                          "baseline coherent");
                    if (s == "cycle")
                        Check(a.averageCost <= 3.375 && a.cheap >= 4, "Cycle ceiling");
                    if (s == "spell_cycle")
                        Check(a.spells >= 2 && a.buildings >= 1 && a.cheap >= 3, "spell cycle");
                    if (s == "control")
                        Check(a.buildings >= 1 && a.defenseScore >= 65, "control defense");
                }
        });
        Test("mechanical counters and symmetric91 pair analysis", [] {
            const auto *iron = FindCard("ironclad"), *bat = FindCard("vampire_bats"),
                       *raven = FindCard("storm_raven"), *arch = FindCard("ember_archer");
            Near(CounterScore(*iron, *bat), 0, 0, "ground does not counter flying");
            Check(CounterScore(*arch, *bat) > 0, "archer valid air");
            Check(CounterScore(*raven, *bat) > 0 && CounterScore(*raven, *bat) < CounterScore(*arch, *bat),
                  "aura not direct DPS");
            int pairs = 0;
            for (std::size_t a = 0; a < Cards().size(); ++a)
                for (std::size_t b = a + 1; b < Cards().size(); ++b) {
                    ++pairs;
                    Near(PairSynergy(Cards()[a], Cards()[b]), PairSynergy(Cards()[b], Cards()[a]), 0,
                         "symmetric");
                }
            Check(pairs == 91, "all pairs");
            auto d = AnalyzeDeck({"vampire_bats", "twin_blades", "nova_flask"});
            Near(d.averageHP, (174 * 5 + 262 * 2) / 2., 0, "swarm aggregate spell excluded");
            Near(d.deploymentDPS, (86 / 1.05 * 5 + 58 / .72 * 2) / 2., .0051, "aggregate DPS spell excluded");
        });
        Test("AI symmetric seeded decisions and paid economy", [] {
            MatchOptions o;
            o.seed = 123;
            o.aiEnabled = {true, true};
            o.aiStyles = {"aggro", "control"};
            Match a(o), b(o);
            a.Step(15);
            for (int n = 0; n < 300; ++n)
                b.Step(.05);
            Same(a, b);
            Check(a.State().spent[0] > 0 && a.State().spent[1] > 0, "both AI sides actually deploy");
            for (int side = 0; side < 2; ++side) {
                Near(a.State().spent[side] + a.State().aether[side] + a.State().leaked[side],
                     5 + Match::AetherGenerated(0, a.State().elapsed), 1e-6, "honest bank/spend/leak");
            }
            a.SetAIEnabled(Team::Enemy, false);
            Check(!a.State().ai[1].enabled, "AI toggle");
            Check(a.SetAIStyle(Team::Enemy, "split"), "style change");
            Check(a.State().ai[1].style == "split" && ValidateDeck(a.State().decks[1]), "style deck rebuild");
        });
        Test("all card intelligence roles answers partners and honest spell resistance", [] {
            for (const auto &card : Cards()) {
                const auto intel = IntelligenceFor(card.id);
                Check(intel.id == card.id && !intel.role.empty() && !intel.suggestedUses.empty(),
                      "full card intelligence");
                Check(!intel.partners.empty() && !intel.bestOffensivePartner.id.empty(), "offensive partner");
                for (const auto &answer : intel.bestAgainst)
                    Check(!answer.reason.empty() && answer.score >= 0 && answer.score <= 100,
                          "explained mechanical answers");
            }
            const auto intel = IntelligenceFor("meteor_shards");
            Check(!intel.weakAgainst.empty(), "full stationary effect resistance");
            Check(FindCard(intel.weakAgainst.front().id)->hp > 462, "resistance survives full462");
            Check(CounterReason(*FindCard("ironclad"), *FindCard("vampire_bats")) ==
                      "Cannot damage this flying target.",
                  "air restriction reason");
            Check(!PairSynergyReasons(*FindCard("frost_fang"), *FindCard("meteor_shards")).empty(),
                  "slow DOT synergy reasons");
        });
        Test("event attribution sequence and drain once", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "ironclad", {0, 0});
            m.Spawn(Team::Player, "nova_flask", {0, 0});
            std::uint64_t previous = 0;
            bool damage = false;
            for (const auto &e : m.Events()) {
                Check(e.sequence > previous, "ordered sequence");
                previous = e.sequence;
                if (e.type == "damage") {
                    damage = true;
                    Check(e.playId > 0 && e.amount == 375 && e.requested == 375 && e.hp == 465,
                          "actual attributed hit");
                }
            }
            Check(damage, "damage exists");
            const auto drained = m.DrainEvents();
            Check(!drained.empty() && m.Events().empty(), "drain once");
            m.ClearField();
            Check(Count(m, "ironclad", Team::Enemy) == 0, "clear entities");
            Check(m.State().entities.size() == 6, "preserve Towers");
        });
        Test("AI never reads hidden opponent deck hand or bank", [] {
            auto aOptions = Quiet(), bOptions = Quiet();
            aOptions.seed = bOptions.seed = 519;
            aOptions.aiEnabled = bOptions.aiEnabled = {true, false};
            aOptions.aiStyles = bOptions.aiStyles = {"aggro", "control"};
            aOptions.decks[0] = bOptions.decks[0] = BuildAIDeck("aggro", 519);
            bOptions.decks[1] = {"meteor_shards", "vampire_bats", "twin_blades", "storm_raven",
                                 "frost_fang",    "bullet_burst", "arc_mage",    "nova_flask"};
            Match a(aOptions), b(bOptions);
            b.SetAether(Team::Enemy, 0);
            a.DrainEvents();
            b.DrainEvents();
            a.Step(30);
            b.Step(30);
            Check(a.State().hands[0] == b.State().hands[0], "own play rotation independent of hidden state");
            Near(a.State().spent[0], b.State().spent[0], 0, "same own choices");
            Check(a.State().randomState == b.State().randomState, "same AI RNG consumption");
            std::vector<std::string> x, y;
            for (const auto &e : a.Events())
                if (e.type == "ai_decision")
                    x.push_back(e.reason);
            for (const auto &e : b.Events())
                if (e.type == "ai_decision")
                    y.push_back(e.reason);
            Check(x == y, "identical public-input decisions");
        });
        Test("charged Ram hit and actual projectile splash attribution", [] {
            Match ram(Quiet());
            ram.Spawn(Team::Player, "rambeast", {7.5, -5.5});
            ram.Step(5);
            bool charged = false;
            for (const auto &e : ram.Events())
                if (e.type == "damage" && e.cardId == "rambeast" && e.targetKind == EntityKind::Guard &&
                    e.requested == 255)
                    charged = true;
            Check(charged, "running approach charged255");
            Match mage(Quiet());
            mage.Spawn(Team::Player, "arc_mage", {7.5, 5.5});
            mage.Spawn(Team::Enemy, "vampire_bats", {7.5, 6.5});
            mage.Step(.7);
            int splash = 0;
            for (const auto &e : mage.Events())
                if (e.type == "damage" && e.cardId == "arc_mage") {
                    Check(e.playId > 0, "projectile retains cast");
                    splash += e.damageKind == "splash";
                }
            Check(splash >= 4, "primary plus four splash victims");
        });
        Test("mixed specials and bridge congestion stress", [] {
            Match m(Quiet());
            for (Team team : {Team::Player, Team::Enemy})
                for (int lane : {-1, 1})
                    for (int n = 0; n < 4; ++n) {
                        const double z = (team == Team::Player ? 1 : -1) * (4.5 + n * .5);
                        m.Spawn(team, "boulderback", {lane * 7.5, z});
                        m.Spawn(team, "vampire_bats", {lane * 7.5, z});
                        if (n == 0) {
                            m.Spawn(team, "frost_fang", {lane * 7.5, z + 1});
                            m.Spawn(team, "storm_raven", {lane * 7.5, z + 2});
                            m.Spawn(team, "archer_tower", {lane * 7.5, z + 3});
                        }
                    }
            m.Spawn(Team::Player, "meteor_shards", {7.5, -4.5});
            m.Spawn(Team::Enemy, "meteor_shards", {-7.5, 4.5});
            for (int n = 0; n < 240; ++n) {
                m.Step(.25);
                for (const auto &e : m.State().entities) {
                    Check(std::isfinite(e.hp) && std::isfinite(e.position.x) && std::isfinite(e.position.z),
                          "stress finite entities");
                    if (!e.dead && e.kind == EntityKind::Troop && !e.flying && std::abs(e.position.z) < 1.93)
                        Check(std::abs(e.position.x - 7.2) < 1.94 || std::abs(e.position.x + 7.2) < 1.94,
                              "stress inside bridge");
                }
                if (m.State().phase == Phase::Finished)
                    break;
            }
            for (const auto &team : m.State().telemetry)
                for (const auto &[id, t] : team) {
                    Check(t.slowTime <= t.slowTrackedSeconds + 1e-7 &&
                              t.stunTime <= t.stunTrackedSeconds + 1e-7,
                          "status honest denominators");
                    Check(std::isfinite(t.towerDamage) && std::isfinite(t.zoneOccupancy),
                          "finite attribution");
                }
        });
        Test("placement means count paid casts rather than swarm members", [] {
            auto options = Quiet();
            options.decks[0] = {"vampire_bats", "meteor_shards", "ironclad",   "arc_mage",
                                "frost_fang",   "archer_tower",  "nova_flask", "rambeast"};
            Match m(options);
            Check(m.Spawn(Team::Player, "vampire_bats", {-9, 8}), "DEV swarm");
            m.SetAether(Team::Player, 10);
            Check(m.Play(Team::Player, 0, {3.5, 5.5}), "paid swarm");
            Check(m.Play(Team::Player, 1, {-3.5, -5.5}), "paid spell");
            const auto &bats = m.State().telemetry[0].at("vampire_bats");
            Check(bats.plays == 1 && bats.spawns == 10, "only paid cast enters placement denominator");
            Near(bats.placementX, 3.5, 0, "swarm placement once");
            Near(bats.placementZ, 5.5, 0, "DEV placement excluded");
            const auto &meteor = m.State().telemetry[0].at("meteor_shards");
            Near(meteor.placementX, -3.5, 0, "spell placement recorded");
            Near(meteor.placementZ, -5.5, 0, "spell placement recorded");
        });
        Test("every physical paid card separates same-tile members and existing bodies on both teams", [] {
            const int fixtureStart = collisionPaidFixtures, playStart = collisionPaidPlays, memberStart = collisionMembers;
            for (const auto &card : Cards()) if (!card.spell)
                for (const Team team : {Team::Player, Team::Enemy}) {
                    const std::string blocker = card.flying ? (card.id == "sky_manta" ? "vampire_bats" : "sky_manta")
                                                          : (card.id == "ironclad" ? "boulderback" : "ironclad");
                    Match m(CollisionOptions(card.id, blocker));
                    const Vec2 drop{7.5, (team == Team::Player ? 1 : -1) * 8.5};
                    const auto first = PaidCollisionCard(m, team, card.id, drop);
                    const auto second = PaidCollisionCard(m, team, blocker, drop);
                    ++collisionPaidFixtures;
                    BodyClearance(m, "all-card same-tile spawn " + card.id);
                    for (const auto &e : second)
                        Check(m.CanPlace(team, *FindCard(e.cardId), e.position), "resolved member stays in actual legal deployment zone");
                    for (int step = 0; step < 120; ++step) {
                        const auto before = m.State().entities;
                        m.Step(1. / 60.);
                        SweptBodyClearance(m, before, "all-card movement " + card.id);
                    }
                    for (const auto &start : first)
                        if (start.kind == EntityKind::Troop) {
                            const auto &after = ById(m, start.id);
                            Check(!after.dead && std::hypot(after.position.x - start.position.x,
                                                          after.position.z - start.position.z) > .5,
                                  "body collision permits real movement for " + card.id);
                        } else {
                            const auto &after = ById(m, start.id);
                            Near(std::hypot(after.position.x - start.position.x, after.position.z - start.position.z),
                                 0, 0, "building stays fixed while nearby troops route around it");
                        }
                }
            Check(collisionPaidFixtures - fixtureStart == 22 && collisionPaidPlays - playStart == 44 &&
                      collisionMembers - memberStart == 62,
                  "all eleven physical cards, both teams, forty-four paid casts and sixty-two actual members");
        });
        Test("ground and air occupy distinct collision layers while spells have no body", [] {
            for (const Team team : {Team::Player, Team::Enemy}) {
                Match m(CollisionOptions("ironclad", "sky_manta"));
                const Vec2 drop{.5, (team == Team::Player ? 1 : -1) * 8.5};
                const auto ground = PaidCollisionCard(m, team, "ironclad", drop);
                const auto air = PaidCollisionCard(m, team, "sky_manta", drop);
                ++collisionPaidFixtures;
                Near(std::hypot(ground[0].position.x - air[0].position.x,
                                ground[0].position.z - air[0].position.z), 0, 1e-9,
                     "flying troop can pass above a ground body without fake lateral displacement");
                BodyClearance(m, "distinct collision layers");
                for (const auto &spell : Cards()) if (spell.spell) {
                    Match cast(CollisionOptions(spell.id, "ironclad"));
                    const auto troop = PaidCollisionCard(cast, team, "ironclad", drop);
                    const auto entities = cast.State().entities.size();
                    PaidCollisionCard(cast, team, spell.id, drop);
                    ++collisionPaidFixtures;
                    Check(cast.State().entities.size() == entities, "spell never creates an invisible collision body");
                    Near(std::hypot(ById(cast, troop[0].id).position.x - troop[0].position.x,
                                    ById(cast, troop[0].id).position.z - troop[0].position.z),
                         0, 0, "spell cast never displaces friendly body");
                    cast.Step(.8);
                    Near(ById(cast, troop[0].id).hp, FindCard("ironclad")->hp, 0,
                         "spell collision policy retains no friendly damage");
                }
            }
        });
        Test("opposing paid melee swarms make contact without overlap or crossing through one another", [] {
            for (const int lane : {-1, 1}) {
                Match m(CollisionOptions("twin_blades", "ironclad"));
                for (const Team team : {Team::Player, Team::Enemy})
                    PaidCollisionCard(m, team, "twin_blades", {lane * 7.5, (team == Team::Player ? 1 : -1) * 2.5});
                ++collisionPaidFixtures;
                BodyClearance(m, "opposing fast twin spawn");
                for (int step = 0; step < 480; ++step) {
                    const auto before = m.State().entities;
                    m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "opposing melee contact");
                }
                int hits = 0;
                for (const auto &event : m.Events())
                    hits += event.type == "damage" && event.cardId == "twin_blades" && event.targetKind == EntityKind::Troop;
                Check(hits >= 4, "collision permits actual reciprocal melee attacks rather than separating enemies forever");
            }
        });
        Test("opposing structure-only runners pass on their selected bridge without tunneling or permanent deadlock", [] {
            for (const int lane : {-1, 1}) {
                Match m(CollisionOptions("rambeast", "boulderback"));
                std::vector<Entity> starts;
                for (const Team team : {Team::Player, Team::Enemy}) {
                    const auto members = PaidCollisionCard(m, team, "rambeast",
                                                          {lane * 7.5, (team == Team::Player ? 1 : -1) * 2.5});
                    starts.insert(starts.end(), members.begin(), members.end());
                }
                ++collisionPaidFixtures;
                bool crossed[2] = {false, false};
                for (int step = 0; step < 600; ++step) {
                    const auto before = m.State().entities;
                    m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "fast structure-only counterflow");
                    for (const auto &start : starts) {
                        const auto *body = FindEntity(m, start.id);
                        if (!body)
                            continue;
                        const auto &unit = *body;
                        const int side = int(start.team), sign = side == 0 ? 1 : -1;
                        crossed[side] |= unit.position.z * sign < -arena::RiverHalfWidth - .3;
                        if (!unit.dead && std::abs(unit.position.z) < arena::RiverHalfWidth + .28)
                            Check(unit.position.x * lane > 0, "counterflow uses the deployment-side bridge");
                    }
                }
                Check(crossed[0] && crossed[1], "both non-aggro runners pass the opposing body and clear the same bridge");
            }
        });
        Test("dense paid mixed ground crowd queues at bridges and makes bounded forward progress", [] {
            const std::vector<std::string> deck{"boulderback", "twin_blades", "ironclad", "rambeast",
                                                "frost_fang", "ember_archer", "arc_mage", "bullet_burst"};
            for (const Team team : {Team::Player, Team::Enemy}) for (const int lane : {-1, 1}) {
                auto options = Quiet();options.decks = {deck, deck};Match m(options);
                const int sign = team == Team::Player ? 1 : -1;
                std::vector<Entity> starts;
                for (int cycle = 0; cycle < 3; ++cycle)
                    for (const auto &id : deck) {
                        const auto members = PaidCollisionCard(m, team, id,
                            id == "bullet_burst" ? Vec2{-lane * 13.5, -sign * 20.5} : Vec2{lane * 7.5, sign * 6.5});
                        starts.insert(starts.end(), members.begin(), members.end());
                        BodyClearance(m, "dense paid same-tile deployment");
                    }
                ++collisionPaidFixtures;
                Check(starts.size() == 24, "dense paid crowd keeps all twenty-four actual troop members");
                std::map<EntityId, bool> crossed;
                for (int step = 0; step < 900; ++step) {
                    const auto before = m.State().entities;
                    m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "dense mixed bridge queue");
                    for (const auto &start : starts) {
                        const auto *body = FindEntity(m, start.id);
                        if (!body)
                            continue;
                        const auto &unit = *body;
                        if (unit.position.z * sign < -arena::RiverHalfWidth - .28)
                            crossed[unit.id] = true;
                        if (!unit.dead && std::abs(unit.position.z) < arena::RiverHalfWidth + .28) {
                            Check(unit.position.x * lane > 0, "dense queue does not change to the other bridge");
                            ClearOwnStructures(m, unit, unit.position);
                        }
                    }
                }
                std::cout << "COLLISION_QUEUE team=" << int(team) << " lane=" << lane << " members=24 crossed=" << crossed.size() << '\n';
                if (crossed.size() < 18)
                    std::cerr << "QUEUE_MATCH phase=" << PhaseName(m.State().phase) << " elapsed=" << m.State().elapsed
                              << " winner=" << m.State().winner << " enemy_core_hp=" << Tower(m, team == Team::Player ? Team::Enemy : Team::Player, EntityKind::Core).hp << '\n';
                if (crossed.size() < 18)
                    for (const auto &start : starts) {
                        const auto *body = FindEntity(m, start.id);
                        if (!body) {
                            std::cerr << "QUEUE_MEMBER " << start.id << ' ' << start.cardId << " removed_after_death=1 crossed=" << crossed.count(start.id) << '\n';
                            continue;
                        }
                        const auto &unit = *body;
                        std::cerr << "QUEUE_MEMBER " << unit.id << ' ' << unit.cardId << " start=" << start.position.x << ',' << start.position.z
                                  << " end=" << unit.position.x << ',' << unit.position.z << " dead=" << unit.dead
                                  << " target=" << unit.target << " bridge=" << unit.bridge << " stuck=" << unit.stuckTime
                                  << " path_size=" << unit.path.size() << " path_front=" << (unit.path.empty() ? 0 : unit.path.front().x)
                                  << ',' << (unit.path.empty() ? 0 : unit.path.front().z)
                                  << " path_target=" << unit.pathTarget.x << ',' << unit.pathTarget.z
                                  << " crossed=" << crossed.count(unit.id) << '\n';
                    }
                Check(crossed.size() >= 18, "at least three quarters of a twenty-four-member paid crowd clears its bridge within fifteen seconds");
            }
        });
        Test("paid three-building pocket releases every ground member through the selected bridge", [] {
            const std::vector<std::string> deck{"ironclad", "twin_blades", "boulderback", "archer_tower",
                                                "sky_manta", "vampire_bats", "storm_raven", "frost_fang"};
            const int slots[]{1, 2, 2, 3, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3};
            for (const Team team : {Team::Player, Team::Enemy}) for (const int lane : {-1, 1}) {
                auto options = Quiet();options.seed = 135;options.decks[int(team)] = deck;Match m(options);
                const int sign = team == Team::Player ? 1 : -1;
                const int deadline = team == Team::Player ? 12 : 15;
                std::vector<Entity> members, ground, buildings;
                int adjustedBuildingDrops = 0;
                for (int play = 0; play < 16; ++play) {
                    Vec2 drop{lane * 7.5, sign * 8.5};
                    if (play == 9) drop = {lane * 6.5, sign * 7.5};
                    if (play == 15) drop = {lane * 8.5, sign * 7.5};
                    const Vec2 requested = drop;
                    const auto id = m.State().hands[int(team)][slots[play]];
                    const auto *card = FindCard(id);
                    // Mirror body offsets can make a building request illegal.
                    // Choose a real legal requested tile, never edit a spawned body.
                    if (card->building && !m.CanPlace(team, *card, drop)) {
                        bool legal = false;
                        for (int ring = 1; ring <= 8 && !legal; ++ring)
                            for (int direction = 0; direction < 8 && !legal; ++direction) {
                                const double angle = direction * 3.14159265358979323846 * .25;
                                const Vec2 candidate = SnapToTile({requested.x + ring * std::cos(angle),
                                                                   requested.z + ring * std::sin(angle)});
                                if (m.CanPlace(team, *card, candidate)) {drop = candidate;legal = true;}
                            }
                        Check(legal, "paid building pocket finds a lawful nearby requested building tile");
                        ++adjustedBuildingDrops;
                        std::cout << "COLLISION_BUILDING_DROP team=" << int(team) << " lane=" << lane
                                  << " play=" << play << " requested=" << requested.x << ',' << requested.z
                                  << " legal=" << drop.x << ',' << drop.z << '\n';
                    }
                    const auto added = PaidCollisionCard(m, team, id, drop);
                    for (const auto &entity : added) {
                        members.push_back(entity);
                        if (entity.kind == EntityKind::Troop && !entity.flying) ground.push_back(entity);
                        if (entity.kind == EntityKind::Building) buildings.push_back(entity);
                    }
                    BodyClearance(m, "paid three-building pocket deployment");
                }
                ++collisionPaidFixtures;
                Check(members.size() == 31 && ground.size() == 11 && buildings.size() == 3,
                      "exact sixteen paid drops retain thirty-one bodies including eleven ground troops and three buildings");
                Near(FindCard("archer_tower")->lifetime, 25, 0, "pocket keeps authored twenty-five-second building lifetime");
                if (team == Team::Player && lane == 1)
                    Check(adjustedBuildingDrops == 0, "original Shipping pocket preserves every exact requested tile");
                std::map<EntityId, bool> crossed;
                std::size_t crossedAt12 = 0;
                for (int step = 0; step < deadline * 60; ++step) {
                    const auto before = m.State().entities;m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "paid three-building pocket movement");
                    for (const auto &start : before) if (!start.dead && start.kind == EntityKind::Troop)
                        if (const auto *end = FindEntity(m, start.id))
                            Check(std::hypot(end->position.x - start.position.x, end->position.z - start.position.z) <=
                                      FindCard(start.cardId)->moveSpeed / 60. + 1e-8,
                                  "building pocket obeys strict authored movement speed without teleporting");
                    for (const auto &start : buildings) {
                        const auto *body = FindEntity(m, start.id);
                        Check(body && !body->dead && body->hp > 0, "all three paid buildings remain live obstacles until the progress deadline");
                        Near(std::hypot(body->position.x - start.position.x, body->position.z - start.position.z),
                             0, 1e-9, "crowd never shoves its stationary paid building blockers");
                    }
                    for (const auto &start : ground) if (const auto *body = FindEntity(m, start.id)) {
                        if (body->position.z * sign < -arena::RiverHalfWidth - .28) crossed[body->id] = true;
                        if (!body->dead && std::abs(body->position.z) < arena::RiverHalfWidth + .28)
                            Check(body->position.x * lane > 0, "building pocket keeps its deployment-side bridge");
                    }
                    if (step == 719) crossedAt12 = crossed.size();
                }
                std::cout << "COLLISION_BUILDING_POCKET team=" << int(team) << " lane=" << lane
                          << " plays=16 members=31 buildings=3 ground=11 crossed=" << crossed.size()
                          << " crossed_at12=" << crossedAt12 << " living_buildings=3 seconds=" << deadline
                          << " adjusted_building_drops=" << adjustedBuildingDrops << '\n';
                if (crossed.size() != ground.size()) for (const auto &start : ground) if (!crossed.count(start.id)) {
                    const auto *body = FindEntity(m, start.id);
                    std::cerr << "BUILDING_POCKET_STUCK team=" << int(team) << " lane=" << lane
                              << " id=" << start.id << " card=" << start.cardId << " start=" << start.position.x << ',' << start.position.z;
                    if (body) std::cerr << " end=" << body->position.x << ',' << body->position.z
                                       << " dead=" << body->dead << " target=" << body->target << " bridge=" << body->bridge
                                       << " stuck=" << body->stuckTime << " path_size=" << body->path.size();
                    else std::cerr << " removed_after_death=1";
                    std::cerr << '\n';
                }
                Check(crossed.size() == ground.size(), "every paid mobile ground member escapes the live three-building pocket and clears its bridge by the authored deadline");
            }
        });
        Test("collision crowd remains deterministic across fixed-step frame chunks", [] {
            auto options = CollisionOptions("twin_blades", "vampire_bats");
            Match a(options), b(options);
            for (Match *m : {&a, &b})
                for (const Team team : {Team::Player, Team::Enemy}) {
                    const int sign = team == Team::Player ? 1 : -1;
                    PaidCollisionCard(*m, team, "twin_blades", {7.5, sign * 3.5});
                    PaidCollisionCard(*m, team, "vampire_bats", {7.5, sign * 3.5});
                    PaidCollisionCard(*m, team, "ironclad", {7.5, sign * 3.5});
                }
            for (int step = 0; step < 360; ++step) a.Step(1. / 60.);
            for (int step = 0; step < 24; ++step) b.Step(.25);
            Same(a, b);
            BodyClearance(a, "deterministic crowd final bodies");
        });
        Test("stationary paid ranged attackers remain solid while friendly melee routes past", [] {
            const std::vector<std::string> deck{"ember_archer", "ironclad", "archer_tower", "boulderback",
                                                "arc_mage", "rambeast", "frost_fang", "bullet_burst"};
            for (const Team team : {Team::Player, Team::Enemy}) {
                auto options = Quiet();options.decks = {deck, deck};Match m(options);
                const Team opponent = team == Team::Player ? Team::Enemy : Team::Player;
                const int sign = team == Team::Player ? 1 : -1;
                Check(m.SetTowerHP(Tower(m, opponent, EntityKind::Guard, -1).id, 0), "actual Guard destruction exposes the attacker pocket");
                PaidCollisionCard(m, opponent, "archer_tower", {-7.5, -sign * 8.5});
                const auto archer = PaidCollisionCard(m, team, "ember_archer", {-7.5, -sign * 4.5});
                const auto melee = PaidCollisionCard(m, team, "ironclad", {-7.5, -sign * 2.5});
                ++collisionPaidFixtures;
                double closest = 1e9;bool routedPast = false;
                for (int step = 0; step < 150; ++step) {
                    const auto before = m.State().entities;m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "stationary ranged blocker");
                    const auto &a = ById(m, archer[0].id), &b = ById(m, melee[0].id);
                    if (!a.dead && !b.dead) {
                        Near(std::hypot(a.position.x - archer[0].position.x, a.position.z - archer[0].position.z),
                             0, 1e-9, "attacking ranged unit is not forcibly pushed away");
                        closest = std::min(closest, std::hypot(a.position.x - b.position.x, a.position.z - b.position.z));
                        routedPast |= (b.position.z - a.position.z) * sign < -.25;
                    }
                }
                Check(closest < 1.3 && routedPast, "melee approaches and routes past an immobile friendly attack body");
                int shots = 0;for (const auto &event : m.Events())shots += event.type == "attack" && event.source == archer[0].id;
                Check(shots >= 2, "stationary body actually performs ordinary ranged attacks");
            }
        });
        Test("real Raven aura stun keeps the stopped body solid to an unstunned paid follower", [] {
            const auto options = CollisionOptions("boulderback", "ironclad");
            for (const Team team : {Team::Player, Team::Enemy}) {
                auto fixture = options;
                fixture.decks = {std::vector<std::string>{"boulderback", "ironclad", "storm_raven", "twin_blades",
                                                         "arc_mage", "rambeast", "frost_fang", "bullet_burst"},
                                 std::vector<std::string>{"boulderback", "ironclad", "storm_raven", "twin_blades",
                                                         "arc_mage", "rambeast", "frost_fang", "bullet_burst"}};
                Match m(fixture);const int sign = team == Team::Player ? 1 : -1;
                const Team opponent = team == Team::Player ? Team::Enemy : Team::Player;
                PaidCollisionCard(m, opponent, "storm_raven", {7.5, -sign * 6.5});
                const auto front = PaidCollisionCard(m, team, "boulderback", {7.5, sign * 2.5});
                const auto follow = PaidCollisionCard(m, team, "ironclad", {7.5, sign * 6.5});
                ++collisionPaidFixtures;
                int stoppedFrames = 0, independentFollowerFrames = 0;
                for (int step = 0; step < 240; ++step) {
                    const auto before = m.State().entities;
                    const Entity frontBefore = ById(m, front[0].id);
                    m.Step(1. / 60.);SweptBodyClearance(m, before, "actual stun contact");
                    const auto &a = ById(m, front[0].id), &b = ById(m, follow[0].id);
                    // An aura may begin during this update. Only intervals that
                    // started with the status active establish immobility.
                    if (!a.dead && frontBefore.stunUntil > m.State().elapsed + 1e-9) {
                        ++stoppedFrames;
                        Near(std::hypot(a.position.x - frontBefore.position.x, a.position.z - frontBefore.position.z),
                             0, 1e-9, "stunned body never moves or is shoved by the crowd");
                        independentFollowerFrames += !b.dead && b.stunUntil < m.State().elapsed &&
                            std::hypot(a.position.x - b.position.x, a.position.z - b.position.z) < 1.5;
                    }
                }
                Check(stoppedFrames >= 12 && independentFollowerFrames >= 6,
                      "real aura stuns the front body for observed frames while a nearby follower remains unstunned");
            }
        });
        Test("full physical deployment capacity rejects paid cards atomically without consuming IDs", [] {
            for (const Team team : {Team::Player, Team::Enemy}) {
                Match m(CollisionOptions("boulderback", "sky_manta"));
                int buildings = 0, residuals = 0, rejected = 0;
                // These are actual public sandbox casts, including deliberate
                // river rows, rather than fabricated occupied cells or radii.
                for (double z = -21.5; z <= 21.5; z += 2)
                    for (double x = -13.5; x <= 14.5; x += 2)
                        if (m.Spawn(team, "archer_tower", {x, z})) ++buildings; else ++rejected;
                bool exhausted = false;
                for (int n = 0; n < 100; ++n) {
                    if (!m.Spawn(team, "boulderback", {7.5, 8.5})) {++rejected;exhausted = true;break;}
                    ++residuals;
                }
                Check(buildings >= 300 && residuals > 0 && exhausted,
                      "bounded real large/small bodies genuinely exhaust arena capacity");
                BodyClearance(m, "capacity fixture real physical bodies");
                const Vec2 drop{7.5, (team == Team::Player ? 1 : -1) * 8.5};
                Check(m.CanPlace(team, *FindCard("boulderback"), drop), "capacity rejection is a legal-zone attempt");
                m.SetAether(team, 10);
                const Match before = m;
                const auto eventCount = m.Events().size();
                const auto telemetry = m.State().telemetry[int(team)];
                EntityId lastEntity = 0;PlayId lastPlay = 0;
                for (const auto &entity : m.State().entities) lastEntity = std::max(lastEntity, entity.id);
                for (const auto &event : m.Events()) lastPlay = std::max(lastPlay, event.playId);
                const auto lastSequence = m.Events().back().sequence;
                Check(!m.Play(team, 0, drop), "full area rejects paid ground deployment");
                Same(before, m);
                Check(m.Events().size() == eventCount && m.State().queues == before.State().queues &&
                          m.State().spent == before.State().spent && m.State().leaked == before.State().leaked &&
                          m.State().telemetry[int(team)].size() == telemetry.size(),
                      "capacity failure changes no event, queue, economy or telemetry entry");
                for (const auto &[id, original] : telemetry) {
                    const auto &after = m.State().telemetry[int(team)].at(id);
                    Check(after.spawns == original.spawns && after.plays == original.plays && after.spent == original.spent,
                          "rejected capacity keeps every original card spawn/play/spend count");
                }
                const auto air = PaidCollisionCard(m, team, "sky_manta", drop);
                ++collisionPaidFixtures;
                Check(air.size() == 1 && air[0].id == lastEntity + 1 && air[0].playId == lastPlay + 1,
                      "capacity rejection consumes neither entity nor paid play identity");
                // PaidCollisionCard's ordinary SetAether emits the intervening
                // sandbox event. Both subsequent sequences remain contiguous.
                Check(m.Events()[eventCount].sequence == lastSequence + 1 &&
                          m.Events()[eventCount + 1].sequence == lastSequence + 2 &&
                          m.Events()[eventCount + 1].type == "card_play",
                      "capacity rejection consumes no event sequence identity");
                BodyClearance(m, "capacity air layer follow-up");
                std::cout << "COLLISION_CAPACITY team=" << int(team) << " building_spawned=" << buildings
                          << " troop_spawned=" << residuals << " rejected_spawn_requests=" << rejected
                          << " paid_rejected=1 air_followup=1\n";
            }
        });
        Test("twenty-one full seeded AI-v-AI matches all styles", [] {
            const std::vector<std::string> styles{"beatdown", "aggro",       "control", "cycle",
                                                  "split",    "spell_cycle", "counter"};
            const auto start = std::chrono::steady_clock::now();
            for (unsigned n = 0; n < 21; ++n) {
                MatchOptions o;
                o.seed = 5000 + n;
                o.aiEnabled = {true, true};
                o.aiStyles = {styles[n % 7], styles[(n * 3 + 1) % 7]};
                Match m(o);
                for (int tick = 0; tick < 7200 && m.State().phase != Phase::Finished; ++tick) {
                    m.Step(.05);
                    BodyClearance(m, "full-match AI crowd");
                }
                Check(m.State().phase == Phase::Finished, "full match terminates within360");
                Check(m.State().elapsed <= 330, "finite TB duration");
                for (int side = 0; side < 2; ++side) {
                    Check(m.State().aether[side] >= 0 && m.State().aether[side] <= 10, "bank bounds");
                    const double used =
                        m.State().spent[side] + m.State().aether[side] + m.State().leaked[side];
                    const double theoretical =
                        5 + Match::AetherGenerated(0, std::min(300., m.State().elapsed));
                    Check(used <= theoretical + 1e-7 && used >= theoretical - .04,
                          "no free/lost Aether beyond final-phase tick");
                }
                for (const auto &e : m.Events())
                    if (e.type == "damage")
                        Check(std::isfinite(e.amount) && e.amount > 0 && e.amount <= e.requested && e.hp >= 0,
                              "actual finite HP damage");
            }
            const double wall =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "SOAK 21 complete matches wall seconds " << wall << '\n';
        });
        std::cout.precision(12);
        std::cout << "COLLISION_SUMMARY paid_fixtures=" << collisionPaidFixtures << " paid_plays=" << collisionPaidPlays
                  << " members=" << collisionMembers << " endpoint_pair_checks=" << collisionPairChecks
                  << " swept_pair_checks=" << collisionSweepChecks << " min_body_gap=" << collisionMinimumGap
                  << " min_swept_gap=" << collisionMinimumSweepGap << '\n';
        std::cout << "Native authoritative simulation: " << passed << " scenarios passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
