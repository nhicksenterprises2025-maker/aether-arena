#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rift;
namespace {
int passed = 0;
int failed = 0;
bool continueAfterFailure = false;
std::string scenarioFilter;
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
Entity &MutableEntity(Match &m, EntityId id) {
    for (auto &entity : const_cast<Snapshot &>(m.State()).entities)
        if (entity.id == id)
            return entity;
    throw std::runtime_error("fixture entity missing");
}
void FreezeCrowns(Match &m) {
    for (auto &entity : const_cast<Snapshot &>(m.State()).entities)
        if (entity.kind == EntityKind::Guard || entity.kind == EntityKind::Core)
            entity.stunUntil = 100;
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
    if (std::abs(unit.position.z) < arena::RiverHalfWidth + .28) {
        const double half = std::max(.34, arena::BridgeWidth / 2 - .16 - unit.radius * .92);
        Check(std::abs(unit.position.x + arena::BridgeCenterX) <= half + 1e-8 ||
                  std::abs(unit.position.x - arena::BridgeCenterX) <= half + 1e-8,
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
                          std::max(FindEntityDefinition(start.cardId)->moveSpeed,
                                   (start.charged || after->charged) ? FindEntityDefinition(start.cardId)->chargedMoveSpeed : 0.) / 60. + 1e-6,
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
            Near(entity.radius, card->collisionRadius,
                 1e-12, "collision keeps canonical per-member radius");
            members.push_back(entity);
        }
    }
    Check(plays == 1 && int(members.size()) == (card->spell ? 0 : card->count), "collision no missing or extra paid members");
    collisionMembers += int(members.size());
    return members;
}
void Test(const char *name, const std::function<void()> &fn) {
    if (!scenarioFilter.empty() && std::string(name).find(scenarioFilter) == std::string::npos)
        return;
    try {fn();}
    catch (const std::exception &error) {
        if (!continueAfterFailure) throw;
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return;
    }
    ++passed;
    std::cout << "PASS " << name << '\n';
}
void Same(const Match &left, const Match &right) {
    const auto &x = left.State();
    const auto &y = right.State();
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
    Check(x.shockwaves.size() == y.shockwaves.size(), "deterministic shockwave count");
    for (std::size_t n = 0; n < x.shockwaves.size(); ++n) {
        const auto &a = x.shockwaves[n], &b = y.shockwaves[n];
        Check(a.id == b.id && a.source == b.source && a.playId == b.playId && a.team == b.team &&
                  a.cardId == b.cardId && a.hitTargets == b.hitTargets, "deterministic wave identity/hit membership");
        Near(a.origin.x, b.origin.x, 0, "deterministic wave originX");
        Near(a.origin.z, b.origin.z, 0, "deterministic wave originZ");
        Near(a.progress, b.progress, 1e-9, "deterministic wave progress");
        Near(a.born, b.born, 1e-9, "deterministic wave launch");
        Near(a.expires, b.expires, 1e-9, "deterministic wave expiry");
    }
    for (std::size_t n = 0; n < x.entities.size(); ++n) {
        const auto &e = x.entities[n];
        const auto &f = y.entities[n];
        Check(e.id == f.id && e.target == f.target && e.hardLock == f.hardLock && e.dead == f.dead,
              "deterministic target/death");
        Check(e.parentEntityId == f.parentEntityId && e.attributionCardId == f.attributionCardId &&
                  e.invisibilitySource == f.invisibilitySource && e.invisibilityPlayId == f.invisibilityPlayId &&
                  e.invisibilityCardId == f.invisibilityCardId && e.charged == f.charged,
              "deterministic child, cloak and charge identity");
        Near(e.hp, f.hp, 1e-8, "deterministic HP");
        Near(e.shieldHP, f.shieldHP, 1e-8, "deterministic remaining shield");
        Near(e.maxShieldHP, f.maxShieldHP, 0, "deterministic maximum shield");
        Near(e.shieldBrokenAt, f.shieldBrokenAt, 1e-9, "deterministic shield break time");
        Near(e.invisibleUntil, f.invisibleUntil, 1e-9, "deterministic cloak expiry");
        Near(e.chargeTime, f.chargeTime, 1e-9, "deterministic actual charge travel");
        Near(e.position.x, f.position.x, 1e-8, "deterministic X");
        Near(e.position.z, f.position.z, 1e-8, "deterministic Z");
        Check(e.slows.size() == f.slows.size(), "deterministic independent slow components");
        for (std::size_t k = 0; k < e.slows.size(); ++k) {
            const auto &a = e.slows[k], &b = f.slows[k];
            Check(a.source == b.source && a.playId == b.playId && a.team == b.team && a.cardId == b.cardId,
                  "deterministic slow component identity");
            Near(a.pct, b.pct, 0, "deterministic slow component potency");
            Near(a.born, b.born, 1e-9, "deterministic slow component refresh");
            Near(a.until, b.until, 1e-9, "deterministic slow component expiry");
        }
    }
    for (int i = 0; i < 2; ++i) {
        Near(x.aether[i], y.aether[i], 1e-8, "deterministic Aether");
        Check(x.hands[i] == y.hands[i], "deterministic hand");
        Check(x.entityTelemetry[i].size() == y.entityTelemetry[i].size(), "deterministic individual body records");
        for (const auto &[id, record] : x.entityTelemetry[i]) {
            const auto &other = y.entityTelemetry[i].at(id);
            Check(record.id == other.id && record.parentEntityId == other.parentEntityId &&
                      record.playId == other.playId && record.cardId == other.cardId &&
                      record.attributionCardId == other.attributionCardId,
                  "deterministic persistent body attribution");
            const auto &a = record.observed, &b = other.observed;
            Check(a.spawns == b.spawns && a.deaths == b.deaths && a.surviving == b.surviving &&
                      a.childSpawns == b.childSpawns && a.childDeaths == b.childDeaths &&
                      a.childSurviving == b.childSurviving && a.targets == b.targets && a.connected == b.connected,
                  "deterministic individual body counters");
            Near(a.damageTaken, b.damageTaken, 1e-8, "deterministic individual body damage");
            Near(a.shieldDamageTaken, b.shieldDamageTaken, 1e-8, "deterministic individual shield loss");
            Near(a.shieldAbsorbed, b.shieldAbsorbed, 1e-8, "deterministic individual shield absorption");
            Near(a.overflowPrevented, b.overflowPrevented, 1e-8, "deterministic individual shield overflow");
            Near(a.lifetime, b.lifetime, 1e-8, "deterministic individual lifetime");
            Near(a.buildingLifetime, b.buildingLifetime, 1e-8, "deterministic individual building lifetime");
            Near(a.slowTime, b.slowTime, 1e-8, "deterministic individual credited slow");
            Near(a.stunTime, b.stunTime, 1e-8, "deterministic individual credited stun");
        }
    }
    Check(left.Events().size() == right.Events().size(), "deterministic events");
    for (std::size_t n = 0; n < left.Events().size(); ++n) {
        const auto &e = left.Events()[n];
        const auto &f = right.Events()[n];
        Check(e.sequence == f.sequence && e.type == f.type && e.cardId == f.cardId &&
                  e.playId == f.playId && e.target == f.target && e.source == f.source &&
                  e.parentEntityId == f.parentEntityId && e.attributionCardId == f.attributionCardId,
              "deterministic event identity and order");
        Near(e.time, f.time, 1e-9, "deterministic event time");
        Near(e.until, f.until, 1e-9, "deterministic event deadline");
        Near(e.amount, f.amount, 1e-8, "deterministic event amount");
        Near(e.shieldHP, f.shieldHP, 1e-8, "deterministic event remaining shield");
        Near(e.shieldOverflowBlocked, f.shieldOverflowBlocked, 1e-8, "deterministic event blocked overflow");
    }
}
} // namespace
int main(int argc, char **argv) {
    for (int argument = 1; argument < argc; ++argument)
        if (std::string(argv[argument]) == "--all-errors") continueAfterFailure = true;
        else scenarioFilter = argv[argument];
    try {
        Test("expansion shield protects breaking packet and preserves separate body telemetry", [] {
            Match m(CollisionOptions("iron_giant", "ember_archer"));FreezeCrowns(m);
            const auto giant = PaidCollisionCard(m, Team::Player, "iron_giant", {.5, 8.5})[0];
            MutableEntity(m, giant.id).stunUntil = 30;
            Check(m.Spawn(Team::Enemy, "ember_archer", {.5, 5.5}), "actual projectile attacker");
            while (m.State().projectiles.empty() && m.State().elapsed < 1) m.Step(1. / 60.);
            Check(!m.State().projectiles.empty(), "real attack launches a committed projectile");
            const auto attacker = m.State().projectiles[0].source;
            auto &projectile = const_cast<Snapshot &>(m.State()).projectiles[0];
            projectile.damage = 400; // Controlled incoming packet, not a card-stat modification.
            MutableEntity(m, attacker).stunUntil = 30;
            m.Step(.5);
            Near(ById(m, giant.id).shieldHP, 0, 0, "400 packet consumes310 shield");
            Near(ById(m, giant.id).hp, 1000, 0, "breaking packet discards90 overflow");
            Check(ById(m, giant.id).shieldBrokenAt >= 0 && !ById(m, giant.id).dead, "shield break timestamp is authoritative and body remains live");
            const double broken = ById(m, giant.id).shieldBrokenAt;
            auto &second = const_cast<Snapshot &>(m.State()).projectiles.emplace_back();
            second.source = attacker;second.target = giant.id;second.team = Team::Enemy;
            second.cardId = "ember_archer";second.damage = 400;second.remaining = 1. / 60.;
            m.Step(1. / 60.);
            Near(ById(m, giant.id).hp, 600, 0, "next400 packet reaches body");
            Near(ById(m, giant.id).shieldBrokenAt, broken, 0, "later damage cannot restart shield fragments");
            const auto &tele = m.State().telemetry[0].at("iron_giant");
            Near(tele.shieldDamageTaken, 310, 0, "actual shield loss");
            Near(tele.shieldAbsorbed, 400, 0, "shield blocked full packet");
            Near(tele.overflowPrevented, 90, 0, "discarded overflow separate");
            Near(tele.damageTaken, 400, 0, "body damage excludes shield");
            const auto &observed = m.State().entityTelemetry[0].at(giant.id).observed;
            Near(observed.shieldDamageTaken, 310, 0, "entity observation retains actual shield loss");
            Near(observed.damageTaken, 400, 0, "entity observation retains actual body loss");
            int shieldEvents = 0;
            for (const auto &event : m.Events()) if (event.type == "shield_damage" && event.target == giant.id) {
                ++shieldEvents;Near(event.amount, 310, 0, "shield packet actual amount");
                Near(event.requested, 400, 0, "shield packet incoming amount");
                Near(event.shieldOverflowBlocked, 90, 0, "shield packet explicit overflow");
            }
            Check(shieldEvents == 1, "exactly one breaking shield packet");
            m.Step(3);Near(ById(m, giant.id).shieldHP, 0, 0, "no shield regeneration");
        });
        Test("expansion Bone Warden death children spawn once through every lethal damage path", [] {
            for (const Team team : {Team::Player, Team::Enemy})
                for (const std::string mode : {"initial", "dot", "projectile", "melee", "aura", "shockwave"}) {
                    Match m(CollisionOptions("bone_warden", "ironclad"));FreezeCrowns(m);
                    const Team foe = team == Team::Player ? Team::Enemy : Team::Player;
                    const double sign = team == Team::Player ? 1 : -1;
                    const auto parent = PaidCollisionCard(m, team, "bone_warden", {.5, sign * 7.5})[0];
                    auto &body = MutableEntity(m, parent.id);body.stunUntil = 30;
                    body.hp = mode == "dot" ? 263 : 1;
                    const Vec2 position = body.position;
                    const std::string attack = mode == "initial" ? "bullet_burst" : mode == "dot" ? "meteor_shards"
                                               : mode == "projectile" ? "ember_archer" : mode == "melee" ? "ironclad"
                                               : mode == "aura" ? "storm_raven" : "pulverizer";
                    Check(m.Spawn(foe, attack, mode == "initial" || mode == "dot" ? position
                                                 : Vec2{position.x + (mode == "melee" ? 1.5 : mode == "aura" ? 2. : 2.5), position.z}), "real lethal source");
                    if (mode == "aura") {
                        const auto source = Unit(m, attack, foe).id;
                        MutableEntity(m, source).auraClock = 2.99;
                        MutableEntity(m, source).cooldown = 30;
                    }
                    const double spent = m.State().spent[int(team)];
                    int ticks = 0;
                    while (Count(m, "bone_warden_skeleton", team) == 0 && ticks++ < 180) m.Step(1. / 60.);
                    Check(Count(m, "bone_warden_skeleton", team) == 4, "lethal path releases all four actual children " + mode);
                    const auto &tele = m.State().telemetry[int(team)].at("bone_warden");
                    Check(tele.plays == 1 && tele.spawns == 1 && tele.deaths == 1 && tele.childSpawns == 4,
                          "one paid parent, one body death, four subordinate spawns");
                    Near(m.State().spent[int(team)], spent, 0, "children grant no paid cost or play");
                    BodyClearance(m, "death children real collision packing");
                    int childEvents = 0;
                    for (const auto &event : m.Events()) if (event.type == "entity_spawn" && event.parentEntityId == parent.id) {
                        ++childEvents;Check(event.playId == parent.playId && event.attributionCardId == "bone_warden" &&
                            event.cardId == "bone_warden_skeleton", "children preserve separate entity and original paid attribution");
                        const auto &child = ById(m, event.source);
                        Near(child.maxHp, 50, 0, "actual child bodyHP");
                        Check(child.parentEntityId == parent.id && child.playId == parent.playId && !child.dead,
                              "child identity remains playable physics");
                    }
                    Check(childEvents == 4, "one event per actual child");
                    // Stop further attacks; verify corpse removal does not spawn a second group.
                    for (auto &entity : const_cast<Snapshot &>(m.State()).entities) entity.stunUntil = 30;
                    m.Step(.6);Check(!FindEntity(m, parent.id), "parent corpse is actually removed");
                    Check(m.State().telemetry[int(team)].at("bone_warden").childSpawns == 4,
                          "removal cannot duplicate death spawn");
                }
        });
        Test("expansion local cloak drops troop and Crown locks but preserves committed damage", [] {
            Match m(CollisionOptions("shadow_surge", "ironclad"));FreezeCrowns(m);
            Check(m.Spawn(Team::Player, "ironclad", {.5, 7.5}), "inside friendly troop");
            Check(m.Spawn(Team::Player, "sky_manta", {4.5, 7.5}), "outside friendly troop");
            Check(m.Spawn(Team::Player, "archer_tower", {.5, 11.5}), "friendly building");
            Check(m.Spawn(Team::Enemy, "ember_archer", {.5, 4.5}), "actual enemy projectile source");
            const auto victim = Unit(m, "ironclad", Team::Player).id;
            MutableEntity(m, victim).stunUntil = 30;
            while (m.State().projectiles.empty() && m.State().elapsed < 1) m.Step(1. / 60.);
            Check(!m.State().projectiles.empty(), "real projectile exists before cloak");
            const auto attacker = m.State().projectiles[0].source;
            const double hp = ById(m, victim).hp;
            auto &projectile = const_cast<Snapshot &>(m.State()).projectiles[0];projectile.remaining = .4;
            auto &enemy = MutableEntity(m, attacker);enemy.target = enemy.hardLock = enemy.forcedTarget = victim;
            enemy.cooldown = 30;
            auto &crown = MutableEntity(m, Tower(m, Team::Enemy, EntityKind::Guard, 1).id);
            crown.target = crown.hardLock = crown.forcedTarget = victim;
            PaidCollisionCard(m, Team::Player, "shadow_surge", {.5, 7.5});
            m.Step(.25);
            Check(ById(m, victim).invisibleUntil > m.State().elapsed, "local inside troop cloaked at impact");
            Near(Unit(m, "sky_manta", Team::Player).invisibleUntil, 0, 0, "outside local radius stays visible");
            Near(Unit(m, "archer_tower", Team::Player).invisibleUntil, 0, 0, "building is not cloaked");
            Check(ById(m, attacker).target != victim && ById(m, attacker).hardLock == 0 &&
                      ById(m, attacker).forcedTarget == 0 && crown.target == 0 && crown.hardLock == 0,
                  "apply explicitly drops ordinary and Crown existing locks");
            Check(!m.CanTarget(ById(m, attacker), ById(m, victim)), "cloak denies new targets");
            m.Step(.2);Near(ById(m, victim).hp, hp - 152, 1e-9, "committed projectile still resolves");
            m.Step(.8);Check(m.CanTarget(ById(m, attacker), ById(m, victim)), "exact one-second cloak expires");
            int applied = 0, expired = 0, drops = 0;
            for (const auto &event : m.Events()) {
                applied += event.type == "invisibility_apply" && event.target == victim;
                expired += event.type == "invisibility_expire" && event.target == victim;
                drops += event.type == "target_drop" && event.target == victim;
            }
            Check(applied == 1 && expired == 1 && drops >= 2, "explicit replayable apply/drop/expiry events");
        });
        Test("expansion Archer decay hard expiry and Meteor structure DOT are independent", [] {
            Match m(Quiet());FreezeCrowns(m);
            Check(m.Spawn(Team::Player, "archer_tower", {.5, 8.5}), "actual Archer");
            const auto tower = Unit(m, "archer_tower", Team::Player).id;
            m.Step(24.9);Near(ById(m, tower).hp, 850 - 24.9 * 25, 1e-7, "25HP per second explicit decay");
            Check(!ById(m, tower).dead, "Archer still lives before hard expiry with HP remaining");
            m.Step(.1);Check(ById(m, tower).dead, "hard25second expiry removes remaining225HP");
            Match spell(Quiet());FreezeCrowns(spell);
            const auto guard = Tower(spell, Team::Enemy, EntityKind::Guard, 1);
            Check(spell.Spawn(Team::Enemy, "boulderback", {guard.position.x, guard.position.z + 3}), "troop near real Crown");
            const auto troop = Unit(spell, "boulderback", Team::Enemy).id;MutableEntity(spell, troop).stunUntil = 30;
            Check(spell.Spawn(Team::Player, "meteor_shards", guard.position), "actual delayed Meteor");
            spell.Step(.75);Near(ById(spell, guard.id).hp, 2250 - 143, 0, "structure initial143");
            spell.Step(5);Near(ById(spell, guard.id).hp, 2250 - 143 - 5 * 15, 1e-8, "five structure15 ticks");
            Near(ById(spell, troop).hp, 1620 - 262 - 5 * 40, 1e-8, "same five troop40 ticks");
            Check(spell.State().telemetry[0].at("meteor_shards").dotTicks == 5, "exactly five shared zone ticks");
        });
        Test("expansion shield processes each Meteor packet independently", [] {
            Match m(Quiet());FreezeCrowns(m);
            Check(m.Spawn(Team::Enemy, "iron_giant", {.5, 8.5}), "real Giant shield target");
            const auto id = Unit(m, "iron_giant", Team::Enemy).id;MutableEntity(m,id).stunUntil=30;
            Check(m.Spawn(Team::Player, "meteor_shards", {.5,8.5}), "actual Meteor packets");
            m.Step(.75);Near(ById(m,id).shieldHP,48,0,"initial262 leaves48 shield");
            m.Step(1);Near(ById(m,id).shieldHP,8,0,"first40 tick leaves8 shield");
            m.Step(1);Near(ById(m,id).shieldHP,0,0,"second40 tick breaks shield");
            Near(ById(m,id).hp,1000,0,"second tick discards32 rather than hurting body");
            m.Step(3);Near(ById(m,id).hp,880,1e-9,"remaining three40 ticks hurt body");
            const auto &tele=m.State().telemetry[1].at("iron_giant");
            Near(tele.shieldDamageTaken,310,0,"separate total actual shield loss");
            Near(tele.shieldAbsorbed,342,0,"three shield packets fully blocked");
            Near(tele.overflowPrevented,32,0,"actual breaking DOT overflow");
            Near(tele.damageTaken,120,0,"actual three body packets");
        });
        Test("expansion Reaper direct melee splash respects ground air and structure masks", [] {
            Match m(Quiet());FreezeCrowns(m);
            m.Spawn(Team::Player,"aether_reaper",{.5,8.5});
            m.Spawn(Team::Enemy,"ironclad",{.5,7.5});
            m.Spawn(Team::Enemy,"sky_manta",{1.5,7.5});
            m.Spawn(Team::Enemy,"archer_tower",{3.5,7.5});
            const auto troop=Unit(m,"ironclad",Team::Enemy).id, air=Unit(m,"sky_manta",Team::Enemy).id,
                       structure=Unit(m,"archer_tower",Team::Enemy).id;
            MutableEntity(m,structure).position={2.2,7.5};
            for(auto &e:const_cast<Snapshot &>(m.State()).entities)if(e.team==Team::Enemy)e.stunUntil=30;
            BodyClearance(m,"controlled actual Reaper contacts");m.Step(1./60.);
            int hits=0;std::set<EntityId> targets;
            for(const auto &event:m.Events())if(event.type=="damage"&&event.cardId=="aether_reaper") {
                ++hits;targets.insert(event.target);Near(event.requested,95,0,"one95 damage packet per contact");
            }
            Check(hits==3&&targets==std::set<EntityId>{troop,air,structure},"one melee attack reaches three eligible contacts");
            Check(std::none_of(m.State().projectiles.begin(),m.State().projectiles.end(),[](const Projectile&p){return p.cardId=="aether_reaper";}),
                  "direct Reaper attack has no invented projectile");
        });
        Test("expansion charged movement speed and total opening packet follow real movement", [] {
            Match m(Quiet());FreezeCrowns(m);m.Spawn(Team::Player,"rambeast",{arena::BridgeCenterX,8.5});
            const auto id=Unit(m,"rambeast",Team::Player).id;
            m.Step(1.5);Check(!ById(m,id).charged,"exact1.5 accumulated seconds does not charge before threshold");
            m.Step(1./60.);Check(ById(m,id).charged,"next actual movement tick charges");
            double maximum=0;bool hit=false;
            for(int n=0;n<1000&&!hit;++n) {
                const Entity before=ById(m,id);m.Step(1./60.);
                const auto *after=FindEntity(m,id);if(!after||after->dead)break;
                const double distance=std::hypot(after->position.x-before.position.x,after->position.z-before.position.z);
                maximum=std::max(maximum,distance*60);
                Check(distance<=3.2/60.+1e-8,"charged movement remains physically speed bounded");
                for(const auto &event:m.Events())if(event.type=="damage"&&event.source==id&&event.targetKind!=EntityKind::Troop) {
                    Near(event.requested,302,0,"first charged structure packet is total302");hit=true;break;
                }
            }
            Check(maximum>3.19&&hit,"actual movement reaches3.2 before actual302 hit");
            int ready=0,spent=0;for(const auto &event:m.Events()){ready+=event.type=="charge_ready"&&event.source==id;spent+=event.type=="charge_spent"&&event.source==id;}
            Check(ready==1&&spent==1,"exact charge entry and spend are replayable");
        });
        Test("complete exact roster and swarm DPS", [] {
            Check(Cards().size() == 21 && EntityDefinitions().size() == 1 && !FindCard("bone_warden_skeleton"), "twenty-one playable cards and one subordinate outside decks");
            const std::vector<double> hp{822, 374, 262, 1620, 512, 934, 480, 174, 1265, 1222, 0, 850, 0, 0, 111, 111, 700, 1526, 445, 1000, 0},
                damage{93, 152, 55, 118, 108, 156, 77, 86, 72, 251, 262, 75, 175, 375, 20, 20, 65, 101, 95, 85, 100},
                interval{1.1, 1.75, .75, 2, 1.15, 1.25, .92, 1.05, .75, 1.7, 0, .9, 0, 0, .9, 1, .9, 1.5, 1.05, 1.3, 0};
            for (std::size_t n = 0; n < Cards().size(); ++n) {
                Near(Cards()[n].hp, hp[n], 0, "roster HP");
                Near(Cards()[n].damage, damage[n], 0, "roster damage");
                Near(Cards()[n].attackInterval, interval[n], 0, "roster attack interval");
            }
            Near(FindCard("rambeast")->chargeDamage, 302, 0, "total charged packet");
            Check(FindCard("vampire_bats")->count == 5, "five Bats");
            Check(FindCard("twin_blades")->count == 2, "two Blades");
            Check(FindCard("mini_stampede")->count == 5 && FindCard("mini_stampede")->cost == 2,
                  "five ordinary hogs cost two Aether");
            Check(FindCard("stampede")->count == 15 && FindCard("stampede")->cost == 7 &&
                      FindCard("stampede")->structuresOnly && !FindCard("mini_stampede")->structuresOnly,
                  "fifteen royal hogs cost seven Aether and target structures");
            Near(FindCard("mini_stampede")->damage / FindCard("mini_stampede")->attackInterval, 20/.9, 1e-12,
                 "Mini Stampede correct derived per-hog DPS");
            Near(FindCard("stampede")->damage / FindCard("stampede")->attackInterval * 15, 300, 1e-12,
                 "Stampede has300 basic deployment DPS");
            Near(FindCard("archer_tower")->footprint, 1.65, 0, "building footprint");
            Near(FindCard("nova_flask")->towerDamage, 185, 0, "nova structures");
            for (const auto &card : Cards())
                Near(card.castDelay,
                     card.id == "meteor_shards" ? .75 : card.id == "bullet_burst" ? .30 :
                     card.id == "nova_flask" ? .85 : card.id == "shadow_surge" ? .25 : 0,
                     0, "all four approved spell windups");
        });
        Test("current patch Pulverizer exact paid stats footprint lifetime", [] {
            const auto *card = FindCard("pulverizer");
            Check(card && card->building && card->count == 1 && card->cost == 3 && !card->canHitAir &&
                      !card->structuresOnly && !card->spell, "ground-only defensive building definition");
            Near(card->hp, 700, 0, "Pulverizer HP");Near(card->damage, 65, 0, "wave hit damage");
            Near(card->attackInterval, .9, 0, "wave attack cadence");Near(card->range, 4, 0, "unchanged attack range");
            Near(card->splash, 4, 0, "radial reach");Near(card->shockwaveSpeed, 8, 0, "radial travel speed");
            Near(card->slowPct, .10, 0, "nonstacking slow potency");Near(card->slowDuration, 1.5, 0, "slow refresh duration");
            Near(card->footprint, 1.65, 0, "building footprint");Near(card->scale, 1.05, 0, "model scale");
            Near(card->lifetime, 45, 0, "natural expiry");Near(card->moveSpeed, 0, 0, "stationary");
            Near(card->projectileSpeed, 0, 0, "shockwave is independent of ordinary projectiles");
            for (Team team : {Team::Player, Team::Enemy}) {
                Match m(CollisionOptions("pulverizer", "ironclad"));
                const double sign = team == Team::Player ? 1 : -1;
                const auto before = m.State().hands[int(team)];
                m.SetAether(team, 10);
                Check(!m.Play(team, 0, {.5, sign * 2.5}), "whole building footprint must be deployable");
                Near(m.State().aether[int(team)], 10, 0, "invalid footprint does not pay");
                Check(m.State().hands[int(team)] == before, "invalid footprint does not cycle");
                auto units = PaidCollisionCard(m, team, "pulverizer", {.5, sign * 8.5});
                Check(units.size() == 1, "one paid building");Near(units[0].radius, 1.65*.52, 1e-12, "real collision radius");
                m.Step(44.95);Check(!ById(m, units[0].id).dead, "alive before45seconds");
                Near(ById(m, units[0].id).position.x, .5, 0, "building fixedX");
                m.Step(.1);Check(ById(m, units[0].id).dead, "expires at45seconds");
                Near(m.State().telemetry[int(team)].at("pulverizer").buildingLifetime, 45, 1e-7,
                     "actual full lifetime telemetry");
            }
        });
        Test("current patch traveling ground-only wave hits once and survives source death", [] {
            Match m(CollisionOptions("pulverizer", "ironclad"));
            FreezeCrowns(m);
            const auto source = PaidCollisionCard(m, Team::Player, "pulverizer", {.5, 8.5})[0];
            Check(m.Spawn(Team::Enemy, "boulderback", {.5, 4.5}), "controlled ground victim");
            const auto victim = Unit(m, "boulderback", Team::Enemy).id;
            MutableEntity(m, victim).stunUntil = 20;
            Check(m.Spawn(Team::Enemy, "sky_manta", {2.5, 8.5}), "air victim in radial area");
            MutableEntity(m, Unit(m, "sky_manta", Team::Enemy).id).stunUntil = 20;
            Check(m.Spawn(Team::Enemy, "archer_tower", {-2.5, 8.5}), "enemy structure in radial area");
            const auto structure = Unit(m, "archer_tower", Team::Enemy).id;
            Check(m.Spawn(Team::Player, "ironclad", {3.5, 8.5}), "friendly ground body");
            MutableEntity(m, Unit(m, "ironclad", Team::Player).id).stunUntil = 20;
            while(m.State().shockwaves.empty() && m.State().elapsed<1)
                m.Step(1./60.);
            Check(m.State().shockwaves.size() == 1 && std::none_of(m.State().projectiles.begin(),m.State().projectiles.end(),
                [](const Projectile &projectile){return projectile.cardId=="pulverizer";}), "attack launches one independent wave");
            const auto wave = m.State().shockwaves[0];
            Check(wave.source == source.id && wave.playId == source.playId && wave.cardId == "pulverizer" &&
                      wave.hitTargets.empty(), "wave preserves paid attribution and initial hit set");
            Near(wave.progress, 0, 0, "launch tick has no instant radial movement or damage");
            Near(wave.expires-wave.born, .5, 1e-12, "four tiles at eight tiles per second");
            Check(std::any_of(m.Events().begin(), m.Events().end(), [&](const Event &event) {
                return event.sequence == wave.id && event.type == "shockwave_launch";
            }), "wave identity comes from replayable launch sequence");
            m.Step(.1);Near(ById(m, victim).hp, 1620, 0, "distant enemy is not hit during travel");
            Near(m.State().shockwaves[0].progress, .8, 1e-9, "actual radial speed");
            MutableEntity(m, source.id).hp = 1;
            Check(m.Spawn(Team::Enemy, "bullet_burst", {.5, 8.5}), "real delayed spell destroys source after launch");
            m.Step(.30);
            Check(ById(m, source.id).dead && m.State().shockwaves.size() == 1, "source death leaves wave alive");
            double damageTime = -1;
            for (int step = 0; step < 25; ++step) {
                m.Step(1./60.);
                for (const auto &event : m.Events())
                    if (event.type == "damage" && event.cardId == "pulverizer" && event.target == victim)
                        damageTime = event.time;
            }
            const double contact = wave.born + (4-ById(m,victim).radius)/8;
            Check(damageTime >= contact-1e-9 && damageTime <= contact+1./60.+1e-9,
                  "wave contacts body at distance divided by travel speed");
            int hits=0, forbidden=0, structureHits=0;
            for (const auto &event : m.Events())
                if (event.type == "damage" && event.cardId == "pulverizer") {
                    hits += event.target == victim;
                    structureHits += event.target == structure;
                    forbidden += event.target != victim && event.target != structure;
                    Check(event.damageKind == "shockwave" && event.requested == 65 && event.source == source.id &&
                              event.playId == source.playId, "post-death hit retains exact damage/source/play attribution");
                }
            if (hits != 1 || structureHits != 0 || forbidden) {
                std::cerr << "WAVE_CONTACTS troop=" << hits << " structure=" << structureHits << " forbidden=" << forbidden << '\n';
                for (const auto &event : m.Events()) if (event.type == "damage" && event.cardId == "pulverizer")
                    std::cerr << "WAVE_CONTACT target=" << event.target << " kind=" << int(event.targetKind)
                              << " at=" << event.time << " requested=" << event.requested << '\n';
            }
            Check(hits == 1 && structureHits == 0 && forbidden == 0, "one hit per wave on ground troops only, no ally, air or structure damage");
            Check(m.State().shockwaves.empty(), "wave expires after four-tile travel");
            Near(ById(m,victim).slowPct, .10, 0, "travel hit applies ten percent slow");
            Near(m.State().telemetry[0].at("pulverizer").troopDamage, 65, 0, "post-death actual damage telemetry");
            m.Step(1.5);Near(ById(m,victim).slowPct, 0, 0, "slow expires without source or another wave");
            Check(m.State().telemetry[0].at("pulverizer").slowTime > 1.4, "credited realized slow uptime");
        });
        Test("current patch repeated waves refresh without stacking and preserve Frost expiry", [] {
            Match m(CollisionOptions("pulverizer", "frost_fang"));
            FreezeCrowns(m);
            PaidCollisionCard(m, Team::Player, "pulverizer", {.5, 8.5});
            m.Spawn(Team::Enemy, "boulderback", {.5, 5.5});
            const auto id=Unit(m,"boulderback",Team::Enemy).id;MutableEntity(m,id).stunUntil=20;
            m.Step(.8);const double firstUntil=ById(m,id).slowUntil;
            m.Step(.9);Near(ById(m,id).slowPct,.10,0,"two waves stay at ten percent");
            Check(ById(m,id).slowUntil > firstUntil+.85 && ById(m,id).slows.size()==1,
                  "same-card wave refresh extends one independent component");
            m.Spawn(Team::Player,"frost_fang",{1.5,5.5});m.Step(.05);
            const auto frost=Unit(m,"frost_fang",Team::Player).id;
            Near(ById(m,id).slowPct,.3,0,"stronger Frost remains thirty percent");
            const double frostUntil=ById(m,id).slowUntil;
            MutableEntity(m,frost).stunUntil=20;
            m.Step(1.1);Near(ById(m,id).slowPct,.3,0,"weak refresh cannot reduce stronger Frost");
            Near(ById(m,id).slowUntil,frostUntil,1e-9,"weak refresh cannot prolong stronger Frost expiry");
            Check(ById(m,id).slows.size()==2,"separate serializable weak and strong expiries");
            m.Step(1.);Near(ById(m,id).slowPct,.10,0,"live refreshed weaker effect resumes after Frost expires");
            Check(ById(m,id).slows.size()==1 && ById(m,id).slows[0].cardId=="pulverizer",
                  "expired Frost component is removed independently");
            bool refreshed=false;
            for(const auto&e:m.Events())if(e.type=="slow"&&e.cardId=="pulverizer"){
                Near(e.amount,.1,0,"slow event declares incoming weak potency honestly");
                Near(e.until-e.time,1.5,1e-9,"every weak refresh has exact1.5second duration");
                refreshed |= e.reason=="refresh";
            }
            Check(refreshed,"refresh event is explicit");
        });
        Test("current patch strict detection applies to every deployable card and retention", [] {
            Match m(Quiet());Entity target;target.id=900;target.team=Team::Enemy;target.radius=10;
            int cards=0;
            for(const auto &card:Cards())if(!card.spell){
                Entity source;source.id=901;source.cardId=card.id;source.facing={0,-1};source.radius=10;
                for(const double boundary:{detection::Front,detection::Rear}){
                    const double sign=boundary==detection::Front?-1:1;
                    target.position={0,sign*boundary};Check(m.InSight(source,target),"exact shared boundary inclusive");
                    target.position={0,sign*(boundary+.00001)};Check(!m.InSight(source,target),"no radius or pursuit bonus");
                }
                ++cards;
            }
            Check(cards==17,"all seventeen physical cards share strict8/5");
            for(const auto&id:std::vector<std::string>{"ironclad","ember_archer","archer_tower","pulverizer","stampede"}){
                Match a(Quiet());a.Spawn(Team::Player,id,{.5,8.5});
                a.Spawn(Team::Enemy,id=="stampede"?"archer_tower":"ironclad",{.5,4.5});
                const auto source=Unit(a,id,Team::Player).id;
                const auto foe=Unit(a,id=="stampede"?"archer_tower":"ironclad",Team::Enemy).id;
                auto &s=MutableEntity(a,source);s.target=s.forcedTarget=foe;s.forcedUntil=20;s.hardLock=0;
                s.scanClock=20;s.cooldown=20;s.facing={0,-1};
                MutableEntity(a,foe).position={s.position.x,s.position.z-8.001};
                MutableEntity(a,foe).stunUntil=20;a.Step(1./60.);
                Check(ById(a,source).target!=foe && ById(a,source).forcedTarget==0,
                      "current and forced pursuit drop immediately outside strict front boundary");
            }
            Match building(Quiet());building.Spawn(Team::Player,"archer_tower",{.5,8.5});
            building.Spawn(Team::Enemy,"ironclad",{.5,14.5});
            building.Step(.03);Check(Unit(building,"archer_tower",Team::Player).target==0,
                                      "rear enemy within seven attack tiles remains outside five detection tiles");
            int pulls=0;
            for(Team team:{Team::Player,Team::Enemy})for(const auto&id:std::vector<std::string>{"ironclad","stampede"})
                for(bool structure:{false,true})for(bool front:{false,true})for(bool inside:{false,true}){
                    Match fresh(Quiet());FreezeCrowns(fresh);
                    const double home=team==Team::Player?1:-1;
                    fresh.Spawn(team,id,{.5,home*7.5});
                    const auto source=Unit(fresh,id,team).id;
                    auto&s=MutableEntity(fresh,source);s.position={.5,home*7.5};s.facing={0,-home};
                    s.target=Tower(fresh,team==Team::Player?Team::Enemy:Team::Player,EntityKind::Guard,1).id;
                    s.hardLock=s.forcedTarget=0;
                    const double boundary=front?8:5;
                    const double distance=boundary+(inside?-1:1);
                    const Team foeTeam=team==Team::Player?Team::Enemy:Team::Player;
                    const std::string foeCard=structure?"archer_tower":"ironclad";
                    Check(fresh.Spawn(foeTeam,foeCard,{.5,home*7.5+(front?-home:home)*distance}),
                          "fresh deployed enemy fixture is physically accepted");
                    const auto&foe=Unit(fresh,foeCard,foeTeam);
                    const auto&after=ById(fresh,source);
                    const bool legalTarget=fresh.CanTarget(after,foe);
                    const bool detectable=fresh.InSight(after,foe);
                    Check(detectable==inside,"fresh fixture actually lands on intended side of detection boundary");
                    Check((after.target==foe.id)==(legalTarget&&detectable),
                          "fresh troop/building pulls obey the same actual8/5 gate for both teams");
                    ++pulls;
                }
            Check(pulls==32,"mirrored ordinary/structure-only fresh troop and building pull matrix");
        });
        Test("current patch Mini Stampede stays compact at legal boundaries and obstacles", [] {
            int fixtures=0,members=0;
            for(Team team:{Team::Player,Team::Enemy}){
                const double sign=team==Team::Player?1:-1;
                for(Vec2 drop:std::vector<Vec2>{{-.5,2.5},{.5,2.5},{5.5,2.5},{.5,6.5},{5.5,6.5},{5.5,9.5},{.5,14.5},{.5,6.5}}){
                    Match m(CollisionOptions("mini_stampede","archer_tower"));
                    const bool obstacle=fixtures%8==7;
                    if(obstacle)PaidCollisionCard(m,team,"archer_tower",{drop.x,sign*drop.z});
                    const auto units=PaidCollisionCard(m,team,"mini_stampede",{drop.x,sign*drop.z});
                    Check(units.size()==5,"compact paid formation retains all five hogs");
                    double x=0,z=0,maximum=0;int left=0;
                    for(const auto&unit:units){
                        x+=unit.position.x/5;z+=unit.position.z/5;
                        maximum=std::max(maximum,std::hypot(unit.position.x-drop.x,unit.position.z-sign*drop.z));
                        left+=unit.position.x<0;
                        Check(unit.lane==(unit.position.x<0?-1:1),"actual resolved landing side determines lane");
                    }
                    const bool clear=!obstacle&&drop.z<=6.5;
                    Check(maximum<=(clear?1.30:3.25),"no independent four-tile scattering from clear compact drop");
                    Check(std::hypot(x-drop.x,z-sign*drop.z)<=(clear?.20:2.0),"formation centroid remains anchored to hand drop");
                    if(clear&&std::abs(drop.x)<=.5)Check(left==2||left==3,"center placement still splits five hogs2/3");
                    ++fixtures;members+=int(units.size());
                }
            }
            Check(fixtures==16&&members==80,"mirrored river/center/side/tower/core/building compactness fixtures");
            std::cout<<"MINI_COMPACT fixtures="<<fixtures<<" paid_members="<<members<<" clear_max_radius=1.30 clear_centroid=0.20\n";
        });
        Test("current patch shockwave and independent status determinism across frame partitions", [] {
            Match a(Quiet()),b(Quiet());
            for(auto*m:{&a,&b}){
                FreezeCrowns(*m);
                m->Spawn(Team::Player,"pulverizer",{.5,8.5});m->Spawn(Team::Enemy,"boulderback",{.5,5.5});
                MutableEntity(*m,Unit(*m,"boulderback",Team::Enemy).id).stunUntil=20;
            }
            for(int step=0;step<180;++step){a.Step(1./60.);b.Step(1./120.);b.Step(1./120.);Same(a,b);}
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
            m.Step(.85);
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
            Check(!m.CanPlace(Team::Player, *c, {arena::GuardX, arena::GuardDepth}), "Tower overlap");
            m.Spawn(Team::Player, c->id, {7.5, 4.5});
            Check(!m.CanPlace(Team::Player, *c, {7.5, 5.5}), "building overlap");
            Check(m.CanPlace(Team::Player, *FindCard("nova_flask"), {0, -arena::LastTileZ + 1}), "spell any board region");
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
            m.Step(.85);
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
                const auto *dodgingId = enteringId;
                const Vec2 dodgePoint{7.5, card->id == "bullet_burst" ? 3.5 : 1.5};
                const Vec2 enterPoint{7.5, card->id == "bullet_burst" ? 8.5 : 10.5};
                Match dodge(Quiet()), enter(Quiet());
                FreezeCrowns(dodge);FreezeCrowns(enter);
                Check(dodge.Spawn(Team::Enemy, dodgingId, {7.5, 5.5}), "moving dodge target");
                Check(enter.Spawn(Team::Enemy, enteringId, {7.5, 5.5}), "moving enter target");
                const auto inside = [&](const Entity &entity, Vec2 point) {
                    return std::hypot(entity.position.x - point.x, entity.position.z - point.z) <=
                           card->spellRadius + entity.radius * .2;
                };
                Check(inside(Unit(dodge, dodgingId, Team::Enemy), dodgePoint), "dodger starts inside");
                Check(!inside(Unit(enter, enteringId, Team::Enemy), enterPoint), "entrant starts outside");
                Check(dodge.Spawn(Team::Player, id, dodgePoint) && enter.Spawn(Team::Player, id, enterPoint),
                      "fixed-area casts accepted");
                dodge.Step(card->castDelay);
                enter.Step(card->castDelay);
                Check(!inside(Unit(dodge, dodgingId, Team::Enemy), dodgePoint), "dodger leaves before impact");
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
            m.Spawn(Team::Player, "ironclad", {-arena::GuardX - 1.3, -arena::GuardDepth + 1.5});
            m.Step(.05);
            auto tower = Tower(m, Team::Enemy, EntityKind::Guard, -1).id;
            Check(Unit(m, "ironclad", Team::Player).hardLock == tower, "first real Crown attack commits lock");
            m.Spawn(Team::Enemy, "ironclad", {-arena::GuardX - 2.3, -arena::GuardDepth + 1.5});
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
        Test("Crown target remains pullable while waiting for its first attack", [] {
            for (Team team : {Team::Player, Team::Enemy}) {
                Match m(Quiet());
                FreezeCrowns(m);
                const double sign = team == Team::Player ? 1. : -1.;
                const Team other = team == Team::Player ? Team::Enemy : Team::Player;
                Check(m.Spawn(team, "ironclad", {arena::GuardX + 1.3, -sign * (arena::GuardDepth - 1.5)}), "waiting attacker spawns");
                const EntityId attacker = Unit(m, "ironclad", team).id;
                MutableEntity(m, attacker).cooldown = 1;
                m.Step(.05);
                Check(ById(m, attacker).target == Tower(m, other, EntityKind::Guard, 1).id &&
                          ById(m, attacker).hardLock == 0,
                      "being in Crown range does not commit a cooldown-blocked attack");
                for (const auto &event : m.Events())
                    Check(event.type != "attack" || event.source != attacker,
                          "fixture has not begun an actual attack");
                Check(m.Spawn(other, "ironclad", {arena::GuardX + 1.3, -sign * (arena::GuardDepth - 2.5)}), "nearby defender spawns");
                const EntityId defender = Unit(m, "ironclad", other).id;
                Check(ById(m, attacker).target == defender && ById(m, attacker).hardLock == 0,
                      "legal defender pulls an uncommitted Crown approach");
            }
        });
        Test("explicit target masks govern ground air and structures", [] {
            Entity ground, air, building, guard;
            air.flying = true;
            building.kind = EntityKind::Building;
            guard.kind = EntityKind::Guard;
            for (std::uint8_t mask = 1; mask <= targets::All; ++mask) {
                Card card;
                card.targetMask = mask;
                // Deliberately contradictory legacy projections cannot widen
                // an explicit mask used by primary or secondary attack hits.
                card.canHitAir = true;
                card.structuresOnly = true;
                card.building = true;
                Check(CanHit(card, ground) == bool(mask & targets::GroundTroop), "ground mask parity");
                Check(CanHit(card, air) == bool(mask & targets::AirTroop), "air mask parity");
                Check(CanHit(card, building) == bool(mask & targets::Structure), "building mask parity");
                Check(CanHit(card, guard) == bool(mask & targets::Structure), "Crown mask parity");
            }
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
            Near(Unit(m, "archer_tower", Team::Player).hp, 850 - 24.9 * 25, .0001, "explicit25HPpersecond");
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
            std::map<EntityId, bool> crossed;
            for (int lane : {-1, 1}) {
                Check(m.Spawn(Team::Player, "boulderback", {lane * 7.5, 5.5}), "Player heavy troop spawns");
                Check(m.Spawn(Team::Enemy, "boulderback", {lane * 7.5, -5.5}), "Enemy heavy troop spawns");
            }
            for (const auto &e : m.State().entities)
                if (e.kind == EntityKind::Troop) crossed[e.id] = false;
            for (int n = 0; n < 600; ++n) {
                const auto before = m.State().entities;
                m.Step(1. / 60);
                SweptBodyClearance(m, before, "opposite heavy bridge traffic");
                for (const auto &e : m.State().entities)
                    if (!e.dead && e.kind == EntityKind::Troop) {
                        const double sign = e.team == Team::Player ? 1. : -1.;
                        crossed[e.id] = crossed[e.id] || e.position.z * sign < -arena::RiverHalfWidth - .28;
                        ClearOwnStructures(m, e, e.position);
                    }
            }
            const bool allCrossed = crossed.size() == 4 &&
                std::all_of(crossed.begin(), crossed.end(), [](const auto &entry) { return entry.second; });
            if (!allCrossed)
                for (const auto &e : m.State().entities)
                    if (!e.dead && e.kind == EntityKind::Troop)
                        std::cerr << "BRIDGE_PROGRESS id=" << e.id << " team=" << int(e.team)
                                  << " position=" << e.position.x << ',' << e.position.z
                                  << " target=" << e.target << " bridge=" << e.bridge
                                  << " path=" << e.path.size() << " stuck=" << e.stuckTime << '\n';
            Check(allCrossed, "all four opposing heavy units pass on both bridges within ten seconds");
        });
        Test("22 by 40 arena preserves physical tower axes and validates every new grid edge", [] {
            Match m(Quiet());
            Check(arena::Width == 22 && arena::Height == 40, "confirmed 22 by 40 tile board");
            Near(arena::BridgeCenterX * 1.25, 9.7, 1e-12, "updated arena places physical bridge centers");
            Near(arena::GuardX, arena::BridgeCenterX, 0, "Guard axis shares the bridge center");
            int guardsAligned = 0, coresCentered = 0;
            for (Team team : {Team::Player, Team::Enemy}) {
                const double sign = team == Team::Player ? 1. : -1.;
                const auto &core = Tower(m, team, EntityKind::Core);
                Near(core.position.x, 0, 0, "Core remains centered between the bridge lanes");
                Near(core.position.z * 1.25, sign * 21.05, 1e-12, "updated arena places physical Core depth");
                Near(core.radius, 1.35, 0, "Core collision radius retained");
                ++coresCentered;
                for (int lane : {-1, 1}) {
                    const auto &guard = Tower(m, team, EntityKind::Guard, lane);
                    Near(guard.position.z * 1.25, sign * 17.15, 1e-12, "updated arena places physical Guard depth");
                    Near(guard.position.x * 1.25, lane * 9.7, 1e-12, "physical Guard and bridge axes stay aligned");
                    Near(guard.position.x, lane * arena::BridgeCenterX, 0, "actual Guard body follows the bridge axis");
                    Near(guard.radius, 1.15, 0, "Guard collision radius retained");
                    ++guardsAligned;
                }
                Near(core.position.x, (Tower(m, team, EntityKind::Guard, -1).position.x +
                                      Tower(m, team, EntityKind::Guard, 1).position.x) * .5, 0,
                     "Core stays centered between actual mirrored Guards");
                Check(m.CanPlace(team, *FindCard("ironclad"), {10.5, sign * 19.5}), "new outer corner accepts ordinary ground card");
                m.SetAether(team, 10);
                Check(m.Play(team, 0, {-10.5, sign * 19.5}), "new outer paid corner accepted");
                ClearOwnStructures(m, Unit(m, "ironclad", team), Unit(m, "ironclad", team).position);
                Check(m.Spawn(team, "sky_manta", {10.5, sign * 19.5}), "new outer DEV corner accepted");
            }
            Near(SnapToTile({11, 20}).x, 10.5, 0, "positive X edge snaps to the last center");
            Near(SnapToTile({11, 20}).z, 19.5, 0, "positive Z edge snaps to the last center");
            Near(SnapToTile({-11, -20}).x, -10.5, 0, "negative X edge snaps to the last center");
            Near(SnapToTile({-11, -20}).z, -19.5, 0, "negative Z edge snaps to the last center");
            Check(!m.Spawn(Team::Player, "ironclad", {11.001, 10}), "outside new width rejected");
            Check(!m.Spawn(Team::Enemy, "ironclad", {0, -20.001}), "outside new depth rejected");
            Near(arena::PocketOuterX, 10.2, 1e-12, "pocket outer edge derives from new width");
            Near(arena::PocketMaxDepth, 10.57, 1e-12, "pocket depth derives from adjusted Guard");
            int tileCenters = 0, edgeFixtures = 0;
            std::set<double> centersX, centersZ;
            for (int row = 0; row < 40; ++row)
                for (int column = 0; column < 22; ++column) {
                    const Vec2 point{-10.5 + column, -19.5 + row};
                    const Vec2 snapped = SnapToTile(point);
                    Near(snapped.x, point.x, 0, "all grid centers retain exact X");
                    Near(snapped.z, point.z, 0, "all grid centers retain exact Z");
                    centersX.insert(snapped.x); centersZ.insert(snapped.z); ++tileCenters;
                }
            Check(tileCenters == 880 && centersX.size() == 22 && centersZ.size() == 40,
                  "exactly 22 across and 40 Core-to-Core grid centers");
            for (const auto &card : Cards())
                for (Team team : {Team::Player, Team::Enemy})
                    for (int side : {-1, 1}) {
                        Match edge(CollisionOptions(card.id, "ironclad"));
                        const double sign = team == Team::Player ? 1. : -1.;
                        if (card.building)
                            Check(!edge.CanPlace(team, card, {side * 10.5, sign * 19.5}),
                                  "a building footprint cannot extend beyond the last corner tile");
                        const double inset = card.building ? 1 : 0;
                        PaidCollisionCard(edge, team, card.id, {side * (10.5 - inset), sign * (19.5 - inset)});
                        BodyClearance(edge, "new arena paid outer corner");
                        for (const auto &body : edge.State().entities)
                            if (!body.dead && body.kind < EntityKind::Guard) {
                                const double margin = .40 + body.radius * .72;
                                Check(std::abs(body.position.x) <= 11 - margin + 1e-8 &&
                                          std::abs(body.position.z) <= 20 - margin + 1e-8,
                                      "every paid edge member remains inside its full body bounds");
                            }
                        ++edgeFixtures;
                    }
            Check(edgeFixtures == int(Cards().size()) * 4, "all cards mirror new paid corners for both teams");
            Check(guardsAligned == 4 && coresCentered == 2, "every actual tower has the requested lane axis");
            std::cout << "TOWER_ALIGNMENT width=" << arena::Width << " height=" << arena::Height
                      << " guard_x=" << arena::GuardX << " bridge_x=" << arena::BridgeCenterX
                      << " guard_depth=" << arena::GuardDepth << " core_depth=" << arena::CoreDepth
                      << " guards=" << guardsAligned << " cores=" << coresCentered << " aligned=1\n";
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
            Check(fixtures == 132 && members == 360, "all eleven ground cards, both teams, both lane states and pockets");
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
                        source.radius = card.collisionRadius;
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
            Check(routes == 44, "all eleven ground cards mirror the current-side route policy");
        });
        Test("paid swarms split from each resolved landing side and advance through their own lane", [] {
            int fixtures = 0, members = 0, splitFixtures = 0;
            for (const auto *id : {"twin_blades", "vampire_bats", "mini_stampede", "stampede"})
                for (const Team team : {Team::Player, Team::Enemy})
                    for (double x : {-7.5, -.5, .5, 7.5})
                        for (int destroyedLane : {-1, 0, 1}) {
                            Match m(CollisionOptions(id, "ironclad"));
                            const double sign = team == Team::Player ? 1. : -1.;
                            const Team other = team == Team::Player ? Team::Enemy : Team::Player;
                            const EntityId left = Tower(m, other, EntityKind::Guard, -1).id;
                            const EntityId right = Tower(m, other, EntityKind::Guard, 1).id;
                            const EntityId core = Tower(m, other, EntityKind::Core).id;
                            if (destroyedLane)
                                Check(m.SetTowerHP(destroyedLane < 0 ? left : right, 0),
                                      "swarm fixture destroys exactly one opposing Guard");
                            const auto landed = PaidCollisionCard(m, team, id, {x, sign * 8.5});
                            ++collisionPaidFixtures;
                            BodyClearance(m, "resolved swarm formation");
                            int leftMembers = 0, rightMembers = 0;
                            for (const auto &body : landed) {
                                const int lane = body.position.x < 0 ? -1 : 1;
                                Check(body.lane == lane, "each member stores its actual resolved landing side");
                                Check(m.CanPlace(team, *FindCard(id), body.position),
                                      "every member remains in a legal paid deployment zone");
                                if (lane < 0) ++leftMembers; else ++rightMembers;
                            }
                            if (std::abs(x) < 1.) {
                                Check(leftMembers > 0 && rightMembers > 0,
                                      "both center-adjacent tiles split every multi-unit card across the divider");
                                Check(std::abs(leftMembers - rightMembers) <=
                                          (std::string(id) == "stampede" ? 3 : 1),
                                      "members split by landing side, including Stampede's three-member center rank");
                                ++splitFixtures;
                            } else
                                Check(x < 0 ? rightMembers == 0 : leftMembers == 0,
                                      "clear side placements retain the whole swarm on that side");
                            m.Step(1. / 60.);
                            for (const auto &body : landed) {
                                const auto &current = ById(m, body.id);
                                const EntityId guard = body.lane < 0 ? left : right;
                                Check(current.target == (body.lane == destroyedLane ? core : guard),
                                      "each resolved side chooses its Guard or exposed Core, including flying swarms");
                                if (!body.flying)
                                    Check(current.bridge == body.lane, "each grounded member commits to its own bridge");
                            }
                            if (std::abs(x) < 1.) {
                                std::vector<bool> crossed(landed.size(), false);
                                for (int step = 0; step < 720; ++step) {
                                    const auto before = m.State().entities;
                                    m.Step(1. / 60.);
                                    SweptBodyClearance(m, before, "split swarm route");
                                    for (std::size_t n = 0; n < landed.size(); ++n) {
                                        const auto &original = landed[n];
                                        const auto *body = FindEntity(m, original.id);
                                        if (!body || body->dead)
                                            continue;
                                        const EntityId opposite = original.lane < 0 ? right : left;
                                        Check(body->target != opposite,
                                              "opposite standing Guard cannot steal the split member's lane");
                                        if (!body->flying && std::abs(body->position.z) < arena::RiverHalfWidth + .28) {
                                            Check(body->position.x * original.lane > 0 && body->bridge == original.lane,
                                                  "grounded split member traverses the bridge selected by landing X");
                                        }
                                        crossed[n] = crossed[n] || body->position.z * sign < -arena::RiverHalfWidth - .28;
                                    }
                                }
                                Check(std::all_of(crossed.begin(), crossed.end(), [](bool crossed) { return crossed; }),
                                      "every split swarm member reaches the opposing bank within twelve seconds");
                            }
                            ++fixtures;
                            members += static_cast<int>(landed.size());
                        }
            Check(fixtures == 96 && splitFixtures == 48 && members == 648,
                  "both teams, all four multi-unit cards, both center tiles, both side tiles and both destroyed lanes");
            std::cout << "SWARM_SPLIT fixtures=" << fixtures << " center_fixtures=" << splitFixtures
                      << " paid_members=" << members << " all_crossed=1\n";
        });
        Test("fifteen-member paid formations preserve economy identities and deterministic event positions", [] {
            const auto options = CollisionOptions("stampede", "mini_stampede");
            Match a(options), b(options);
            for (Match *m : {&a, &b}) {
                for (const Team team : {Team::Player, Team::Enemy}) {
                    const double sign = team == Team::Player ? 1. : -1.;
                    const auto royal = PaidCollisionCard(*m, team, "stampede", {0, sign * 8.5});
                    const auto wild = PaidCollisionCard(*m, team, "mini_stampede", {0, sign * 8.5});
                    Check(royal.size() == 15 && wild.size() == 5, "both paid casts preserve their complete swarm");
                    Near(m->State().spent[int(team)], 9, 0, "both swarms cost nine Aether total, paid once each");
                    Check(m->State().telemetry[int(team)].at("stampede").plays == 1 &&
                              m->State().telemetry[int(team)].at("stampede").spawns == 15 &&
                              m->State().telemetry[int(team)].at("mini_stampede").plays == 1 &&
                              m->State().telemetry[int(team)].at("mini_stampede").spawns == 5,
                          "telemetry counts one play and every physical member");
                    for (std::size_t n = 1; n < royal.size(); ++n)
                        Check(royal[n].id == royal[n - 1].id + 1 && royal[n].playId == royal[0].playId,
                              "large swarm reserves contiguous entity identities under one paid play");
                    BodyClearance(*m, "consecutive centered hog swarm casts");
                }
                ++collisionPaidFixtures;
            }
            Same(a, b);
            a.Step(8);
            for (int tick = 0; tick < 960; ++tick)
                b.Step(1. / 120.);
            Same(a, b);
            for (std::size_t n = 0; n < a.Events().size(); ++n) {
                const auto &left = a.Events()[n], &right = b.Events()[n];
                Check(left.source == right.source && left.count == right.count,
                      "swarm replay records retain every source identity and member count");
                Near(left.position.x, right.position.x, 1e-9, "swarm replay event X is deterministic");
                Near(left.position.z, right.position.z, 1e-9, "swarm replay event Z is deterministic");
            }
        });
        Test("Stampede uses five columns and three ranks at clear ordinary and river-edge drops", [] {
            int fixtures = 0;
            for (Team team : {Team::Player, Team::Enemy})
                for (double x : {-7.5, -.5, .5, 7.5})
                    for (double z : {2.5, 6.5}) {
                        Match m(CollisionOptions("stampede", "ironclad"));
                        const double sign = team == Team::Player ? 1. : -1.;
                        const auto members = PaidCollisionCard(m, team, "stampede", {x, sign * z});
                        Check(members.size() == 15, "all fifteen paid bodies land");
                        std::map<double, int> rows, columns;
                        for (const auto &member : members) {
                            ++rows[member.position.z * sign];
                            ++columns[member.position.x];
                            Check(m.CanPlace(team, *FindCard("stampede"), member.position),
                                  "whole grid remains inside legal deployment region");
                        }
                        Check(rows.size() == 3 && columns.size() == 5, "five across and three deep");
                        for (const auto &[position, count] : rows) {
                            (void)position;
                            Check(count == 5, "each rank contains exactly five members");
                        }
                        for (const auto &[position, count] : columns) {
                            (void)position;
                            Check(count == 3, "each column contains exactly three members");
                        }
                        if (z == 2.5)
                            Near(rows.begin()->first, 2.15, 1e-12,
                                 "river-edge grid moves together to first legal front row");
                        BodyClearance(m, "five-by-three paid landing");
                        ++fixtures;
                    }
            Check(fixtures == 16, "both teams both banks and center/side placements inspected");
            std::cout << "STAMPEDE_FORMATION fixtures=" << fixtures << " columns=5 ranks=3 members=240\n";
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
            Check(fixtures == 132 && members == 360, "all eleven ground cards and every swarm member in the tower matrix");
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
                            source.radius = FindCard(id)->collisionRadius;
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
            const std::vector<Vec2> points{{.5, arena::LastTileZ}, {-arena::DeploymentMaxX, arena::LastTileZ},
                                           {arena::DeploymentMaxX, arena::LastTileZ}, {-arena::LastTileX, arena::LastTileZ},
                                           {arena::LastTileX, arena::LastTileZ}, {-arena::DeploymentMaxX, 8.5}, {arena::DeploymentMaxX, 8.5}};
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
                        FreezeCrowns(m);
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
            Check(fixtures == 154 && members == 420, "all eleven ground cards mirrored across rear/side/corner cases");
        });
        Test("legal ground drops air spells buildings and DEV river fixtures retain their positions", [] {
            for (Team team : {Team::Player, Team::Enemy}) {
                const double sign = team == Team::Player ? 1. : -1.;
                const double coreTile = SnapToTile({0, arena::CoreDepth}).z;
                Match m(Quiet());
                Check(m.Spawn(team, "ironclad", {7.5, sign * 5.5}), "legal DEV ground drop");
                Near(Unit(m, "ironclad", team).position.x, 7.5, 0, "legal ground X unchanged");
                Near(Unit(m, "ironclad", team).position.z, sign * 5.5, 0, "legal ground Z unchanged");
                Check(m.Spawn(team, "sky_manta", {.5, sign * coreTile}), "flying tower-center fixture");
                Near(Unit(m, "sky_manta", team).position.x, .5, 0, "air X unchanged");
                Near(Unit(m, "sky_manta", team).position.z, sign * coreTile, 0, "air can still fly over towers");
                Check(m.Spawn(team, "archer_tower", {.5, sign * 8.5}), "existing legal DEV building fixture");
                Near(Unit(m, "archer_tower", team).position.x, .5, 0, "building X unchanged");
                Near(Unit(m, "archer_tower", team).position.z, sign * 8.5, 0, "legal fixed building position unchanged");
                Check(m.Spawn(team, "meteor_shards", {.5, sign * coreTile}), "existing fixed spell fixture");
                Near(m.State().spellCasts.back().position.x, .5, 0, "spell area X unchanged");
                Near(m.State().spellCasts.back().position.z, sign * coreTile, 0, "spell area Z unchanged");
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
                    Check(m->Play(team, 0, {.5, sign * (arena::CoreDepth + .5)}), "paid original Core repro");
                    Check(m->Spawn(team, "twin_blades", {arena::GuardX + 1.3, sign * arena::GuardDepth}), "Guard swarm repro");
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
                    Check(d == BuildAIDeck(s, 100 + n), "identical seed retains identical deck and hand order");
                    for (const auto &id : d)
                        Check(FindCard(id) != nullptr, "every generated slot is a playable card");
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
            for (const std::string &style :
                 {"beatdown", "aggro", "control", "cycle", "split", "spell_cycle", "counter"}) {
                const auto diagnostics = AIDeckCacheDiagnostics(style);
                Check(diagnostics.evaluated > 0 && diagnostics.evaluated <= AIDeckCandidateLimit &&
                          diagnostics.legal >= diagnostics.retained && diagnostics.retained > 0 &&
                          diagnostics.retained <= 24 && diagnostics.hits >= 8,
                      "cache reports actual bounded evaluations legal pools retained pools and reuse");
                Check(std::isfinite(diagnostics.buildMilliseconds) && diagnostics.buildMilliseconds >= 0,
                      "reported construction latency is finite measured time");
                std::cout << "AI_CACHE style=" << style << " evaluated=" << diagnostics.evaluated
                          << " legal=" << diagnostics.legal << " retained=" << diagnostics.retained
                          << " hits=" << diagnostics.hits << " build_ms=" << diagnostics.buildMilliseconds << '\n';
            }
        });
        Test("AI candidate coverage is deterministic bounded and independent of bit-mask width", [] {
            for (const std::size_t roster : {7U, 8U, 9U, 17U, 18U, 21U, 32U, 40U, 64U, 128U}) {
                const auto began = std::chrono::steady_clock::now();
                const auto candidates = AIDeckCandidateIndices(roster);
                const auto repeated = AIDeckCandidateIndices(roster);
                Check(candidates == repeated, "candidate order is deterministic without match RNG");
                Check(candidates.size() <= AIDeckCandidateLimit, "evaluations stay within practical cap");
                std::set<AIDeckIndices> unique;
                std::vector<bool> covered(roster, false);
                for (const auto &candidate : candidates) {
                    Check(unique.insert(candidate).second, "each candidate subset is unique");
                    for (std::size_t slot = 0; slot < candidate.size(); ++slot) {
                        Check(candidate[slot] < roster && (slot == 0 || candidate[slot] > candidate[slot - 1]),
                              "eight legal distinct playable indices without shift overflow");
                        covered[candidate[slot]] = true;
                    }
                }
                if (roster < 8)
                    Check(candidates.empty(), "undersized roster cannot produce an eight-card deck");
                else
                    Check(std::all_of(covered.begin(), covered.end(), [](bool value) { return value; }),
                          "every card including high indices can enter a candidate deck");
                if (roster == 8) Check(candidates.size() == 1, "single eight-card combination");
                if (roster == 9) Check(candidates.size() == 9, "all nine eight-card combinations");
                if (roster >= 17) Check(candidates.size() == AIDeckCandidateLimit,
                                       "baseline exhaustiveness and expansion sample count");
                const double milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - began).count();
                std::cout << "AI_CANDIDATES roster=" << roster << " candidates=" << candidates.size()
                          << " repeated_and_checked_ms=" << milliseconds << '\n';
            }
        });
        Test("AI counter reasoning obeys explicit masks with a separate troop-only aura", [] {
            Card attack = *FindCard("ironclad");
            attack.targetMask = targets::GroundTroop;
            attack.canHitAir = true;
            Near(CounterScore(attack, *FindCard("vampire_bats")), 0, 0, "mask excludes air despite legacy flag");
            Near(CounterScore(attack, *FindCard("archer_tower")), 0, 0, "troop-only mask excludes structures");
            Check(CounterScore(attack, *FindCard("ironclad")) > 0, "ground counter remains eligible");
            attack.targetMask = targets::AirTroop;
            attack.canHitAir = false;
            Check(CounterScore(attack, *FindCard("vampire_bats")) > 0, "air mask admits flying troops");
            Near(CounterScore(attack, *FindCard("ironclad")), 0, 0, "air-only mask excludes ground");
            Card spell = *FindCard("bullet_burst");
            spell.targetMask = targets::GroundTroop;
            Near(CounterScore(spell, *FindCard("vampire_bats")), 0, 0, "spell mask excludes air");
            Near(CounterScore(spell, *FindCard("archer_tower")), 0, 0, "spell mask excludes structure damage");
            const auto &raven = *FindCard("storm_raven");
            Check(CounterScore(raven, *FindCard("vampire_bats")) > 0 &&
                      CounterScore(raven, *FindCard("ironclad")) > 0,
                  "Raven's separate aura still counters ground and air troops");
        });
        Test("AI ground-only building defense distinguishes air swarms from eligible ground pressure", [] {
            for (const std::string &enemy : {"vampire_bats", "boulderback"}) {
                auto options = Quiet();
                options.aiEnabled[0] = true;
                options.decks[0] = {"pulverizer", "boulderback", "rambeast", "stampede",
                                    "ironclad", "archer_tower", "sky_manta", "ember_archer"};
                Match match(options);
                FreezeCrowns(match);
                match.SetAether(Team::Player, 10);
                Check(match.Spawn(Team::Enemy, enemy, {7.5, 5.5}), "real enemy pressure spawns");
                match.Step(.45);
                bool buildingDefense = false;
                for (const auto &event : match.Events())
                    if (event.type == "card_play" && event.team == Team::Player) {
                        std::cout << "AI_MASK_DEFENSE enemy=" << enemy << " played=" << event.cardId
                                  << " reason=" << event.reason << '\n';
                        buildingDefense |= event.cardId == "pulverizer" && event.reason == "building_defense";
                    }
                Check(buildingDefense == (enemy == "boulderback"),
                      "AI uses Pulverizer for ground tank pressure and does not pretend it shoots bats");
            }
        });
        Test("expanded deck analysis separates shield and future child reserves from initial bodies", [] {
            const auto deck = AnalyzeDeck({"bone_warden", "iron_giant", "shadow_surge", "bone_warden",
                                           "bone_warden_skeleton", "unknown"});
            Check(deck.troops == 2 && deck.spells == 1 && deck.ground == 2,
                  "only unique playable cards enter a deck analysis");
            Near(deck.averageHP, (1526. + 1000.) / 2, 0, "initial paid body HP excludes shield and later children");
            Near(deck.deploymentDPS, (101. / 1.5 + 85. / 1.3) / 2, .0051,
                 "initial deployment DPS excludes not-yet-born children");
            Near(deck.totalShieldHP, 310, 0, "shield reserve reported independently");
            Near(deck.deathSpawnHP, 4 * 50, 0, "four child bodies reported independently");
            Near(deck.deathSpawnDPS, 4 * 36 / .75, 0, "future child DPS reported independently");
            Card packet = *FindCard("nova_flask"), protectedBody = *FindCard("iron_giant");
            packet.damage = 400; packet.dotDamage = 0; packet.dotDuration = 0;
            Card overflowBody = protectedBody; overflowBody.shieldOverflowProtected = false;
            Check(CounterScore(packet, protectedBody) < CounterScore(packet, overflowBody),
                  "a shield-breaking protected packet cannot be valued as body overflow");
            Check(CounterReason(packet, protectedBody).find("cannot overflow") != std::string::npos,
                  "shield counter explanation matches the actual packet rule");
            Card noChildren = *FindCard("bone_warden"); noChildren.deathSpawnCount = 0;
            Check(CounterScore(*FindCard("ember_archer"), *FindCard("bone_warden")) <=
                      CounterScore(*FindCard("ember_archer"), noChildren),
                  "future child HP cannot make the parent cheaper to remove");
            Check(CounterScore(*FindCard("pulverizer"), *FindCard("iron_giant")) > 0,
                  "Pulverizer notices eligible ground troops including shielded Giant");
            Near(CounterScore(*FindCard("pulverizer"), *FindCard("archer_tower")), 0, 0,
                 "Pulverizer never treats a ground building as a ground troop");
        });
        Test("expanded paid mechanics and individual telemetry are deterministic across frame chunks", [] {
            auto options = Quiet(); options.seed = 20261011;
            for (auto &deck : options.decks)
                deck = {"bone_warden", "iron_giant", "shadow_surge", "archer_tower",
                        "meteor_shards", "ember_archer", "rambeast", "frost_fang"};
            Match a(options), b(options);
            for (Match *match : {&a, &b}) {
                FreezeCrowns(*match);
                const auto parent = PaidCollisionCard(*match, Team::Player, "bone_warden", {-3.5, 7.5})[0];
                MutableEntity(*match, parent.id).hp = 1;
                PaidCollisionCard(*match, Team::Player, "iron_giant", {3.5, 7.5});
                PaidCollisionCard(*match, Team::Player, "archer_tower", {.5, 4.5});
                PaidCollisionCard(*match, Team::Player, "shadow_surge", {3.5, 7.5});
                Check(match->Spawn(Team::Enemy, "bullet_burst", {-3.5, 7.5}), "controlled real child-spawn packet");
                Check(match->Spawn(Team::Enemy, "nova_flask", {3.5, 7.5}), "controlled real shield-breaking packet");
            }
            a.Step(2);
            for (int tick = 0; tick < 240; ++tick) b.Step(1. / 120.);
            Same(a, b);
            Check(Count(a, "bone_warden_skeleton", Team::Player) == 4, "all four real child bodies were compared");
            Check(Unit(a, "iron_giant", Team::Player).shieldBrokenAt >= 0, "actual shield transition was compared");
            int cloaks = 0, expiries = 0;
            for (const auto &event : a.Events()) {
                cloaks += event.type == "invisibility_apply";
                expiries += event.type == "invisibility_expire";
            }
            Check(cloaks > 0 && expiries > 0, "actual local cloak application and expiry were compared");
            BodyClearance(a, "expanded fixed-step final bodies");
        });
        Test("cloak AI values future enemy locks and excludes already committed or stunned damage", [] {
            for (const std::string mode : {"future_lock", "no_lock", "outside_window", "committed_only", "stunned", "short_stun"}) {
                auto options = Quiet(); options.seed = 71217;
                options.aiStyles[0] = "control";
                options.decks[0] = {"shadow_surge", "bone_warden", "stampede", "meteor_shards",
                                    "storm_raven", "frost_fang", "vampire_bats", "boulderback"};
                Match match(options); FreezeCrowns(match);
                Check(match.Spawn(Team::Player, "ironclad", {.5, 5.5}), "visible friendly body");
                Check(match.Spawn(Team::Enemy, "ember_archer", {.5, 2.5}), "visible enemy with direct target capability");
                const auto friendly = Unit(match, "ironclad", Team::Player).id;
                const auto enemy = Unit(match, "ember_archer", Team::Enemy).id;
                MutableEntity(match, friendly).hp = 100;
                MutableEntity(match, friendly).stunUntil = 30;
                auto &attacker = MutableEntity(match, enemy);
                attacker.target = mode == "no_lock" || mode == "committed_only" ? 0 : friendly;
                attacker.cooldown = mode == "outside_window" || mode == "committed_only" ? 3 : .6;
                if (mode == "stunned") attacker.stunUntil = 30;
                if (mode == "short_stun") {attacker.stunUntil = .6; attacker.cooldown = .1;}
                if (mode == "committed_only") {
                    Projectile projectile; projectile.id = 90000; projectile.source = enemy; projectile.target = friendly;
                    projectile.team = Team::Enemy; projectile.cardId = "ember_archer";
                    projectile.damage = 152; projectile.remaining = .6;
                    const_cast<Snapshot &>(match.State()).projectiles.push_back(projectile);
                }
                match.SetAether(Team::Player, 3);
                match.SetAIEnabled(Team::Player, true);
                const_cast<Snapshot &>(match.State()).ai[0].think = 0;
                match.Step(1. / 60.);
                bool cloakDefense = false;
                for (const auto &event : match.Events())
                    if (event.type == "card_play" && event.team == Team::Player && event.reason == "cloak_defense") {
                        cloakDefense = true;
                        Check(event.cardId == "shadow_surge" && !event.sandbox, "AI cloak uses a real paid card");
                        Near(event.amount, 3, 0, "AI cloak pays full Aether cost");
                    }
                std::cout << "AI_CLOAK mode=" << mode << " paid_cloak_defense=" << cloakDefense << '\n';
                Check(cloakDefense == (mode == "future_lock" || mode == "short_stun"),
                      "cloak prevents an actual future lock attack, not committed or disabled packets: " + mode);
            }
        });
        Test("approved paid performance opening packs all eight cards on both teams and bridge sides", [] {
            auto options = Quiet(); options.seed = 74021;
            const std::vector<std::string> requested{"pulverizer", "stampede", "mini_stampede", "bone_warden",
                                                      "aether_reaper", "iron_giant", "shadow_surge", "nova_flask"};
            options.decks = {requested, requested};
            Match match(options);
            int plays = 0, bodies = 0, stampede = 0, mini = 0;
            for (const Team team : {Team::Player, Team::Enemy})
                for (int lane : {-1, 1})
                    for (const auto &id : requested) {
                        const double sign = team == Team::Player ? 1 : -1;
                        const auto *card = FindCard(id);
                        Vec2 point{lane * arena::BridgeCenterX, 0};
                        if (id == "pulverizer") point.z = sign * 3.5;
                        if (id == "stampede") point.z = sign * 6.5;
                        if (id == "mini_stampede" || id == "aether_reaper" || id == "shadow_surge") point.z = sign * 4.5;
                        if (id == "bone_warden") point = {lane * (arena::HalfWidth - 3.5), sign * 5.5};
                        if (id == "iron_giant") point = {lane * (arena::BridgeCenterX - 2.8), sign * 8};
                        int hand = -1;
                        for (int slot = 0; slot < 4; ++slot)
                            if (match.State().hands[int(team)][slot] == id) hand = slot;
                        Check(hand >= 0, "real hand rotates to every requested fixture card");
                        const Vec2 offsets[]{{0, 0}, {3. * lane, 0}, {-3. * lane, 0}, {0, 3 * sign},
                                             {0, 6 * sign}, {0, 9 * sign}, {-3. * lane, 6 * sign}};
                        match.SetAether(team, 10);
                        const double before = match.State().spent[int(team)];
                        const auto sequence = match.Events().empty() ? 0 : match.Events().back().sequence;
                        bool accepted = false;
                        for (const auto offset : offsets) {
                            const Vec2 candidate{point.x + offset.x, point.z + offset.z};
                            if (!card->spell && candidate.x * lane <= 0) continue;
                            if (match.Play(team, hand, candidate, "performance_expansion_paid_fixture")) {
                                accepted = true; break;
                            }
                            if (card->spell) break;
                        }
                        Check(accepted, "real expanded paid fixture placement accepted for " + id);
                        Near(match.State().spent[int(team)] - before, card->cost, 0, "fixture actually pays canonical cost");
                        int members = 0;
                        for (const auto &event : match.Events())
                            if (event.sequence > sequence && event.type == "entity_spawn" && event.cardId == id && event.team == team)
                                ++members;
                        Check(members == (card->spell ? 0 : card->count), "exact real fixture member count");
                        bodies += members; stampede += id == "stampede" ? members : 0; mini += id == "mini_stampede" ? members : 0;
                        ++plays;
                        BodyClearance(match, "approved fixture opening");
                    }
            Check(plays == 32 && bodies == 96 && stampede == 60 && mini == 20,
                  "independent approved opening counts are32plays96bodies60hogs20mini");
            Near(match.State().spent[0] + match.State().spent[1], 128, 0, "exact canonical paid opening Aether");
            std::cout << "EXPANSION_PERF_OPENING paid_plays=" << plays << " bodies=" << bodies
                      << " stampede=" << stampede << " mini=" << mini << " spent=128\n";
        });
        Test("mechanical counters and all symmetric card pairs", [] {
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
            Check(pairs == 210, "all twenty-one-card pairs");
            auto d = AnalyzeDeck({"vampire_bats", "twin_blades", "nova_flask"});
            Near(d.averageHP, (174 * 5 + 262 * 2) / 2., 0, "swarm aggregate spell excluded");
            Near(d.deploymentDPS, (86 / 1.05 * 5 + 55 / .75 * 2) / 2., .0051, "aggregate DPS spell excluded");
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
            m.Step(.85);
            std::uint64_t previous = 0;
            bool damage = false;
            for (const auto &e : m.Events()) {
                Check(e.sequence > previous, "ordered sequence");
                previous = e.sequence;
                if (e.type == "damage") {
                    damage = true;
                    Check(e.playId > 0 && e.amount == 375 && e.requested == 375 && e.hp == 447,
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
            ram.Spawn(Team::Player, "rambeast", {arena::BridgeCenterX, -arena::GuardDepth + 8});
            ram.Step(5);
            bool charged = false;
            for (const auto &e : ram.Events())
                if (e.type == "damage" && e.cardId == "rambeast" && e.targetKind == EntityKind::Guard &&
                    e.requested == 302)
                    charged = true;
            Check(charged, "running approach total charged302");
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
                        Check(std::abs(e.position.x - arena::BridgeCenterX) < 1.94 || std::abs(e.position.x + arena::BridgeCenterX) < 1.94,
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
            Check(collisionPaidFixtures - fixtureStart == 34 && collisionPaidPlays - playStart == 68 &&
                      collisionMembers - memberStart == 122,
                  "all seventeen physical cards, both teams, sixty-eight paid casts and122 actual members");
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
        Test("direct melee reach uses body edges while ranged reach remains unchanged", [] {
            Match m(Quiet());
            Entity source, target;
            source.kind = EntityKind::Troop;source.team = Team::Player;
            target.kind = EntityKind::Guard;target.team = Team::Enemy;target.radius = 1.15;
            for (const auto &card : Cards()) {
                if (card.spell || card.building)
                    continue;
                source.cardId = card.id;source.radius = card.collisionRadius;source.flying = card.flying;
                Near(m.AttackReach(source, target), card.range + target.radius +
                    (card.projectileSpeed <= 0 ? source.radius : 0.), 1e-12,
                    "only direct melee adds the actual source body radius " + card.id);
                Near(FindCard(card.id)->range, card.range, 0, "melee correction changes no authored card range");
            }
            source.cardId = "pulverizer";source.kind = EntityKind::Building;source.radius = .858;
            Near(m.AttackReach(source, target), 4 + 1.15 + .35, 1e-12, "stationary building keeps center reach");
            source.kind = EntityKind::Guard;
            Near(m.AttackReach(source, target), 10 + 1.15, 1e-12, "Guard keeps center reach");
            source.kind = EntityKind::Core;
            Near(m.AttackReach(source, target), 8.9 + 1.15, 1e-12, "Core keeps center reach");
        });
        Test("paid hog swarms wrap free target arcs and all members attack together", [] {
            int fixtures = 0, members = 0;
            int slowContactFixtures = 0;
            for (const auto *cardId : {"mini_stampede", "stampede"})
                for (const Team team : {Team::Player, Team::Enemy})
                    for (const int lane : {-1, 1})
                        for (const EntityKind kind : {EntityKind::Guard, EntityKind::Core, EntityKind::Building}) {
                            Match m(CollisionOptions(cardId, "archer_tower"));FreezeCrowns(m);
                            const int sign = team == Team::Player ? 1 : -1;
                            const Team opponent = team == Team::Player ? Team::Enemy : Team::Player;
                            EntityId targetId = 0;
                            if (kind == EntityKind::Building) {
                                targetId = PaidCollisionCard(m, opponent, "archer_tower", {lane * 7.5, -sign * 5.5})[0].id;
                                MutableEntity(m, targetId).stunUntil = 100;
                            } else {
                                if (kind == EntityKind::Core)
                                    Check(m.SetTowerHP(Tower(m, opponent, EntityKind::Guard, lane).id, 0), "real Guard destruction exposes Core lane");
                                targetId = Tower(m, opponent, kind, kind == EntityKind::Guard ? lane : 0).id;
                            }
                            const auto swarm = PaidCollisionCard(m, team, cardId, {lane * 7.5, sign * 5.5});
                            const auto *card = FindCard(cardId);
                            Check(swarm.size() == std::size_t(card->count), "contact fixture keeps complete actual paid swarm");
                            const auto oppositeGuard = Tower(m, opponent, EntityKind::Guard, -lane).id;
                            std::map<EntityId, double> lastHit;
                            std::map<EntityId, double> firstHit;
                            std::map<EntityId, double> approachEntry;
                            std::map<EntityId, double> approachTravel, stationaryClock, stationaryMax;
                            std::map<EntityId, double> windowAt, netWait, netWaitMax;
                            std::map<EntityId, Vec2> windowPosition;
                            std::size_t eventCursor = m.Events().size();
                            int peakConcurrent = 0, togetherFrames = 0;
                            double firstTogether = -1;
                            for (int step = 0; step < 24 * 60; ++step) {
                                // Declared diagnostic HP maintenance keeps one
                                // real target alive long enough to observe arrival.
                                // Costs, movement, attack intervals and body sizes
                                // remain the exact canonical paid game definitions.
                                auto &target = MutableEntity(m, targetId);
                                Check(!target.dead, "contact target remains live before maintenance");
                                if (kind == EntityKind::Building) target.hp = target.maxHp;
                                else Check(m.SetTowerHP(targetId, target.maxHp), "declared diagnostic crown HP maintenance accepted");
                                const auto before = m.State().entities;m.Step(1. / 60.);
                                SweptBodyClearance(m, before, "swarm target arc arrival");
                                for (; eventCursor < m.Events().size(); ++eventCursor) {
                                    const auto &event = m.Events()[eventCursor];
                                    if (event.type == "damage" && event.target == targetId && event.cardId == cardId) {
                                        Near(event.requested, card->damage, 0, "every swarm hit retains authored per-member damage");
                                        lastHit[event.source] = event.time;
                                        firstHit.try_emplace(event.source, event.time);
                                    }
                                }
                                int concurrent = 0, recentlyHit = 0;
                                for (const auto &start : swarm) {
                                    const auto &body = ById(m, start.id);
                                    Check(!body.dead && body.target != oppositeGuard, "contact movement keeps each living member and its original bridge lane target");
                                    Near(body.radius, card->collisionRadius, 0, "contact movement keeps the authored body radius");
                                    if (body.target == targetId && body.position.z * ById(m, targetId).position.z >= 0 &&
                                        m.CombatDistance(body, ById(m, targetId)) <=
                                            card->range + body.radius + ById(m, targetId).radius + 4.)
                                        approachEntry.try_emplace(body.id, m.State().elapsed);
                                    if (approachEntry.count(body.id) && !firstHit.count(body.id)) {
                                        const auto prior = std::find_if(before.begin(), before.end(), [&](const Entity &old) {return old.id == body.id;});
                                        const double travel = std::hypot(body.position.x - prior->position.x, body.position.z - prior->position.z);
                                        approachTravel[body.id] += travel;
                                        stationaryClock[body.id] = travel < .0001 ? stationaryClock[body.id] + 1. / 60. : 0.;
                                        stationaryMax[body.id] = std::max(stationaryMax[body.id], stationaryClock[body.id]);
                                        if (!windowAt.count(body.id)) {windowAt[body.id] = m.State().elapsed;windowPosition[body.id] = body.position;}
                                        if (m.State().elapsed - windowAt[body.id] >= .5 - 1e-9) {
                                            const double net = std::hypot(body.position.x - windowPosition[body.id].x, body.position.z - windowPosition[body.id].z);
                                            netWait[body.id] = net < .03 ? netWait[body.id] + .5 : 0.;
                                            netWaitMax[body.id] = std::max(netWaitMax[body.id], netWait[body.id]);
                                            windowAt[body.id] = m.State().elapsed;windowPosition[body.id] = body.position;
                                        }
                                        if (stationaryClock[body.id] > .5 && stationaryClock[body.id] <= .5 + 1. / 60. + 1e-9) {
                                            EntityId blocker = 0;double closest = 1e9;
                                            for (const auto &other : m.State().entities) if (PhysicalPair(body, other)) {
                                                const double gap = std::hypot(body.position.x - other.position.x, body.position.z - other.position.z) -
                                                    body.radius - other.radius - (other.kind == EntityKind::Troop ? .02 : .22);
                                                if (gap < closest) {closest = gap;blocker = other.id;}
                                            }
                                            std::cout << "SWARM_BLOCKED card=" << cardId << " id=" << body.id << " time=" << m.State().elapsed
                                                      << " position=" << body.position.x << ',' << body.position.z << " arrival=" << body.pathTarget.x << ',' << body.pathTarget.z
                                                      << " reason=" << (body.path.empty() ? "no_route" : "contact_or_obstacle")
                                                      << " nearest_body=" << blocker << " clearance_gap=" << closest << '\n';
                                        }
                                    }
                                    if (body.target == targetId && m.CombatDistance(body, ById(m, targetId)) <=
                                        card->range + body.radius + ById(m, targetId).radius + 1e-9)
                                        ++concurrent;
                                    const auto hit = lastHit.find(body.id);
                                    if (hit != lastHit.end() && m.State().elapsed - hit->second <= card->attackInterval + .05)
                                        ++recentlyHit;
                                }
                                peakConcurrent = std::max(peakConcurrent, concurrent);
                                if (concurrent == card->count && recentlyHit == card->count) {
                                    if (firstTogether < 0) firstTogether = m.State().elapsed;
                                    ++togetherFrames;
                                }
                            }
                            std::cout << "SWARM_ENGAGEMENT card=" << cardId << " team=" << int(team) << " lane=" << lane
                                      << " target_kind=" << int(kind) << " members=" << swarm.size()
                                      << " peak_concurrent=" << peakConcurrent << " hit_members=" << lastHit.size()
                                      << " together_frames=" << togetherFrames << " first_together=" << firstTogether << '\n';
                            double firstAttack = 1e9, lastArrivalAttack = 0;
                            double longestApproach = 0;
                            for (const auto &[id, time] : firstHit) {
                                firstAttack = std::min(firstAttack, time);
                                lastArrivalAttack = std::max(lastArrivalAttack, time);
                                Check(approachEntry.count(id), "every observed attacker has an actual local approach entry");
                                longestApproach = std::max(longestApproach, time - approachEntry.at(id));
                            }
                            std::cout << "SWARM_CONTACT_TIMING card=" << cardId << " team=" << int(team) << " lane=" << lane
                                      << " target_kind=" << int(kind) << " requested_drop=" << lane * 7.5 << ',' << sign * 5.5
                                      << " target_center=" << ById(m, targetId).position.x << ',' << ById(m, targetId).position.z
                                      << " first_member_attack=" << firstAttack << " last_member_first_attack=" << lastArrivalAttack
                                      << " settling_delay=" << lastArrivalAttack - firstAttack
                                      << " max_local_approach=" << longestApproach << '\n';
                            for (const auto &start : swarm)
                                std::cout << "SWARM_MEMBER_TIMING card=" << cardId << " team=" << int(team) << " lane=" << lane
                                          << " target_kind=" << int(kind) << " id=" << start.id << " spawn=" << start.position.x << ',' << start.position.z
                                          << " local_entry=" << (approachEntry.count(start.id) ? approachEntry.at(start.id) : -1)
                                          << " first_attack=" << (firstHit.count(start.id) ? firstHit.at(start.id) : -1)
                                          << " local_path_length=" << approachTravel[start.id] << " max_stationary_wait=" << stationaryMax[start.id]
                                          << " max_no_progress_wait=" << netWaitMax[start.id] << '\n';
                            if (togetherFrames < 30 || lastArrivalAttack - firstAttack > 5)
                                for (const auto &start : swarm) {
                                    const auto &body = ById(m, start.id);
                                    std::cerr << "ENGAGEMENT_MEMBER id=" << body.id << " position=" << body.position.x << ',' << body.position.z
                                              << " target=" << body.target << " last_hit=" << (lastHit.count(body.id) ? lastHit.at(body.id) : -1)
                                              << " first_hit=" << (firstHit.count(body.id) ? firstHit.at(body.id) : -1)
                                              << " path_size=" << body.path.size() << " stuck=" << body.stuckTime
                                              << " arrival=" << body.pathTarget.x << ',' << body.pathTarget.z << '\n';
                                    for (const auto &point : body.path)
                                        std::cerr << "ENGAGEMENT_PATH id=" << body.id << " point=" << point.x << ',' << point.z << '\n';
                                }
                            Check(peakConcurrent == card->count && lastHit.size() == swarm.size() && togetherFrames >= 30,
                                  "every paid swarm member occupies legal melee reach and damages the same live target concurrently");
                            slowContactFixtures += longestApproach > 5 ||
                                lastArrivalAttack - firstAttack > (kind == EntityKind::Core ? 6. : 5.);
                            ++fixtures;members += int(swarm.size());
                        }
            Check(fixtures == 24 && members == 240, "both real hog swarms and every member contact Guard Core and paid building in both mirrored lanes");
            Check(slowContactFixtures == 0, "all members join within five seconds of local approach, with bounded first-to-last arrival travel");
        });
        Test("swarm target arc reservations remain deterministic across frame chunks", [] {
            auto options = CollisionOptions("stampede", "archer_tower");Match first(options), repeat(options);
            for (Match *m : {&first, &repeat}) {
                FreezeCrowns(*m);
                Check(m->SetTowerHP(Tower(*m, Team::Enemy, EntityKind::Guard, -1).id, 0), "deterministic real Core lane opened");
                PaidCollisionCard(*m, Team::Player, "stampede", {-7.5, 5.5});
            }
            for (int step = 0; step < 12 * 60; ++step) first.Step(1. / 60.);
            for (int step = 0; step < 48; ++step) repeat.Step(.25);
            Same(first, repeat);BodyClearance(first, "deterministic actual target arc bodies");
        });
        Test("real crown fire frees swarm contacts and living waiting members refill promptly", [] {
            int refills = 0, exposedDeaths = 0;
            double worstLatency = 0;
            for (const Team team : {Team::Player, Team::Enemy}) for (const int lane : {-1, 1}) {
                Match m(CollisionOptions("mini_stampede", "stampede"));
                const Team opponent = team == Team::Player ? Team::Enemy : Team::Player;
                const int sign = team == Team::Player ? 1 : -1;
                const EntityId targetId = Tower(m, opponent, EntityKind::Guard, lane).id;
                // Two actual paid plays exceed the contact ring's body capacity.
                // Every crown fires normally; no HP, damage or position edits.
                const double dropDepth = team == Team::Player && lane == -1 ? 4.5 : 3.5;
                auto paid = PaidCollisionCard(m, team, "mini_stampede", {lane * 7.5, sign * dropDepth});
                const auto larger = PaidCollisionCard(m, team, "stampede", {lane * 7.5, sign * dropDepth});
                paid.insert(paid.end(), larger.begin(), larger.end());
                Check(paid.size() == 20, "two paid swarm plays retain all twenty real bodies");
                struct Vacancy {EntityId lost = 0;double at = 0, deadline = 0, distance = 0;};
                std::map<EntityId, Vacancy> waiting;
                std::set<EntityId> hit;
                std::size_t cursor = m.Events().size();
                int fixtureRefills = 0;
                for (int frame = 0; frame < 20 * 60; ++frame) {
                    const auto before = m.State().entities;m.Step(1. / 60.);
                    SweptBodyClearance(m, before, "real crown fire swarm contact refill");
                    for (; cursor < m.Events().size(); ++cursor) {
                        const auto &event = m.Events()[cursor];
                        if (event.type != "damage" || event.target != targetId ||
                            (event.cardId != "mini_stampede" && event.cardId != "stampede")) continue;
                        hit.insert(event.source);
                        const auto refill = waiting.find(event.source);
                        if (refill == waiting.end()) continue;
                        const double latency = event.time - refill->second.at;
                        std::cout << "SWARM_VACANCY_REFILL team=" << int(team) << " lane=" << lane
                                  << " lost=" << refill->second.lost << " replacement=" << event.source
                                  << " death_time=" << refill->second.at << " hit_time=" << event.time
                                  << " latency=" << latency << " direct_contact_distance=" << refill->second.distance
                                  << " allowed_latency=" << refill->second.deadline - refill->second.at << '\n';
                        Check(event.time <= refill->second.deadline + 1e-9,
                              "a living nearby waiter refills a real crown casualty within direct walking time plus normal steering allowance");
                        worstLatency = std::max(worstLatency, latency);++refills;++fixtureRefills;
                        waiting.erase(refill);
                    }
                    const auto *target = FindEntity(m, targetId);
                    for (auto it = waiting.begin(); it != waiting.end();) {
                        const auto *body = FindEntity(m, it->first);
                        if (!target || target->dead || !body || body->dead) {it = waiting.erase(it);continue;}
                        if (m.State().elapsed > it->second.deadline + 1e-9)
                            std::cerr << "SWARM_VACANCY_DELAY team=" << int(team) << " lane=" << lane << " lost=" << it->second.lost
                                      << " waiter=" << body->id << " elapsed=" << m.State().elapsed << " death_time=" << it->second.at
                                      << " deadline=" << it->second.deadline << " position=" << body->position.x << ',' << body->position.z
                                      << " arrival=" << body->pathTarget.x << ',' << body->pathTarget.z << '\n';
                        Check(m.State().elapsed <= it->second.deadline + 1e-9,
                              "a live waiting member cannot remain sealed behind a vacated nearby attacking contact");
                        ++it;
                    }
                    if (!target || target->dead) break;
                    for (const auto &lost : before) {
                        if (lost.dead || lost.team != team || !hit.count(lost.id) || lost.target != targetId ||
                            m.CombatDistance(lost, *target) > m.AttackReach(lost, *target) + 1e-9) continue;
                        const auto *now = FindEntity(m, lost.id);
                        if (now && !now->dead) continue;
                        ++exposedDeaths;
                        EntityId next = 0;double nearest = 3.;
                        for (const auto &candidate : paid) {
                            const auto *body = FindEntity(m, candidate.id);
                            if (!body || body->dead || hit.count(body->id) || waiting.count(body->id) || body->target != targetId) continue;
                            const double distance = std::hypot(body->position.x - lost.position.x, body->position.z - lost.position.z);
                            bool clear = true;
                            for (const auto &obstacle : m.State().entities) {
                                if (obstacle.id == lost.id || !PhysicalPair(*body, obstacle)) continue;
                                const double clearance = body->radius + obstacle.radius +
                                    (obstacle.kind == EntityKind::Troop ? .02 : .22);
                                if (SegmentDistance(body->position, lost.position, obstacle.position) < clearance - 1e-8) {clear = false;break;}
                            }
                            if (!clear) continue;
                            if (distance < nearest) {nearest = distance;next = body->id;}
                        }
                        if (next) {
                            const Card *definition = FindEntityDefinition(ById(m,next).cardId);
                            const double allowance = std::max(1., nearest / definition->moveSpeed + .75);
                            waiting[next] = {lost.id, m.State().elapsed, m.State().elapsed + allowance, nearest};
                        }
                    }
                }
                std::cout << "SWARM_VACANCY_FIXTURE team=" << int(team) << " lane=" << lane << " paid_members=20 hit_members=" << hit.size()
                          << " refills=" << fixtureRefills << " exposed_deaths_so_far=" << exposedDeaths << '\n';
                Check(fixtureRefills > 0, "real crown fire exposes and refills a clear-route waiting contact in every mirrored lane");
            }
            Check(refills > 0 && exposedDeaths > 0, "real crown fire actually exposes and refills attacking contacts during the four mirrored matches");
            std::cout << "SWARM_VACANCY_SUMMARY fixtures=4 exposed_attacker_deaths=" << exposedDeaths
                      << " completed_refills=" << refills << " worst_latency=" << worstLatency << '\n';
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
                            id == "bullet_burst" ? Vec2{-lane * (arena::LastTileX - 1), -sign * (arena::LastTileZ - 1)} : Vec2{lane * 7.5, sign * 6.5});
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
                    // This measures escape through the bridge. Keep its paid
                    // pocket's river-relative travel distance when the crowns
                    // move farther back in an arena geometry update.
                    Vec2 drop{lane * (arena::BridgeCenterX + .3), sign * 5.5};
                    if (play == 9) drop = {lane * (arena::BridgeCenterX - .7), sign * 4.5};
                    if (play == 15) drop = {lane * (arena::BridgeCenterX + 2.3), sign * 4.5};
                    drop = SnapToTile(drop);
                    const Vec2 requested = drop;
                    const auto id = m.State().hands[int(team)][slots[play]];
                    const auto *card = FindCard(id);
                    auto buildingTileFree = [&](Vec2 candidate) {
                        if (!m.CanPlace(team, *card, candidate) || candidate.z * sign < 4.5 ||
                            std::abs(candidate.x) > arena::HalfWidth - .40 - card->collisionRadius * .72) return false;
                        for (const auto &body : m.State().entities) {
                            if (body.dead || (body.kind == EntityKind::Troop && body.flying)) continue;
                            if (std::hypot(candidate.x - body.position.x, candidate.z - body.position.z) <
                                card->collisionRadius + body.radius + .22 - 1e-9) return false;
                        }
                        return true;
                    };
                    // Mirror body offsets can make a building request illegal.
                    // Request a genuinely free tile at this pocket's depth;
                    // resolving a crowded request nearer the river can close
                    // the selected bridge physically to the largest bodies.
                    if (card->building && !buildingTileFree(drop)) {
                        bool legal = false;
                        for (int ring = 1; ring <= 8 && !legal; ++ring)
                            for (int direction = 0; direction < 8 && !legal; ++direction) {
                                const double angle = direction * 3.14159265358979323846 * .25;
                                const Vec2 candidate = SnapToTile({requested.x + ring * std::cos(angle),
                                                                   requested.z + ring * std::sin(angle)});
                                if (buildingTileFree(candidate)) {drop = candidate;legal = true;}
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
                if (team == Team::Enemy && lane == 1) {
                    for (const auto &building : buildings)
                        std::cout << "POCKET_STATIC_OBSTACLE id=" << building.id << " pos=" << building.position.x << ',' << building.position.z
                                  << " radius=" << building.radius << '\n';
                    for (const auto &start : ground) {
                        const auto path = m.FindPath(start, Tower(m, Team::Player, EntityKind::Guard, lane), lane);
                        std::cout << "POCKET_INITIAL_ROUTE id=" << start.id << " card=" << start.cardId << " points=" << path.size() << '\n';
                    }
                }
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
                    if (body)
                        for (const auto &point : body->path)
                            std::cerr << "BUILDING_POCKET_PATH id=" << body->id << " point="
                                      << point.x << ',' << point.z << '\n';
                }
                Check(crossed.size() == ground.size(), "every paid mobile ground member escapes the live three-building pocket and clears its bridge by the authored deadline");
            }
        });
        Test("original legal three-building pocket yields before obstacle expiry without tunneling", [] {
            const std::vector<std::string> deck{"ironclad", "twin_blades", "boulderback", "archer_tower",
                                                "sky_manta", "vampire_bats", "storm_raven", "frost_fang"};
            const int slots[]{1, 2, 2, 3, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3};
            for (const Team team : {Team::Player, Team::Enemy}) for (const int lane : {-1, 1}) {
                auto options = Quiet(); options.seed = 135; options.decks[int(team)] = deck;
                Match first(options), repeat(options); const int sign = team == Team::Player ? 1 : -1;
                std::vector<Entity> ground, buildings;
                for (Match *m : {&first, &repeat}) {
                    int members = 0;
                    for (int play = 0; play < 16; ++play) {
                        Vec2 drop{lane * 7.5, sign * 8.5};
                        if (play == 9) drop = {lane * 6.5, sign * 7.5};
                        if (play == 15) drop = {lane * 8.5, sign * 7.5};
                        const Vec2 requested = drop;
                        const auto id = m->State().hands[int(team)][slots[play]];
                        const auto *card = FindCard(id);
                        if (card->building && !m->CanPlace(team, *card, drop)) {
                            bool legal = false;
                            for (int ring = 1; ring <= 8 && !legal; ++ring)
                                for (int direction = 0; direction < 8 && !legal; ++direction) {
                                    const double angle = direction * 3.14159265358979323846 * .25;
                                    const auto candidate = SnapToTile({requested.x + ring * std::cos(angle),
                                                                       requested.z + ring * std::sin(angle)});
                                    if (m->CanPlace(team, *card, candidate)) {drop = candidate; legal = true;}
                                }
                            Check(legal, "original pocket requests a real legal nearby building tile");
                        }
                        const auto added = PaidCollisionCard(*m, team, id, drop); members += int(added.size());
                        if (m == &first) for (const auto &body : added) {
                            if (body.kind == EntityKind::Troop && !body.flying) ground.push_back(body);
                            if (body.kind == EntityKind::Building) buildings.push_back(body);
                        }
                        BodyClearance(*m, "original pocket paid landing");
                    }
                    Check(members == 31, "original pocket retains all thirty-one physical paid bodies");
                    ++collisionPaidFixtures;
                }
                Check(ground.size() == 11 && buildings.size() == 3, "original pocket has eleven mobile ground bodies and three fixed buildings");
                std::map<EntityId, bool> crossed;
                std::map<EntityId, Vec2> progress;
                for (const auto &body : ground) progress[body.id] = body.position;
                for (int step = 0; step < 20 * 60; ++step) {
                    for (Match *m : {&first, &repeat}) {
                        // Explicit developer diagnostic: maintain only Core HP
                        // so a fast member's victory cannot end the clock while
                        // slower bodies are still observed. Paid cards, firing,
                        // radii, targets, and obstacle lifetimes stay unchanged.
                        for (const auto &body : m->State().entities)
                            if (!body.dead && body.kind == EntityKind::Core)
                                Check(m->SetTowerHP(body.id, body.maxHp), "declared diagnostic Core maintenance accepted");
                        const auto before = m->State().entities; m->Step(1. / 60.);
                        SweptBodyClearance(*m, before, "original legal pocket safe yield");
                    }
                    for (const auto &start : buildings) {
                        const auto *body = FindEntity(first, start.id);
                        Check(body && !body->dead && body->hp > 0, "original pocket does not wait for paid building death or expiry");
                        Near(std::hypot(body->position.x - start.position.x, body->position.z - start.position.z),
                             0, 1e-9, "original pocket never shoves a stationary building");
                    }
                    for (const auto &start : ground) if (const auto *body = FindEntity(first, start.id)) {
                        if (body->position.z * sign < -arena::RiverHalfWidth - .28) crossed[body->id] = true;
                        if (!body->dead && std::abs(body->position.z) < arena::RiverHalfWidth + .28)
                            Check(body->position.x * lane > 0, "original pocket preserves deployment-side bridge");
                        if ((step + 1) % 300 == 0) {
                            const auto previous = progress.at(body->id);
                            if (!crossed.count(body->id)) {
                                const double net = std::hypot(body->position.x - previous.x, body->position.z - previous.z);
                                if (net <= .25)
                                    std::cerr << "ORIGINAL_POCKET_STALLED team=" << int(team) << " lane=" << lane
                                              << " time=" << first.State().elapsed << " id=" << body->id << " card=" << body->cardId
                                              << " hp=" << body->hp << " dead=" << body->dead << " position=" << body->position.x << ',' << body->position.z
                                              << " target=" << body->target << " stuck=" << body->stuckTime << " net=" << net << '\n';
                                Check(!body->dead && net > .25, "surviving un-crossed original pocket member makes net progress every five seconds");
                            }
                            progress[body->id] = body->position;
                        }
                    }
                }
                Same(first, repeat);
                if (crossed.size() != ground.size()) for (const auto &start : ground) if (!crossed.count(start.id)) {
                    const auto *body = FindEntity(first, start.id);
                    std::cerr << "ORIGINAL_POCKET_UNCROSSED team=" << int(team) << " lane=" << lane << " id=" << start.id
                              << " card=" << start.cardId << " start=" << start.position.x << ',' << start.position.z;
                    if (body) {
                        std::cerr << " end=" << body->position.x << ',' << body->position.z << " hp=" << body->hp << " dead=" << body->dead
                                  << " target=" << body->target << " stuck=" << body->stuckTime << " goal=" << body->pathTarget.x << ',' << body->pathTarget.z;
                        for (const auto &point : body->path) std::cerr << " via=" << point.x << ',' << point.z;
                    } else std::cerr << " removed=1";
                    std::cerr << '\n';
                }
                Check(crossed.size() == ground.size(), "all eleven original-layout ground bodies clear their bridge within twenty seconds before building expiry");
                std::cout << "COLLISION_ORIGINAL_POCKET core_maintained_diagnostic=1 team=" << int(team)
                          << " lane=" << lane << " casts_per_repeat=16 members=31 ground=" << ground.size()
                          << " crossed=" << crossed.size() << " seconds=20 deterministic_repeats=2\n";
            }
        });
        Test("physically sealed paid bridge waits safely and resumes after real building expiry", [] {
            const std::vector<std::string> deck{"ironclad", "twin_blades", "boulderback", "archer_tower",
                                                "sky_manta", "vampire_bats", "storm_raven", "frost_fang"};
            const int slots[]{1, 2, 2, 3, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3};
            auto options = Quiet();options.seed = 135;options.decks[1] = deck;
            Match m(options);FreezeCrowns(m);
            std::vector<Entity> ground, buildings;
            for (int play = 0; play < 16; ++play) {
                Vec2 drop{arena::BridgeCenterX + .3, -5.5};
                if (play == 9) drop = {arena::BridgeCenterX - .7, -4.5};
                if (play == 15) drop = {arena::BridgeCenterX + 2.3, -4.5};
                drop = SnapToTile(drop);
                const auto requested = drop;
                const auto id = m.State().hands[1][slots[play]];
                const auto *card = FindCard(id);
                // Reproduce the observed legal casts whose normal crowded
                // placement resolution physically closes this bridge. This
                // fixture intentionally permits that resolved wall geometry.
                if (card->building && !m.CanPlace(Team::Enemy, *card, drop)) {
                    bool legal = false;
                    for (int ring = 1; ring <= 8 && !legal; ++ring)
                        for (int direction = 0; direction < 8 && !legal; ++direction) {
                            const double angle = direction * 3.14159265358979323846 * .25;
                            const auto candidate = SnapToTile({requested.x + ring * std::cos(angle), requested.z + ring * std::sin(angle)});
                            if (m.CanPlace(Team::Enemy, *card, candidate)) {drop = candidate;legal = true;}
                        }
                    Check(legal, "sealed bridge still uses a legal real paid building request");
                }
                const auto added = PaidCollisionCard(m, Team::Enemy, id, drop);
                for (const auto &body : added) {
                    if (body.kind == EntityKind::Troop && !body.flying) ground.push_back(body);
                    if (body.kind == EntityKind::Building) buildings.push_back(body);
                }
                BodyClearance(m, "sealed bridge paid landing");
            }
            ++collisionPaidFixtures;
            Check(ground.size() == 11 && buildings.size() == 3, "sealed bridge retains eleven paid ground members and three actual building bodies");
            const auto target = Tower(m, Team::Player, EntityKind::Guard, 1);
            std::vector<Entity> waiting;
            int physicallySealed = 0;
            for (const auto &body : ground)
                if (m.FindPath(body, target, 1).empty()) {
                    waiting.push_back(body);
                    physicallySealed += body.cardId == "boulderback";
                }
            Check(waiting.size() == 5 && physicallySealed == 3,
                  "observed grid-unrouted cohort includes the three analytically sealed Boulderbacks");
            std::map<EntityId, double> crossed;
            double openedAt = -1;
            for (int frame = 0; frame < 45 * 60; ++frame) {
                // Explicit observation-only crown suppression and HP upkeep
                // prevent fire/casualties or an early victory ending the clock.
                // Building HP decay, mandatory lifetime and all troops are real.
                for (const auto &body : m.State().entities)
                    if (!body.dead && (body.kind == EntityKind::Guard || body.kind == EntityKind::Core))
                        Check(m.SetTowerHP(body.id, body.maxHp), "declared sealed-bridge crown HP maintenance accepted");
                const auto before = m.State().entities;m.Step(1. / 60.);
                SweptBodyClearance(m, before, "sealed bridge wait and expiry recovery");
                bool allLive = true;
                for (const auto &start : buildings) {
                    const auto *body = FindEntity(m, start.id);
                    allLive = allLive && body && !body->dead;
                    if (body && !body->dead) {
                        Near(std::hypot(body->position.x - start.position.x, body->position.z - start.position.z), 0, 0,
                             "sealed bridge never shoves its stationary obstacles");
                        Check(body->hp > 0 && m.State().elapsed < 25 + 1e-8, "live wall follows real building HP and lifetime");
                    }
                }
                if (!allLive && openedAt < 0) {
                    openedAt = m.State().elapsed;
                    Near(openedAt, 25, 1. / 60. + 1e-8, "bridge wall opens at mandatory expiry rather than a shortened lifetime");
                }
                for (const auto &start : waiting) {
                    const auto &body = ById(m, start.id);
                    Check(!body.dead, "suppressed crown observation keeps every waiting paid member alive");
                    if (allLive && body.cardId == "boulderback")
                        Check(body.position.z <= arena::RiverHalfWidth + .28, "a physically blocked paid body cannot cross the live solid wall");
                    if (std::abs(body.position.z) < arena::RiverHalfWidth + .28)
                        Check(body.position.x > 0, "a closed bridge never redirects a waiting troop to the opposite bridge");
                    if (body.position.z > arena::RiverHalfWidth + .28) crossed.try_emplace(body.id, m.State().elapsed);
                }
                if (crossed.size() == waiting.size()) break;
            }
            Check(openedAt >= 0 && crossed.size() == waiting.size(), "every physically blocked paid member resumes and crosses its own bridge after real expiry");
            double latest = 0;
            for (const auto &[id, time] : crossed) {
                Check(time - openedAt <= 20, "expiry recovery respects the original twenty-second travel bound");
                latest = std::max(latest, time - openedAt);
                std::cout << "SEALED_BRIDGE_RECOVERY id=" << id << " opened_at=" << openedAt << " crossed_at=" << time
                          << " recovery_seconds=" << time - openedAt << '\n';
            }
            std::cout << "SEALED_BRIDGE_SUMMARY paid_casts=16 ground=11 buildings=3 physically_sealed=3 grid_unrouted=5 recovered=5 latest_recovery=" << latest << '\n';
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
                int buildings = 0, residuals = 0, rejected = 0, buildingRequests = 0;
                const Vec2 drop{7.5, (team == Team::Player ? 1 : -1) * 8.5};
                // These are actual public sandbox casts, including deliberate
                // river rows, rather than fabricated occupied cells or radii.
                for (double z = -arena::LastTileZ; z <= arena::LastTileZ; z += 2)
                    for (double x = -arena::LastTileX; x <= arena::LastTileX; x += 2)
                        {++buildingRequests; if (m.Spawn(team, "archer_tower", {x, z})) ++buildings; else ++rejected;}
                Check(buildingRequests == 220 && buildings >= 190 && rejected == buildingRequests - buildings && rejected > 0,
                      "22x40 board has220 requests and retains a dense radius-aware separated building field");
                bool exhausted = false;
                for (int n = 0; n < 100; ++n) {
                    if (!m.Spawn(team, "boulderback", drop)) {++rejected;exhausted = true;break;}
                    ++residuals;
                }
                Check(residuals > 0 && exhausted,
                      "bounded real large/small bodies genuinely exhaust arena capacity");
                BodyClearance(m, "capacity fixture real physical bodies");
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
        Check(passed > 0, "requested scenario filter selected actual tests");
        std::cout.precision(12);
        std::cout << "COLLISION_SUMMARY paid_fixtures=" << collisionPaidFixtures << " paid_plays=" << collisionPaidPlays
                  << " members=" << collisionMembers << " endpoint_pair_checks=" << collisionPairChecks
                  << " swept_pair_checks=" << collisionSweepChecks << " min_body_gap=" << collisionMinimumGap
                  << " min_swept_gap=" << collisionMinimumSweepGap << '\n';
        std::cout << "Native authoritative simulation: " << passed << " scenarios passed.\n";
        if (failed) {std::cerr << "Native authoritative simulation: " << failed << " scenarios failed.\n";return 1;}
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
