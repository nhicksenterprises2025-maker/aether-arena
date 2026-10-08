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
} // namespace
void Match::Acquire(Entity &s) {
    const Card *c = FindCard(s.cardId);
    const Entity *locked = Get(s.hardLock);
    if (locked && !locked->dead && CanTarget(s, *locked)) {
        s.target = locked->id;
        s.forcedTarget = 0;
        return;
    }
    s.hardLock = 0;
    const Entity *forced = Get(s.forcedTarget);
    if (forced && !forced->dead && state_.elapsed < s.forcedUntil && CanTarget(s, *forced) &&
        InSight(s, *forced, 2.25)) {
        s.target = forced->id;
        return;
    }
    s.forcedTarget = 0;
    const Entity *current = Get(s.target);
    if (!Crown(s) && c && !c->structuresOnly && current && current->kind == EntityKind::Troop &&
        CanTarget(s, *current) && InSight(s, *current, 2.25))
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
            if (CanTarget(s, t) && InSight(s, t) && D(s.position, t.position) <= c->range + t.radius + .35) {
                double distance = D(s.position, t.position);
                if (distance < best) {
                    best = distance;
                    id = t.id;
                }
            }
        s.target = id;
        return;
    }
    if (!c->structuresOnly) {
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
    double best = std::numeric_limits<double>::max();
    EntityId nearest = 0;
    for (const auto &t : state_.entities)
        if (Structure(t) && CanTarget(s, t)) {
            if (!c->structuresOnly && t.kind != EntityKind::Building)
                continue;
            if (!c->structuresOnly && !InSight(s, t))
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
    if (!c->structuresOnly)
        for (const auto &t : state_.entities)
            if (!t.dead && t.team != s.team && t.kind == EntityKind::Guard && t.lane == s.lane) {
                s.target = t.id;
                return;
            }
    best = std::numeric_limits<double>::max();
    nearest = 0;
    for (const auto &t : state_.entities)
        if (Structure(t) && CanTarget(s, t)) {
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
        const Card *c = FindCard(s.cardId);
        const Entity *target = Get(s.target);
        if (!c || !target || target->dead || !Structure(*target))
            continue;
        if (deployed.kind == EntityKind::Troop) {
            if (c->structuresOnly || !CanTarget(s, deployed))
                continue;
            const double dot = (deployed.position.x - s.position.x) * s.facing.x +
                               (deployed.position.z - s.position.z) * s.facing.z;
            const double pull = (dot >= 0 ? 8. : 5.) + 1.75 + deployed.radius + s.radius * .35;
            if (CombatDistance(s, deployed) > pull)
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
            bool pull =
                c->structuresOnly ? distance <= CombatDistance(s, *target) + .35 : InSight(s, deployed, 1.75);
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
    const Card *c = FindCard(s.cardId);
    double damage = Crown(s) ? s.kind == EntityKind::Core ? 112 : 86 : c ? c->damage : 0;
    double interval = Crown(s) ? s.kind == EntityKind::Core ? .92 : 1.02 : c ? c->attackInterval : 1;
    const double speed = Crown(s) ? 17 : c ? c->projectileSpeed : 0;
    const double splash = c ? c->splash : 0;
    if (c && c->chargeDamage > 0 && s.charged && Structure(t)) {
        damage = c->chargeDamage;
        s.chargeTime = 0;
        s.charged = false;
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
    if (speed > 0) {
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
        Damage(target, damage, team, card, play, source);
        auto *victim = Get(target);
        if (c && c->slowPct > 0 && victim && !victim->dead && victim->kind == EntityKind::Troop &&
            Running(state_.phase)) {
            victim->slowPct = std::max(victim->slowPct, c->slowPct);
            victim->slowUntil = std::max(victim->slowUntil, state_.elapsed + c->slowDuration);
            victim->slowSource = play;
            if (victim->slowTrackedFrom < 0)
                victim->slowTrackedFrom = state_.elapsed;
            statusCredits_[play] = {team, card};
            auto &event = Emit("slow", team);
            event.source = source;
            event.target = target;
            event.playId = play;
            event.cardId = card;
            event.amount = victim->slowPct;
            event.until = victim->slowUntil;
        }
    }
}
void Match::UpdateEntity(EntityId id, double dt) {
    Entity *s = Get(id);
    if (!s || s->dead || !Running(state_.phase))
        return;
    const Card *c = FindCard(s->cardId);
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
        tele.buildingLifetime += std::min(dt, std::max(0., state_.elapsed - s->born));
        s->hp = std::max(0., s->hp - c->hp / c->lifetime * dt);
        if (s->hp <= 1e-9) {
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
                    statusCredits_[copy.playId] = {copy.team, copy.cardId};
                    ++state_.telemetry[I(copy.team)][copy.cardId].stunned;
                    auto &event = Emit("stun", copy.team);
                    event.source = copy.id;
                    event.target = target;
                    event.playId = copy.playId;
                    event.cardId = copy.cardId;
                    event.until = v->stunUntil;
                }
            }
        }
    }
    if (state_.elapsed + 1e-9 < s->stunUntil)
        return;
    if (state_.elapsed >= s->slowUntil)
        s->slowPct = 0;
    const Entity *target = Get(s->target);
    if (target && target->dead) {
        s->target = 0;
        s->hardLock = 0;
        s->path.clear();
        if (std::abs(s->position.z) > 2.69)
            s->bridge = 0;
        target = nullptr;
    }
    const bool approachTroops = c && !c->structuresOnly && !s->hardLock && target && Structure(*target);
    if (s->scanClock <= 0 || !target || approachTroops) {
        Acquire(*s);
        s->scanClock = s->kind == EntityKind::Building ? .12 : .10 + Random() * .06;
        target = Get(s->target);
    }
    if (!target || target->dead || !CanTarget(*s, *target))
        return;
    const double range = Crown(*s) ? s->kind == EntityKind::Core ? 8.9 : 10 : c ? c->range : 0;
    const double distance = CombatDistance(*s, *target);
    if (distance <= range + target->radius + (s->kind == EntityKind::Building ? .35 : 0) + 1e-9) {
        if (s->kind == EntityKind::Troop &&
            (Crown(*target) || (c && c->structuresOnly && target->kind == EntityKind::Building))) {
            s->hardLock = target->id;
            s->forcedTarget = 0;
        }
        s->chargeTime = std::max(0., s->chargeTime - dt * .7);
        if (s->cooldown <= 1e-9)
            Attack(*s, *target);
    } else if (s->kind == EntityKind::Troop)
        Move(*s, *target, dt);
}
} // namespace rift
