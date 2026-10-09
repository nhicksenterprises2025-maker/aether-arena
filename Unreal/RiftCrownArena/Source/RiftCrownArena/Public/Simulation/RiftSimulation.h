#pragma once

// Portable authoritative gameplay. UE presentation, Meta Lab and native tests
// consume this same implementation; no rendering or hidden opponent hand enters AI.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rift {
using EntityId = std::uint64_t;
using PlayId = std::uint64_t;
enum class Team : std::uint8_t { Player, Enemy };
enum class Phase : std::uint8_t { Regulation, Overtime, Tiebreaker, Finished };
enum class EntityKind : std::uint8_t { Troop, Building, Guard, Core };
struct Vec2 {
    double x = 0, z = 0;
};
struct Card {
    std::string id, name;
    int cost = 0, count = 1;
    double hp = 0, damage = 0, attackInterval = 0, moveSpeed = 0, range = 0, scale = 1;
    double projectileSpeed = 0, splash = 0, lifetime = 0, footprint = 0;
    double towerDamage = 0, spellRadius = 0, chargeDamage = 0, castDelay = 0;
    double slowPct = 0, slowDuration = 0, auraDamage = 0, auraInterval = 0, auraRadius = 0, stunDuration = 0;
    double dotDamage = 0, dotDuration = 0, dotInterval = 0;
    int rounds = 0;
    bool flying = false, canHitAir = false, structuresOnly = false, spell = false, building = false;
};
const std::vector<Card> &Cards();
const Card *FindCard(const std::string &id);
const std::vector<std::string> &DefaultDeck();
std::string TeamName(Team team);
std::string PhaseName(Phase phase);
Vec2 SnapToTile(Vec2 point);
bool ValidateDeck(const std::vector<std::string> &deck);

struct Entity {
    EntityId id = 0, target = 0, hardLock = 0, forcedTarget = 0;
    PlayId playId = 0, slowSource = 0, stunSource = 0;
    Team team = Team::Player;
    EntityKind kind = EntityKind::Troop;
    std::string cardId;
    Vec2 position, facing{0, -1}, pathTarget, stuckPosition;
    double hp = 0, maxHp = 0, radius = 0, cooldown = 0, born = 0, died = 0;
    double slowPct = 0, slowUntil = 0, stunUntil = 0, forcedUntil = 0;
    double chargeTime = 0, auraClock = 0, repathClock = 0, scanClock = 0;
    double stuckClock = 0, stuckTime = 0, stuckDistance = 1e9;
    double slowTrackedFrom = -1, stunTrackedFrom = -1;
    int lane = 0, bridge = 0, memberCount = 1;
    bool active = true, dead = false, flying = false, charged = false;
    std::vector<Vec2> path;
};
struct Projectile {
    std::uint64_t id = 0;
    EntityId source = 0, target = 0;
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    Vec2 origin;
    double damage = 0, splash = 0, remaining = 0, duration = 0;
};
struct Hazard {
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    Vec2 position;
    double radius = 0, born = 0, nextTick = 0, expires = 0;
    int ticks = 0;
};
struct SpellCast {
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    Vec2 position;
    double born = 0, impactAt = 0;
};
struct Event {
    std::uint64_t sequence = 0;
    double time = 0;
    std::string type, reason, cardId, damageKind;
    Team team = Team::Player, targetTeam = Team::Player;
    EntityId source = 0, target = 0;
    PlayId playId = 0;
    EntityKind targetKind = EntityKind::Troop;
    Vec2 position;
    double amount = 0, requested = 0, overkill = 0, hp = 0, maxHp = 0;
    double aetherBefore = 0, aetherAfter = 0, until = 0, targetCost = 0;
    int handIndex = -1, count = 1, crowns = 0;
    bool sandbox = false, openingHand = false, firstPlay = false, overtime = false;
    std::array<std::string, 4> handAfter;
};
struct CardTelemetry {
    double spent = 0, troopDamage = 0, towerDamage = 0, buildingDamage = 0, damageTaken = 0;
    double kills = 0, deaths = 0, killValue = 0, lifetime = 0, slowTime = 0, stunTime = 0;
    double initialDamage = 0, dotDamage = 0, auraDamage = 0, overkill = 0, prevented = 0;
    double placementX = 0, placementZ = 0, spellValue = 0, zoneOccupancy = 0, zoneSeconds = 0;
    double crownContribution = 0;
    double buildingLifetime = 0, buildingCapacity = 0, slowTrackedSeconds = 0, stunTrackedSeconds = 0;
    std::uint64_t plays = 0, spawns = 0, surviving = 0, pulls = 0, connected = 0, targets = 0;
    std::uint64_t openingPlays = 0, firstPlays = 0, overtimePlays = 0, stunned = 0, dotTicks = 0;
    std::uint64_t openingEligible = 0;
};
struct AIState {
    bool enabled = false;
    std::string style = "control", phase = "bank", decision = "BANK", reason;
    double think = .35, estimatedOpponentAether = 5, lastPunish = -100, lastSpell = -100;
    EntityId anchor = 0;
    int supports = 0;
    int pushLane = 1;
    std::vector<std::string> observedCycle;
};
struct Snapshot {
    std::uint32_t seed = 0, randomState = 0;
    double elapsed = 0, phaseElapsed = 0, timeRemaining = 180;
    Phase phase = Phase::Regulation;
    std::array<double, 2> aether{5, 5}, spent{0, 0}, leaked{0, 0};
    std::array<int, 2> crowns{0, 0};
    std::array<std::array<std::string, 4>, 2> hands;
    std::array<std::vector<std::string>, 2> queues, decks;
    std::array<AIState, 2> ai;
    std::vector<Entity> entities;
    std::vector<Projectile> projectiles;
    std::vector<Hazard> hazards;
    std::vector<SpellCast> spellCasts;
    std::array<std::map<std::string, CardTelemetry>, 2> telemetry;
    int winner = -1; // -1 draw/unresolved, 0 Player, 1 Enemy; phase disambiguates.
    std::string resultReason;
};
struct MatchOptions {
    std::uint32_t seed = 151515;
    std::array<std::vector<std::string>, 2> decks;
    std::array<bool, 2> aiEnabled{false, true};
    std::array<std::string, 2> aiStyles{"control", "control"};
};
struct DeckAnalysis {
    double averageCost = 0, averageHP = 0, deploymentDPS = 0, averageRange = 0;
    int troops = 0, spells = 0, buildings = 0, air = 0, ground = 0, antiAir = 0, sustainedAntiAir = 0;
    int winConditions = 0, cheap = 0, ranged = 0, frontline = 0, splash = 0, fast = 0;
    double defenseScore = 0, synergy = 0;
    std::array<int, 11> costCurve{};
    std::vector<std::string> archetypes, strengths, weaknesses;
};
double CounterScore(const Card &attacker, const Card &defender);
std::string CounterReason(const Card &attacker, const Card &defender);
double PairSynergy(const Card &a, const Card &b);
std::vector<std::string> PairSynergyReasons(const Card &a, const Card &b);
struct CardRelation {
    std::string id, reason;
    double score = 0;
};
struct CardIntelligence {
    std::string id, role;
    std::vector<std::string> suggestedUses;
    std::vector<CardRelation> bestAgainst, weakAgainst, partners;
    CardRelation bestDefensiveAnswer, bestOffensivePartner;
};
CardIntelligence IntelligenceFor(const std::string &cardId);
DeckAnalysis AnalyzeDeck(const std::vector<std::string> &deck);
std::vector<std::string> BuildAIDeck(const std::string &style, std::uint32_t seed);

