#pragma once

// Portable authoritative gameplay. UE presentation, Meta Lab and native tests
// consume this same implementation; no rendering or hidden opponent hand enters AI.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rift {
namespace arena {
#include "RiftArenaRules.generated.inc"
} // namespace arena
namespace detection {
inline constexpr double Front = 8, Rear = 5;
} // namespace detection
namespace targets {
inline constexpr std::uint8_t GroundTroop = 1, AirTroop = 2, Structure = 4, All = 7;
} // namespace targets
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
    double projectileSpeed = 0, splash = 0, lifetime = 0, footprint = 0, shockwaveSpeed = 0;
    // Collision is gameplay data, independent of the displayed model scale.
    double collisionRadius = 0;
    std::uint8_t targetMask = 0;
    double towerDamage = 0, spellRadius = 0, chargeDamage = 0, castDelay = 0;
    double slowPct = 0, slowDuration = 0, auraDamage = 0, auraInterval = 0, auraRadius = 0, stunDuration = 0;
    double dotDamage = 0, dotDuration = 0, dotInterval = 0;
    double chargeMoveSeconds = 0, chargedMoveSpeed = 0, hpDecayPerSecond = 0, initialAttackCooldown = 0;
    double shieldHP = 0, invisibilityDuration = 0, dotStructureDamage = 0, healthCloakThreshold = 0;
    int deathSpawnCount = 0;
    std::string deathSpawnId;
    int rounds = 0;
    bool flying = false, canHitAir = false, structuresOnly = false, spell = false, building = false;
    bool hardExpiry = false, shieldOverflowProtected = false, globalFriendlyInvisibility = false,
         breakExistingLocks = false;
};
const std::vector<Card> &Cards();
const Card *FindCard(const std::string &id);
const std::vector<Card> &EntityDefinitions(); // Subordinate entities, excluded from decks/collection.
const Card *FindEntityDefinition(const std::string &id);
const std::string &CardManifestHash();
const std::string &CardManifestVersion();
const std::vector<std::string> &DefaultDeck();
std::string TeamName(Team team);
std::string PhaseName(Phase phase);
Vec2 SnapToTile(Vec2 point);
bool ValidateDeck(const std::vector<std::string> &deck);

