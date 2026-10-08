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
            m.Spawn(Team::Player, "nova_flask", {0, -16.3});
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
            Check(!m.CanPlace(Team::Player, *c, {-7.5, -10.5}), "deep backfield excluded");
        });
        Test("building full footprint and collision", [] {
            Match m(Quiet());
            const auto *c = FindCard("archer_tower");
            Check(!m.CanPlace(Team::Player, *c, {7.5, 2.5}), "footprint crosses bank");
            Check(m.CanPlace(Team::Player, *c, {7.5, 4.5}), "full footprint fits");
            Check(!m.CanPlace(Team::Player, *c, {8.5, 12.5}), "Tower overlap");
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
            m.Spawn(Team::Player, "bullet_burst", {0, -16.3});
            Near(Tower(m, Team::Enemy, EntityKind::Core).hp, 3545, 0, "55 structure damage");
        });
        Test("Meteor initial and inclusive fifth DOT tick no structures", [] {
            Match m(Quiet());
            m.Spawn(Team::Enemy, "boulderback", {8.5, 10.5});
            m.Spawn(Team::Player, "meteor_shards", {8.5, 10.5});
            Near(m.State().telemetry[0].at("meteor_shards").initialDamage, 262, 0, "initial262");
            m.Step(5);
            const auto &t = m.State().telemetry[0].at("meteor_shards");
            Check(t.dotTicks == 5, "five scheduled ticks");
            Near(t.dotDamage, 200, 1e-8, "five40damage ticks");
            Near(t.towerDamage, 0, 0, "no structure damage");
            Near(t.zoneSeconds, 5, 1e-8, "exact five-second zone exposure");
            Check(m.State().hazards.empty(), "expired zone after final tick");
        });
        Test("AI Meteor zone exposure begins at its cast endpoint", [] {
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
            Check(m.State().hazards.size() == 1, "visible clustered threat causes an AI Meteor cast");
            Near(m.State().hazards.front().born, m.State().elapsed, 1e-9,
                 "hazard born at the fixed-tick endpoint");
            const auto &born = m.State().telemetry[0].at("meteor_shards");
            Check(born.plays == 1, "one paid AI cast");
            Near(born.zoneSeconds, 0, 0, "no exposure before the hazard was born");
            Near(born.zoneOccupancy, 0, 0, "no target occupancy before the hazard was born");
            m.SetAIEnabled(Team::Player, false);
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
            m.Spawn(Team::Player, "ironclad", {-8.5, -10.5});
            m.Step(.05);
            auto tower = Tower(m, Team::Enemy, EntityKind::Guard, -1).id;
            Check(Unit(m, "ironclad", Team::Player).hardLock == tower, "locks upon reaching attack range");
            m.Spawn(Team::Enemy, "ironclad", {-7.5, -10.5});
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
                for (int tick = 0; tick < 1440 && m.State().phase != Phase::Finished; ++tick)
                    m.Step(.25);
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
        std::cout << "Native authoritative simulation: " << passed << " scenarios passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