class Match {
  public:
    explicit Match(const MatchOptions &options = MatchOptions{});
    const Snapshot &State() const {
        return state_;
    }
    const std::vector<Event> &Events() const {
        return events_;
    }
    std::vector<Event> DrainEvents();
    void Step(double seconds);
    bool Play(Team team, int handIndex, Vec2 point, const std::string &reason = "player");
    bool Spawn(Team team, const std::string &cardId, Vec2 point); // tagged zero-cost DEV cast
    bool CanPlace(Team team, const Card &card, Vec2 point, bool sandbox = false) const;
    void SetAether(Team team, double value);
    bool SetTowerHP(EntityId tower, double hp);
    void ClearField();
    void SetAIEnabled(Team team, bool enabled);
    bool SetAIStyle(Team team, const std::string &style);
    // Testable public mechanics use the identical validation as attack execution.
    bool CanTarget(const Entity &source, const Entity &target) const;
    bool InSight(const Entity &source, const Entity &target, double bonus = 0) const;
    double CombatDistance(const Entity &source, const Entity &target) const;
    std::vector<Vec2> FindPath(const Entity &source, const Entity &target, int bridge = 0) const;
    static double AetherGenerated(double from, double to);

  private:
    Snapshot state_;
    std::vector<Event> events_;
    double accumulator_ = 0;
    EntityId nextEntity_ = 1;
    PlayId nextPlay_ = 1;
    std::uint64_t nextEvent_ = 1, nextProjectile_ = 1;
    std::array<std::uint64_t, 2> paidPlays_{};
    std::array<std::vector<std::string>, 2> openingPlayed_;
    std::array<double, 2> tieMin_{}, tieTotal_{};
    std::map<PlayId, std::vector<EntityId>> castTargets_;
    std::map<PlayId, bool> connected_;
    struct StatusCredit {
        Team team = Team::Player;
        std::string card;
    };
    std::map<PlayId, StatusCredit> statusCredits_;
    std::map<EntityId, std::map<std::pair<Team, std::string>, double>> towerCredits_;
    double Random();
    Entity *Get(EntityId id);
    const Entity *Get(EntityId id) const;
    Event &Emit(const std::string &type, Team team = Team::Player);
    void FixedStep(double dt);
    void Finish(int winner, const std::string &reason);
    void BeginTiebreaker();
    void UpdateTiebreaker(double dt);
    void Deploy(Team team, const Card &card, Vec2 point, PlayId play, bool sandbox);
    void ApplySpell(Team team, const Card &card, Vec2 point, PlayId play);
    void Damage(EntityId target, double amount, Team team, const std::string &card, PlayId play,
                EntityId source = 0, const std::string &kind = "attack");
    void Kill(EntityId target, Team team, const std::string &card, PlayId play, EntityId source);
    void PullDeployment(EntityId deployed);
    void UpdateEntity(EntityId id, double dt);
    void Acquire(Entity &source);
    void Attack(Entity &source, const Entity &target);
    void Move(Entity &source, const Entity &target, double dt);
    bool NavValid(Vec2 point, const Entity &source, EntityId goal, int bridge = 0) const;
    void UpdateAI(Team team, double dt);
    void Decision(Team team, const std::string &label, const std::string &reason);
    bool AIPlay(Team team, int index, Vec2 point, const std::string &reason, double reserve = 0);
};
} // namespace rift
