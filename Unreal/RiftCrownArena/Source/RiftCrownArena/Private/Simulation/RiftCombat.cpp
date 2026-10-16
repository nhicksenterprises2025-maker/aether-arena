#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rift {
namespace {
int I(Team t) {
    return t == Team::Player ? 0 : 1;
}
double D(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
bool Crown(const Entity &e) {
    return e.kind == EntityKind::Guard || e.kind == EntityKind::Core;
}
bool Structure(const Entity &e) {
    return e.kind != EntityKind::Troop;
}
bool Running(Phase p) {
    return p == Phase::Regulation || p == Phase::Overtime;
}
bool StructuresOnly(const Card &card) {
    return (TargetMaskFor(card) & targets::Structure) != 0 &&
           (TargetMaskFor(card) & (targets::GroundTroop | targets::AirTroop)) == 0;
}
} // namespace
void Match::Acquire(Entity &s) {
    const Card *c = FindEntityDefinition(s.cardId);
    if (s.kind == EntityKind::Troop && !s.flying && !s.bridge &&
        s.position.z * (s.team == Team::Player ? 1 : -1) > arena::RiverHalfWidth + .28)
        s.lane = s.position.x < 0 ? -1 : s.position.x > 0 ? 1 : s.lane < 0 ? -1 : 1;
    const Entity *locked = Get(s.hardLock);
    if (locked && !locked->dead && CanTarget(s, *locked)) {
        s.target = locked->id;
        s.forcedTarget = 0;
        return;
    }
    s.hardLock = 0;
    const Entity *forced = Get(s.forcedTarget);
    if (forced && !forced->dead && state_.elapsed < s.forcedUntil && CanTarget(s, *forced) &&
        InSight(s, *forced)) {
        s.target = forced->id;
        return;
    }
    s.forcedTarget = 0;
    const Entity *current = Get(s.target);
    if (!Crown(s) && c && !StructuresOnly(*c) && current && current->kind == EntityKind::Troop &&
        CanTarget(s, *current) && InSight(s, *current))
        return;
    if (Crown(s)) {
        double best = std::numeric_limits<double>::max();
        EntityId id = 0;
        const double range = s.kind == EntityKind::Core ? 8.9 : 10;
        for (const auto &t : state_.entities)
            if (CanTarget(s, t)) {
                const double d = D(s.position, t.position);
                if (d <= range + t.radius && d < best) {
                    best = d;
                    id = t.id;
                }
            }
        s.target = id;
        return;
    }
    if (!c) {
        s.target = 0;
        return;
    }
    if (c->building) {
        double best = std::numeric_limits<double>::max();
        EntityId id = 0;
        for (const auto &t : state_.entities)
            if (CanTarget(s, t) && InSight(s, t)) {
                double distance = D(s.position, t.position);
                if (distance < best) {
                    best = distance;
                    id = t.id;
                }
            }
        s.target = id;
        return;
    }
    if (!StructuresOnly(*c)) {
        double best = std::numeric_limits<double>::max();
        EntityId id = 0;
        for (const auto &t : state_.entities)
            if (t.kind == EntityKind::Troop && CanTarget(s, t) && InSight(s, t)) {
                const double forward =
                    (t.position.x - s.position.x) * s.facing.x + (t.position.z - s.position.z) * s.facing.z;
                double distance = CombatDistance(s, t) - forward * .06;
                if (distance < best) {
                    best = distance;
                    id = t.id;
                }
            }
        if (id) {
            s.target = id;
            return;
        }
    }
    const Entity *advanceCrown = nullptr;
    // Flying members take a direct route, but share the same landing-side
    // Guard/Core advance as grounded members instead of crossing to the
    // surviving opposite Guard when their own lane opens.
    {
        const int lane = s.bridge ? s.bridge : s.lane ? s.lane : s.position.x < 0 ? -1 : 1;
        for (const auto &t : state_.entities)
            if (!t.dead && t.team != s.team && t.kind == EntityKind::Guard && t.lane == lane) {
                advanceCrown = &t;
                break;
            }
        if (!advanceCrown)
            for (const auto &t : state_.entities)
                if (!t.dead && t.team != s.team && t.kind == EntityKind::Core) {
                    advanceCrown = &t;
                    break;
                }
    }
    double best = std::numeric_limits<double>::max();
    EntityId nearest = 0;
    for (const auto &t : state_.entities)
        if (Structure(t) && CanTarget(s, t)) {
            if (Crown(t) && (!advanceCrown || t.id != advanceCrown->id))
                continue;
            if (!StructuresOnly(*c) && t.kind != EntityKind::Building)
                continue;
            if (t.kind == EntityKind::Building && !InSight(s, t))
                continue;
            const double distance = CombatDistance(s, t);
            if (distance < best) {
                best = distance;
                nearest = t.id;
            }
        }
    if (nearest) {
        s.target = nearest;
        return;
    }
    if (advanceCrown) {
        s.target = advanceCrown->id;
        return;
    }
    if (!StructuresOnly(*c))
        for (const auto &t : state_.entities)
            if (!t.dead && t.team != s.team && t.kind == EntityKind::Guard && t.lane == s.lane) {
                s.target = t.id;
                return;
            }
    best = std::numeric_limits<double>::max();
    nearest = 0;
    for (const auto &t : state_.entities)
        if (Structure(t) && CanTarget(s, t) && (Crown(t) || InSight(s, t))) {
            const double distance = CombatDistance(s, t);
            if (distance < best) {
                best = distance;
                nearest = t.id;
            }
        }
    s.target = nearest;
}
void Match::PullDeployment(EntityId id) {
    const Entity *spawn = Get(id);
    if (!spawn || spawn->dead)
        return;
    const Entity deployed = *spawn;
    for (auto &s : state_.entities) {
        if (s.dead || s.kind != EntityKind::Troop || s.team == deployed.team || s.hardLock)
            continue;
        const Card *c = FindEntityDefinition(s.cardId);
        const Entity *target = Get(s.target);
        if (!c || !target || target->dead || !Structure(*target))
            continue;
        if (deployed.kind == EntityKind::Troop) {
            if (StructuresOnly(*c) || !CanTarget(s, deployed))
                continue;
            if (!InSight(s, deployed))
                continue;
            s.target = s.forcedTarget = deployed.id;
            s.forcedUntil = state_.elapsed + 1.2;
            s.cooldown = std::min(s.cooldown, std::max(.08, c->attackInterval * .32));
            s.chargeTime = 0;
            s.charged = false;
            s.path.clear();
            s.repathClock = 0;
            auto &e = Emit("target_pull", deployed.team);
            e.source = deployed.id;
            e.target = s.id;
            e.cardId = deployed.cardId;
            e.playId = deployed.playId;
            e.until = s.forcedUntil;
        } else if (deployed.kind == EntityKind::Building && CanTarget(s, deployed)) {
            const double distance = CombatDistance(s, deployed);
            const bool pull = InSight(s, deployed) &&
                (!StructuresOnly(*c) || distance <= CombatDistance(s, *target) + .35);
            if (pull) {
                s.target = deployed.id;
                s.cooldown = std::min(s.cooldown, c->attackInterval * .35);
                s.path.clear();
                s.repathClock = 0;
                ++state_.telemetry[I(deployed.team)][deployed.cardId].pulls;
                auto &e = Emit("building_pull", deployed.team);
                e.source = deployed.id;
                e.target = s.id;
                e.cardId = deployed.cardId;
                e.playId = deployed.playId;
            }
        }
    }
}
void Match::Attack(Entity &s, const Entity &t) {
    const Card *c = FindEntityDefinition(s.cardId);
    // A navigation target becomes a structure commitment only when this
    // authoritative attack actually starts, never while waiting on cooldown.
    if (s.kind == EntityKind::Troop &&
        (Crown(t) || (c && StructuresOnly(*c) && t.kind == EntityKind::Building))) {
        s.hardLock = t.id;
        s.forcedTarget = 0;
    }
    double damage = Crown(s) ? s.kind == EntityKind::Core ? 112 : 86 : c ? c->damage : 0;
    double interval = Crown(s) ? s.kind == EntityKind::Core ? .92 : 1.02 : c ? c->attackInterval : 1;
    const double speed = Crown(s) ? 17 : c ? c->projectileSpeed : 0;
    const double splash = c ? c->splash : 0;
    if (c && c->chargeDamage > 0 && s.charged && Structure(t)) {
        damage = c->chargeDamage;
        s.chargeTime = 0;
        s.charged = false;
        auto &spent = Emit("charge_spent", s.team);
        spent.source = s.id;
        spent.target = t.id;
        spent.cardId = s.cardId;
        spent.playId = s.playId;
        spent.amount = damage;
        spent.position = s.position;
    }
    s.cooldown = interval;
    const Vec2 delta{t.position.x - s.position.x, t.position.z - s.position.z};
    const double length = std::hypot(delta.x, delta.z);
    if (length > 1e-9)
        s.facing = {delta.x / length, delta.z / length};
    const EntityId source = s.id, target = t.id;
    const PlayId play = s.playId;
    const Team team = s.team;
    const std::string card = s.cardId;
    auto &e = Emit("attack", team);
    e.source = source;
    e.target = target;
    e.cardId = card;
    e.playId = play;
    e.amount = damage;
    e.position = s.position;
    e.parentEntityId = s.parentEntityId;
    e.attributionCardId = s.attributionCardId;
    if (c && c->shockwaveSpeed > 0) {
        Shockwave wave;
        wave.source = source;
        wave.playId = play;
        wave.team = team;
        wave.cardId = card;
        wave.origin = s.position;
        wave.radius = c->splash;
        wave.speed = c->shockwaveSpeed;
        wave.born = state_.elapsed;
        wave.expires = wave.born + wave.radius / wave.speed;
        auto &event = Emit("shockwave_launch", team);
        wave.id = event.sequence;
        event.source = source;
        event.target = target;
        event.playId = play;
        event.cardId = card;
        event.amount = damage;
        event.until = wave.expires;
        event.position = wave.origin;
        state_.shockwaves.push_back(wave);
    } else if (speed > 0) {
        Projectile p;
        p.id = nextProjectile_++;
        p.source = source;
        p.target = target;
        p.playId = play;
        p.team = team;
        p.cardId = card;
        p.damage = damage;
        p.splash = splash;
        p.origin = s.position;
        const double startY = Crown(s)                         ? 2.48
                              : s.kind == EntityKind::Building ? 2.55
                              : s.flying                       ? .95 + 1.15
                                                               : 1.35;
        const double endY = Structure(t) ? 1.35 : t.flying ? .95 + 1 : .86;
        p.duration = p.remaining = std::max(.11, std::hypot(length, startY - endY) / speed);
        state_.projectiles.push_back(p);
        auto &event = Emit("projectile_launch", team);
        event.source = source;
        event.target = target;
        event.playId = play;
        event.cardId = card;
        event.amount = damage;
        event.until = state_.elapsed + p.duration;
        event.position = p.origin;
    } else {
        // Enumerate the single attack's eligible contacts before applying
        // damage. Visual weapon trails never cause additional hit packets.
        const Vec2 impact = t.position;
        std::vector<EntityId> victims;
        if (c && splash > 0)
            for (const auto &other : state_.entities)
                if (!other.dead && other.team != team && other.id != target && CanHit(*c, other)) {
                    const double padding = other.kind == EntityKind::Building ? other.radius * .35
                                             : Crown(other) ? other.radius * .25 : 0;
                    if (D(other.position, impact) <= splash + padding)
                        victims.push_back(other.id);
                }
        const Vec2 contactSourcePosition = s.position;
        int contacts = 0;
        auto applyContact = [&](EntityId id, const std::string &kind) {
            const auto *before = Get(id);
            const double beforeHealth = before && !before->dead ? before->hp + before->shieldHP : 0.;
            Damage(id, damage, team, card, play, source, kind);
            const auto *after = Get(id);
            if (beforeHealth > 0 && after && after->hp + after->shieldHP < beforeHealth)
                ++contacts;
        };
        applyContact(target, "attack");
        for (auto id : victims) {
            if (!Running(state_.phase))
                break;
            applyContact(id, "splash");
        }
        if (card == "aether_reaper" && contacts >= 2) {
            auto &cleave = Emit("melee_cleave", team);
            cleave.source = source;
            cleave.target = target;
            cleave.cardId = card;
            cleave.playId = play;
            cleave.count = contacts;
            cleave.position = contactSourcePosition;
        }
        auto *victim = Get(target);
        if (c && c->slowPct > 0 && victim && !victim->dead && victim->kind == EntityKind::Troop &&
            Running(state_.phase)) {
            ApplySlow(*victim, *c, play, source, team);
        }
    }
}
void Match::RefreshSlows(Entity &victim) {
    const bool hadComponents = !victim.slows.empty();
    victim.slows.erase(std::remove_if(victim.slows.begin(), victim.slows.end(), [&](const SlowEffect &effect) {
        return effect.until <= state_.elapsed + 1e-9;
    }), victim.slows.end());
    const SlowEffect *dominant = nullptr;
    for (const auto &effect : victim.slows)
        if (!dominant || effect.pct > dominant->pct ||
            (effect.pct == dominant->pct && effect.born > dominant->born))
            dominant = &effect;
    if (dominant) {
        if (victim.slowSource != dominant->playId || victim.slowTrackedFrom < 0)
            victim.slowTrackedFrom = state_.elapsed;
        victim.slowPct = dominant->pct;
        victim.slowUntil = dominant->until;
        victim.slowSource = dominant->playId;
    } else if (hadComponents || state_.elapsed >= victim.slowUntil)
        victim.slowPct = 0;
}
void Match::ApplySlow(Entity &victim, const Card &card, PlayId play, EntityId source, Team team) {
    RefreshSlows(victim);
    auto effect = std::find_if(victim.slows.begin(), victim.slows.end(), [&](const SlowEffect &entry) {
        return entry.cardId == card.id;
    });
    const bool refreshed = effect != victim.slows.end();
    SlowEffect application;
    application.source = source;
    application.playId = play;
    application.team = team;
    application.cardId = card.id;
    application.pct = card.slowPct;
    application.born = state_.elapsed;
    application.until = state_.elapsed + card.slowDuration;
    if (refreshed)
        *effect = application;
    else
        victim.slows.push_back(application);
    statusCredits_[play] = {team, card.id, source};
    RefreshSlows(victim);
    auto &event = Emit("slow", team);
    event.source = source;
    event.target = victim.id;
    event.playId = play;
    event.cardId = card.id;
    event.amount = application.pct;
    event.until = application.until;
    event.reason = refreshed ? "refresh" : "apply";
}
void Match::UpdateShockwaves() {
    for (std::size_t index = 0; index < state_.shockwaves.size();) {
        auto &wave = state_.shockwaves[index];
        const double previous = wave.progress;
        wave.progress = std::min(wave.radius, wave.speed * std::max(0., state_.elapsed - wave.born));
        const Card *card = FindEntityDefinition(wave.cardId);
        if (!card) {
            state_.shockwaves.erase(state_.shockwaves.begin() + index);
            continue;
        }
        std::vector<EntityId> victims;
        if (wave.progress > previous + 1e-9)
            for (const auto &target : state_.entities) {
                if (target.dead || target.team == wave.team || !CanHit(*card, target) ||
                    std::find(wave.hitTargets.begin(), wave.hitTargets.end(), target.id) != wave.hitTargets.end())
                    continue;
                const auto start = bodyStepStarts_.find(target.id);
                const Vec2 from = start == bodyStepStarts_.end() ? target.position : start->second;
                // The authored wave outruns every troop. Intersect its moving
                // front with the body's swept radial interval, rather than
                // turning the already-passed interior into a lingering aura.
                if (D(target.position, wave.origin) <= wave.progress + target.radius + 1e-9 &&
                    D(from, wave.origin) + target.radius >= previous - 1e-9) {
                    wave.hitTargets.push_back(target.id);
                    victims.push_back(target.id);
                }
            }
        const Shockwave copy = wave;
        for (EntityId target : victims) {
            Damage(target, card->damage, copy.team, copy.cardId, copy.playId, copy.source, "shockwave");
            if (!Running(state_.phase))
                return;
            auto *victim = Get(target);
            if (victim && !victim->dead && victim->kind == EntityKind::Troop && card->slowPct > 0)
                ApplySlow(*victim, *card, copy.playId, copy.source, copy.team);
        }
        if (state_.elapsed + 1e-9 >= copy.expires)
            state_.shockwaves.erase(state_.shockwaves.begin() + index);
        else
            ++index;
    }
}
double Match::AttackReach(const Entity &s, const Entity &t) const {
    if (Crown(s))
        return (s.kind == EntityKind::Core ? 8.9 : 10.) + t.radius;
    const Card *c = FindEntityDefinition(s.cardId);
    if (!c)
        return 0;
    // Direct melee range is measured from the two body edges. Ranged shots
    // and stationary buildings retain their existing center-based reach.
    const double bodyReach = s.kind == EntityKind::Troop && c->projectileSpeed <= 0
                                 ? s.radius : 0.;
    return c->range + t.radius + bodyReach + (s.kind == EntityKind::Building ? .35 : 0.);
}
void Match::UpdateEntity(EntityId id, double dt) {
    Entity *s = Get(id);
    if (!s || s->dead || !Running(state_.phase))
        return;
    const Card *c = FindEntityDefinition(s->cardId);
    if (s->invisibleUntil > 0 && state_.elapsed + 1e-9 >= s->invisibleUntil) {
        auto &event = Emit("invisibility_expire", s->team);
        event.target = s->id;
        event.targetTeam = s->team;
        event.source = s->invisibilitySource;
        event.playId = s->invisibilityPlayId;
        event.cardId = s->invisibilityCardId;
        event.until = s->invisibleUntil;
        event.position = s->position;
        s->invisibleUntil = 0;
    }
    s->cooldown -= dt;
    s->scanClock -= dt;
    if (s->kind == EntityKind::Core && !s->active) {
        for (const auto &e : state_.entities)
            if (e.team == s->team && e.kind == EntityKind::Guard && e.dead)
                s->active = true;
        if (!s->active) {
            s->target = 0;
            return;
        }
    }
    if (c && c->building) {
        auto &tele = state_.telemetry[I(s->team)][c->id];
        const double activeDt = std::min(dt, std::max(0., state_.elapsed - s->born));
        tele.buildingLifetime += activeDt;
        state_.entityTelemetry[I(s->team)][id].observed.buildingLifetime += activeDt;
        s->hp = std::max(0., s->hp - c->hpDecayPerSecond * dt);
        if (s->hp <= 1e-9 || (c->hardExpiry && c->lifetime > 0 &&
                             state_.elapsed + 1e-9 >= s->born + c->lifetime)) {
            Kill(id, s->team, "", 0, 0);
            return;
        }
    }
    if (c && c->auraDamage > 0) {
        s->auraClock += dt;
        if (s->auraClock + 1e-9 >= c->auraInterval) {
            s->auraClock -= c->auraInterval;
            const Entity copy = *s;
            auto &pulse = Emit("aura", s->team);
            pulse.source = s->id;
            pulse.playId = s->playId;
            pulse.cardId = s->cardId;
            pulse.position = s->position;
            pulse.amount = c->auraRadius;
            std::vector<EntityId> victims;
            for (const auto &t : state_.entities)
                if (!t.dead && t.kind == EntityKind::Troop && t.team != s->team &&
                    D(s->position, t.position) <= c->auraRadius + t.radius)
                    victims.push_back(t.id);
            for (auto target : victims) {
                Damage(target, c->auraDamage, copy.team, copy.cardId, copy.playId, copy.id, "aura");
                if (!Running(state_.phase))
                    return;
                auto *v = Get(target);
                if (v && !v->dead) {
                    v->stunUntil = std::max(v->stunUntil, state_.elapsed + c->stunDuration);
                    v->stunSource = copy.playId;
                    if (v->stunTrackedFrom < 0)
                        v->stunTrackedFrom = state_.elapsed;
                    statusCredits_[copy.playId] = {copy.team, copy.cardId, copy.id};
                    ++state_.telemetry[I(copy.team)][copy.cardId].stunned;
                    auto &event = Emit("stun", copy.team);
                    event.source = copy.id;
                    event.target = target;
                    event.playId = copy.playId;
                    event.cardId = copy.cardId;
                    event.until = v->stunUntil;
                }
            }
            s = Get(id); // A lethal aura may insert four children and move every vector element.
            if (!s || s->dead) return;
        }
    }
    RefreshSlows(*s);
    if (state_.elapsed + 1e-9 < s->stunUntil)
        return;
    const Entity *target = Get(s->target);
    if (target && (target->dead || !CanTarget(*s, *target))) {
        s->target = 0;
        s->hardLock = 0;
        s->path.clear();
        if (std::abs(s->position.z) > arena::RiverHalfWidth + 1.04)
            s->bridge = 0;
        target = nullptr;
    }
    // Crown advance is navigation, and an established melee structure lock
    // remains engaged. Every other deployable-card pursuit uses the same
    // strict detection boundary on every tick, not a relaxed scan interval.
    if (!Crown(*s) && target && !Crown(*target) && !s->hardLock && !InSight(*s, *target)) {
        s->target = s->forcedTarget = 0;
        s->path.clear();
        target = nullptr;
    }
    const bool approachTroops = c && !StructuresOnly(*c) && !s->hardLock && target && Structure(*target);
    if (s->scanClock <= 0 || !target || approachTroops) {
        Acquire(*s);
        s->scanClock = s->kind == EntityKind::Building ? .12 : .10 + Random() * .06;
        target = Get(s->target);
    }
    if (!target || target->dead || !CanTarget(*s, *target))
        return;
    bool moved = false;
    auto arrival = engagementSlots_.find(s->id);
    const bool inReach = CombatDistance(*s, *target) <= AttackReach(*s, *target) + 1e-9;
    const bool friendlyYield = arrival != engagementSlots_.end() &&
        arrival->second.target == target->id && arrival->second.yieldingToFriend;
    const bool holdMeleeContact = arena::NavigationRevision >= 10 && c && c->projectileSpeed <= 0 &&
        s->kind == EntityKind::Troop && target->kind == EntityKind::Troop &&
        target->team != s->team &&
        (arena::NavigationRevision >= 11 || s->memberCount > 1 || target->memberCount > 1) &&
        inReach && !friendlyYield;
    if (holdMeleeContact) {
        // Opposing fighters do not orbit an ideal arrival goal after contact.
        // Reserve the actual legal stance so neighbours route around its body.
        // A deliberate same-team slot exchange can still make room for a friend.
        engagementSlots_[s->id] = {target->id,
            {s->position.x - target->position.x, s->position.z - target->position.z},
            s->position, 0, state_.elapsed};
        s->path.clear();
        s->repathClock = 0;
        arrival = engagementSlots_.find(s->id);
    }
    if (s->kind == EntityKind::Troop && c && c->count > 1 && c->projectileSpeed <= 0 &&
        !holdMeleeContact &&
        (arrival == engagementSlots_.end() || arrival->second.target != target->id) &&
        CombatDistance(*s, *target) <= AttackReach(*s, *target) + 1e-9) {
        // The front row can enter melee reach during a bridge exit, or be
        // deployed already within reach. It needs an arrival reservation too;
        // otherwise an unaligned early attacker can seal the rest of its swarm.
        const auto blockers = s->flying ? std::vector<NavBlocker>{} : CollectNavBlockers();
        Vec2 point;
        EngagementPoint(*s, *target, blockers, point);
        arrival = engagementSlots_.find(s->id);
    }
    if (s->kind == EntityKind::Troop && arrival != engagementSlots_.end() &&
        arrival->second.target == target->id && !holdMeleeContact &&
        CombatDistance(*s, *target) <= AttackReach(*s, *target) + 1e-9) {
        const Vec2 point{target->position.x + arrival->second.offset.x,
                          target->position.z + arrival->second.offset.z};
        if (D(s->position, point) > .0005) {
            // Finish the approach while cooling down instead of freezing a
            // fraction of a step off-slot and blocking another swarm member.
            // This is one normal swept movement, followed by a fresh reach test.
            Move(*s, *target, dt);
            moved = true;
        } else arrival->second.yieldingToFriend = false;
    }
    const double distance = CombatDistance(*s, *target);
    if (distance <= AttackReach(*s, *target) + 1e-9) {
        s->chargeTime = std::max(0., s->chargeTime - dt * .7);
        if (s->cooldown <= 1e-9)
            Attack(*s, *target);
    } else if (s->kind == EntityKind::Troop && !moved)
        Move(*s, *target, dt);
}
} // namespace rift