struct SlowEffect {
    EntityId source = 0;
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    double pct = 0, born = 0, until = 0;
};
struct Entity {
    EntityId id = 0, target = 0, hardLock = 0, forcedTarget = 0;
    PlayId playId = 0, slowSource = 0, stunSource = 0;
    Team team = Team::Player;
    EntityKind kind = EntityKind::Troop;
    std::string cardId;
    EntityId parentEntityId = 0, invisibilitySource = 0;
    PlayId invisibilityPlayId = 0;
    std::string attributionCardId, invisibilityCardId;
    Vec2 position, facing{0, -1}, pathTarget, stuckPosition;
    double hp = 0, maxHp = 0, radius = 0, cooldown = 0, born = 0, died = 0;
    double shieldHP = 0, maxShieldHP = 0, shieldBrokenAt = -1, invisibleUntil = 0;
    double slowPct = 0, slowUntil = 0, stunUntil = 0, forcedUntil = 0;
    double chargeTime = 0, auraClock = 0, repathClock = 0, scanClock = 0;
    double stuckClock = 0, stuckTime = 0, stuckDistance = 1e9;
    double slowTrackedFrom = -1, stunTrackedFrom = -1;
    int lane = 0, bridge = 0, memberCount = 1;
    bool active = true, dead = false, flying = false, charged = false, healthCloakTriggered = false;
    std::vector<Vec2> path;
    std::vector<SlowEffect> slows;
};
std::uint8_t TargetMaskFor(const Card &card);
bool CanHit(const Card &card, const Entity &target);
struct Projectile {
    std::uint64_t id = 0;
    EntityId source = 0, target = 0;
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    Vec2 origin;
    double damage = 0, splash = 0, remaining = 0, duration = 0;
};
struct Shockwave {
    std::uint64_t id = 0;
    EntityId source = 0;
    PlayId playId = 0;
    Team team = Team::Player;
    std::string cardId;
    Vec2 origin;
    double radius = 0, speed = 0, progress = 0, born = 0, expires = 0;
    std::vector<EntityId> hitTargets;
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
    EntityId parentEntityId = 0;
    std::string attributionCardId;
    PlayId playId = 0;
    EntityKind targetKind = EntityKind::Troop;
    Vec2 position;
    double amount = 0, requested = 0, overkill = 0, hp = 0, maxHp = 0;
    double shieldHP = 0, maxShieldHP = 0, shieldOverflowBlocked = 0;
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
    double shieldDamage = 0, shieldDamageTaken = 0, shieldAbsorbed = 0, overflowPrevented = 0;
    std::uint64_t plays = 0, spawns = 0, surviving = 0, pulls = 0, connected = 0, targets = 0;
    std::uint64_t openingPlays = 0, firstPlays = 0, overtimePlays = 0, stunned = 0, dotTicks = 0;
    std::uint64_t openingEligible = 0;
    std::uint64_t childSpawns = 0, childDeaths = 0, childSurviving = 0;
};
struct EntityTelemetry {
    EntityId id = 0, parentEntityId = 0;
    PlayId playId = 0;
    std::string cardId, attributionCardId;
    CardTelemetry observed;
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
    std::vector<Shockwave> shockwaves;
    std::vector<Hazard> hazards;
    std::vector<SpellCast> spellCasts;
    std::array<std::map<std::string, CardTelemetry>, 2> telemetry;
    std::array<std::map<EntityId, EntityTelemetry>, 2> entityTelemetry;
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
    double totalShieldHP = 0, deathSpawnHP = 0, deathSpawnDPS = 0;
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
inline constexpr std::size_t AIDeckCandidateLimit = 24310;
using AIDeckIndices = std::array<std::size_t, 8>;
// Exhaustive through the original 17-card roster; bounded deterministic
// coverage for expansions. Indices always refer to playable Cards(), never summons.
std::vector<AIDeckIndices> AIDeckCandidateIndices(std::size_t rosterSize);
struct AIDeckCacheMetrics {
    std::size_t evaluated = 0, legal = 0, retained = 0;
    std::uint64_t hits = 0;
    double buildMilliseconds = 0;
};
AIDeckCacheMetrics AIDeckCacheDiagnostics(const std::string &style);

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
    bool InSight(const Entity &source, const Entity &target) const;
    double CombatDistance(const Entity &source, const Entity &target) const;
    double AttackReach(const Entity &source, const Entity &target) const;
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
        EntityId source = 0;
    };
    std::map<PlayId, StatusCredit> statusCredits_;
    std::map<EntityId, std::map<std::pair<Team, std::string>, double>> towerCredits_;
    // Private fixed-step origins also constrain presentation's interpolation.
    // They never enter card stats, saves, replay snapshots, or random decisions.
    std::map<EntityId, Vec2> bodyStepStarts_;
    struct EngagementSlot {
        EntityId target = 0;
        Vec2 offset, progressPosition;
        double progressDistance = 1e9, progressAt = 0;
        bool pendingAcquisition = false;
    };
    // Arrival goals reserve physical space, not damage permission. They are
    // local movement state; attacks still validate the authored reach each tick.
    std::map<EntityId, EngagementSlot> engagementSlots_;
    double engagementCleanupAt_ = -1;
    struct NavBlocker {
        EntityId id = 0;
        Vec2 position;
        double radius = 0;
        EntityKind kind = EntityKind::Building;
    };
    struct CollisionBody {
        Vec2 from, to;
        double radius = 0;
        EntityKind kind = EntityKind::Troop;
        Team team = Team::Player;
        // Query-local constants for the collecting source. No body or source
        // moves between collection and this steering query's sweep tests.
        Vec2 relativeStart, forecastStart;
        double startSquared = 0, forecastSquared = 0, clearanceSquared = 0;
    };
    double Random();
    Entity *Get(EntityId id);
    const Entity *Get(EntityId id) const;
    Event &Emit(const std::string &type, Team team = Team::Player);
    void FixedStep(double dt);
    void Finish(int winner, const std::string &reason);
    void BeginTiebreaker();
    void UpdateTiebreaker(double dt);
    bool PlanDeployment(Team team, const Card &card, Vec2 point, bool sandbox,
                        std::vector<Vec2> &positions) const;
    void Deploy(Team team, const Card &card, Vec2 point, PlayId play, bool sandbox,
                const std::vector<Vec2> &positions);
    bool BodyPlacementValid(Vec2 point, const Entity &source, bool sandbox,
                            const std::vector<Entity> &planned) const;
    bool ResolveBodyPlacement(Vec2 point, const Entity &source, bool sandbox,
                              const std::vector<Entity> &planned, Vec2 &result) const;
    std::vector<CollisionBody> CollisionBodies(const Entity &source, double distance) const;
    double BodyMotionFraction(const Entity &source, Vec2 to,
                              const std::vector<CollisionBody> &bodies,
                              const std::vector<NavBlocker> &blockers, bool forecast = false) const;
    void ApplySpell(Team team, const Card &card, Vec2 point, PlayId play);
    void Damage(EntityId target, double amount, Team team, const std::string &card, PlayId play,
                EntityId source = 0, const std::string &kind = "attack");
    void Kill(EntityId target, Team team, const std::string &card, PlayId play, EntityId source);
    void SpawnDeathChildren(const Entity &parent, const Card &definition);
    void ApplyInvisibility(Team team, const Card &card, Vec2 point, PlayId play);
    void CloakBody(Entity &body, const Card &card, PlayId play, EntityId source = 0);
    void DropCloakedLocks(Team team, const Card &card, const std::vector<EntityId> &cloaked, PlayId play);
    void PullDeployment(EntityId deployed);
    void UpdateEntity(EntityId id, double dt);
    void Acquire(Entity &source);
    void Attack(Entity &source, const Entity &target);
    void ApplySlow(Entity &victim, const Card &card, PlayId play, EntityId source, Team team);
    void RefreshSlows(Entity &victim);
    void UpdateShockwaves();
    void Move(Entity &source, const Entity &target, double dt);
    bool EngagementPoint(const Entity &source, const Entity &target,
                         const std::vector<NavBlocker> &blockers, Vec2 &point);
    bool NavValid(Vec2 point, const Entity &source, EntityId goal, int bridge = 0) const;
    bool NavSegmentValid(Vec2 from, Vec2 to, const Entity &source, EntityId goal, int bridge = 0) const;
    std::vector<NavBlocker> CollectNavBlockers() const;
    bool NavValid(Vec2 point, const Entity &source, EntityId goal, int bridge,
                  const std::vector<NavBlocker> &blockers) const;
    bool NavSegmentValid(Vec2 from, Vec2 to, const Entity &source, EntityId goal, int bridge,
                         const std::vector<NavBlocker> &blockers) const;
    std::vector<Vec2> FindPath(const Entity &source, const Entity &target, int bridge,
                              const std::vector<NavBlocker> &blockers) const;
    void UpdateAI(Team team, double dt);
    void Decision(Team team, const std::string &label, const std::string &reason);
    bool AIPlay(Team team, int index, Vec2 point, const std::string &reason, double reserve = 0);
};
} // namespace rift
