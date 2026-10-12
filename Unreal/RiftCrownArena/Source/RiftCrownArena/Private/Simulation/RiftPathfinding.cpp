#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace rift {
namespace {
constexpr int HalfX = arena::HalfWidth, HalfZ = arena::HalfHeight;
double D(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
double Clamp(double v, double a, double b) {
    return std::max(a, std::min(b, v));
}
double SegmentDistanceSquared(Vec2 from, Vec2 to, Vec2 point) {
    const double dx = to.x - from.x, dz = to.z - from.z;
    const double lengthSquared = dx * dx + dz * dz;
    // A short relative sweep still has an endpoint. Treat only an actually
    // zero-length segment as a point, otherwise contact can tunnel by microns
    // and make the next fixed step reject every possible escape direction.
    const double along = lengthSquared > 0.
                             ? Clamp(((point.x - from.x) * dx + (point.z - from.z) * dz) /
                                         lengthSquared, 0., 1.)
                             : 0.;
    const double x = from.x + dx * along - point.x, z = from.z + dz * along - point.z;
    return x * x + z * z;
}
bool ArenaSegmentValid(Vec2 from, Vec2 to, double radius, int bridge) {
    const double edge = .40 + radius * .72;
    if (std::abs(from.x) > HalfX - edge || std::abs(from.z) > HalfZ - edge ||
        std::abs(to.x) > HalfX - edge || std::abs(to.z) > HalfZ - edge)
        return false;
    // A segment is legal through the river only if the whole part inside its
    // banks stays on one bridge, including off-grid entry and exit points.
    constexpr double Bank = arena::RiverHalfWidth + .28;
    const double dz = to.z - from.z;
    double enter = 0., leave = 1.;
    if (std::abs(dz) < 1e-12) {
        if (std::abs(from.z) >= Bank)
            return true;
    } else {
        const double a = (-Bank - from.z) / dz, b = (Bank - from.z) / dz;
        enter = std::max(0., std::min(a, b));
        leave = std::min(1., std::max(a, b));
        if (enter >= leave)
            return true;
    }
    const double half = std::max(.34, arena::BridgeWidth / 2 - .16 - radius * .92);
    const double x1 = from.x + (to.x - from.x) * enter,
                 x2 = from.x + (to.x - from.x) * leave;
    for (int lane : {-1, 1})
        if ((!bridge || bridge == lane) && std::abs(x1 - lane * arena::BridgeCenterX) <= half &&
            std::abs(x2 - lane * arena::BridgeCenterX) <= half)
            return true;
    return false;
}
bool SameCollisionLayer(const Entity &a, const Entity &b) {
    const bool aAir = a.kind == EntityKind::Troop && a.flying;
    const bool bAir = b.kind == EntityKind::Troop && b.flying;
    return aAir == bAir;
}
double BodyClearance(const Entity &a, EntityKind otherKind, double otherRadius) {
    return a.radius + otherRadius +
           (a.kind != EntityKind::Troop || otherKind != EntityKind::Troop ? .22 : .02);
}
struct Cell {
    int x = 0, z = 0;
    bool operator==(const Cell &b) const {
        return x == b.x && z == b.z;
    }
};
double Heuristic(Cell a, Cell b) {
    double x = std::abs(a.x - b.x), z = std::abs(a.z - b.z);
    return x + z + (1.4142135623730951 - 2) * std::min(x, z);
}
} // namespace
bool Match::NavValid(Vec2 p, const Entity &s, EntityId goal, int bridge) const {
    const double edge = .40 + s.radius * .72;
    if (std::abs(p.x) > HalfX - edge || std::abs(p.z) > HalfZ - edge)
        return false;
    if (std::abs(p.z) < arena::RiverHalfWidth + .28) {
        const double half = std::max(.34, arena::BridgeWidth / 2 - .16 - s.radius * .92);
        bool legal = false;
        for (int lane : {-1, 1})
            if ((!bridge || bridge == lane) && std::abs(p.x - lane * arena::BridgeCenterX) <= half)
                legal = true;
        if (!legal)
            return false;
    }
    for (const auto &e : state_.entities)
        if (!e.dead && e.kind != EntityKind::Troop && e.id != goal && e.id != s.id &&
            D(p, e.position) < e.radius + s.radius + .22)
            return false;
    return true;
}
bool Match::NavSegmentValid(Vec2 from, Vec2 to, const Entity &s, EntityId goal, int bridge) const {
    if (!ArenaSegmentValid(from, to, s.radius, bridge))
        return false;
    for (const auto &e : state_.entities)
        if (!e.dead && e.kind != EntityKind::Troop && e.id != goal && e.id != s.id) {
            const double clearance = e.radius + s.radius + .22;
            if (SegmentDistanceSquared(from, to, e.position) < clearance * clearance)
                return false;
        }
    return true;
}
std::vector<Match::NavBlocker> Match::CollectNavBlockers() const {
    std::vector<NavBlocker> blockers;
    blockers.reserve(16);
    for (const auto &e : state_.entities)
        if (!e.dead && e.kind != EntityKind::Troop)
            blockers.push_back({e.id, e.position, e.radius, e.kind});
    return blockers;
}
bool Match::NavValid(Vec2 p, const Entity &s, EntityId goal, int bridge,
                     const std::vector<NavBlocker> &blockers) const {
    const double edge = .40 + s.radius * .72;
    if (std::abs(p.x) > HalfX - edge || std::abs(p.z) > HalfZ - edge)
        return false;
    if (std::abs(p.z) < arena::RiverHalfWidth + .28) {
        const double half = std::max(.34, arena::BridgeWidth / 2 - .16 - s.radius * .92);
        bool legal = false;
        for (int lane : {-1, 1})
            if ((!bridge || bridge == lane) && std::abs(p.x - lane * arena::BridgeCenterX) <= half)
                legal = true;
        if (!legal)
            return false;
    }
    for (const auto &e : blockers)
        if (e.id != goal && e.id != s.id &&
            D(p, e.position) < BodyClearance(s, e.kind, e.radius) -
                                  (e.kind == EntityKind::Troop ? 1e-8 : 0.))
            return false;
    return true;
}
bool Match::NavSegmentValid(Vec2 from, Vec2 to, const Entity &s, EntityId goal, int bridge,
                            const std::vector<NavBlocker> &blockers) const {
    if (!ArenaSegmentValid(from, to, s.radius, bridge))
        return false;
    const double minX = std::min(from.x, to.x), maxX = std::max(from.x, to.x);
    const double minZ = std::min(from.z, to.z), maxZ = std::max(from.z, to.z);
    for (const auto &e : blockers)
        if (e.id != goal && e.id != s.id) {
            const double clearance = BodyClearance(s, e.kind, e.radius);
            if (e.position.x + clearance + 1e-6 < minX || e.position.x - clearance - 1e-6 > maxX ||
                e.position.z + clearance + 1e-6 < minZ || e.position.z - clearance - 1e-6 > maxZ)
                continue;
            // Dynamic contact can differ by one ULP after a prior body's
            // sweep. The independent relative-body sweep still enforces the
            // exact skin; navigation must not reject every outward escape.
            if (SegmentDistanceSquared(from, to, e.position) < clearance * clearance -
                    (e.kind == EntityKind::Troop ? 1e-10 : 0.))
                return false;
        }
    return true;
}
bool Match::BodyPlacementValid(Vec2 point, const Entity &s, bool sandbox,
                               const std::vector<Entity> &planned) const {
    const Card *card = FindEntityDefinition(s.cardId);
    if (!sandbox && !s.parentEntityId && (!card || !CanPlace(s.team, *card, point)))
        return false;
    const double edge = .40 + s.radius * .72;
    if (std::abs(point.x) > HalfX - edge || std::abs(point.z) > HalfZ - edge)
        return false;
    if (!s.flying && !sandbox && !NavValid(point, s, 0))
        return false;
    auto blocked = [&](const Entity &other) {
        if (other.dead || other.id == s.id || !SameCollisionLayer(s, other))
            return false;
        return D(point, other.position) < BodyClearance(s, other.kind, other.radius) - 1e-10;
    };
    for (const auto &other : state_.entities)
        if (blocked(other))
            return false;
    for (const auto &other : planned)
        if (blocked(other))
            return false;
    return true;
}
bool Match::ResolveBodyPlacement(Vec2 requested, const Entity &s, bool sandbox,
                                 const std::vector<Entity> &planned, Vec2 &result) const {
    if (BodyPlacementValid(requested, s, sandbox, planned)) {
        result = requested;
        return true;
    }
    double best = std::numeric_limits<double>::infinity();
    auto consider = [&](Vec2 point) {
        const double dx = point.x - requested.x, dz = point.z - requested.z;
        const double distance = dx * dx + dz * dz;
        if (distance + 1e-10 < best && BodyPlacementValid(point, s, sandbox, planned)) {
            best = distance;
            result = point;
        }
    };
    const double edge = .40 + s.radius * .72 + .001;
    consider({Clamp(requested.x, -HalfX + edge, HalfX - edge),
              Clamp(requested.z, -HalfZ + edge, HalfZ - edge)});
    constexpr double Tau = 6.28318530717958647692;
    const double forward = std::atan2(s.facing.z, s.facing.x);
    auto around = [&](const Entity &other) {
        if (other.dead || other.id == s.id || !SameCollisionLayer(s, other))
            return;
        const double clearance = BodyClearance(s, other.kind, other.radius) + .001;
        const double distance = D(requested, other.position);
        const double angle = distance > 1e-9
                                 ? std::atan2(requested.z - other.position.z, requested.x - other.position.x)
                                 : forward;
        consider({other.position.x + std::cos(angle) * clearance,
                  other.position.z + std::sin(angle) * clearance});
        if (distance > clearance + 3.)
            return;
        for (int n = 0; n < 64; ++n) {
            const double bearing = forward + n * Tau / 64.;
            consider({other.position.x + std::cos(bearing) * clearance,
                      other.position.z + std::sin(bearing) * clearance});
        }
    };
    for (const auto &other : state_.entities)
        around(other);
    for (const auto &other : planned)
        around(other);
    if (std::isfinite(best))
        return true;
    // Only a fully surrounded/blocked hand drop needs this bounded fallback.
    // Search physical free space without ever spending for an overlapping body.
    for (int z = -HalfZ + 1; z < HalfZ; ++z)
        for (int x = -HalfX + 1; x < HalfX; ++x)
            consider({static_cast<double>(x), static_cast<double>(z)});
    return std::isfinite(best);
}
std::vector<Match::CollisionBody> Match::CollisionBodies(const Entity &s, double distance) const {
    std::vector<CollisionBody> bodies;
    for (const auto &other : state_.entities) {
        if (other.dead || other.id == s.id || !SameCollisionLayer(s, other))
            continue;
        const auto origin = bodyStepStarts_.find(other.id);
        const Vec2 from = origin == bodyStepStarts_.end() ? other.position : origin->second;
        const double reach = distance + BodyClearance(s, other.kind, other.radius);
        if (SegmentDistanceSquared(from, other.position, s.position) > reach * reach)
            continue;
        const Vec2 relativeStart{s.position.x - from.x, s.position.z - from.z};
        const Vec2 forecastStart{s.position.x - other.position.x, s.position.z - other.position.z};
        const double clearance = BodyClearance(s, other.kind, other.radius);
        bodies.push_back({from, other.position, other.radius, other.kind, other.team,
            relativeStart, forecastStart,
            relativeStart.x * relativeStart.x + relativeStart.z * relativeStart.z,
            forecastStart.x * forecastStart.x + forecastStart.z * forecastStart.z,
            clearance * clearance});
    }
    return bodies;
}
double Match::BodyMotionFraction(const Entity &s, Vec2 to,
                                 const std::vector<CollisionBody> &bodies,
                                 const std::vector<NavBlocker> &blockers, bool forecast) const {
    const Vec2 from = s.position;
    const double edge = .40 + s.radius * .72;
    // Prefixes of a legal complete static segment are legal as well. The
    // relative moving-body sweeps still use their own original time domain.
    const bool fullStaticValid = s.flying
        ? std::abs(from.x) <= HalfX - edge && std::abs(from.z) <= HalfZ - edge &&
          std::abs(to.x) <= HalfX - edge && std::abs(to.z) <= HalfZ - edge
        : NavSegmentValid(from, to, s, 0, s.bridge, blockers);
    auto valid = [&](double fraction) {
        const Vec2 point{from.x + (to.x - from.x) * fraction,
                         from.z + (to.z - from.z) * fraction};
        if (!fullStaticValid) {
            if (s.flying) {
                if (std::abs(point.x) > HalfX - edge || std::abs(point.z) > HalfZ - edge)
                    return false;
            } else if (!NavSegmentValid(from, point, s, 0, s.bridge, blockers))
                return false;
        }
        for (const auto &body : bodies) {
            const Vec2 relativeFrom = forecast ? body.forecastStart : body.relativeStart;
            const Vec2 relativeTo{point.x - body.to.x, point.z - body.to.z};
            const double clearanceSquared = body.clearanceSquared;
            const double startSquared = forecast ? body.forecastSquared : body.startSquared;
            const double minimum = SegmentDistanceSquared(relativeFrom, relativeTo, {});
            // Bisection must not put a contact one ULP inside next tick's
            // validity boundary and permanently reject even an outward slide.
            // New approaches keep a positive numerical guard. At an existing
            // near-exact contact only non-worsening relative motion is legal;
            // the numerical band is a tiny fraction of a pixel.
            if (std::abs(startSquared - clearanceSquared) <= 1e-8) {
                if (minimum < startSquared - 1e-12)
                    return false;
            } else if (minimum < clearanceSquared + 1e-9)
                return false;
        }
        return true;
    };
    if (valid(1.))
        return 1.;
    // Zero is legal because earlier troops were swept against this stationary
    // body. Bisection clips the complete relative trajectory, including the
    // straight interpolation used by the renderer, rather than only endpoints.
    double low = 0., high = 1.;
    for (int n = 0; n < 18; ++n) {
        const double middle = (low + high) * .5;
        if (valid(middle))
            low = middle;
        else
            high = middle;
    }
    return low;
}
std::vector<Vec2> Match::FindPath(const Entity &s, const Entity &t, int bridge) const {
    if (s.flying)
        return {t.position};
    return FindPath(s, t, bridge, CollectNavBlockers());
}
std::vector<Vec2> Match::FindPath(const Entity &s, const Entity &t, int bridge,
                                const std::vector<NavBlocker> &blockers) const {
    if (s.flying)
        return {t.position};
    if (!bridge && s.position.z * t.position.z < 0)
        bridge = s.bridge ? s.bridge : s.position.x < 0 ? -1 : s.position.x > 0 ? 1 : s.lane < 0 ? -1 : 1;
    struct Entry {
        Cell c;
        double f = 0, g = 0;
        std::uint64_t sequence = 0;
    };
    struct Order {
        bool operator()(const Entry &a, const Entry &b) const {
            return a.f != b.f ? a.f > b.f : a.sequence > b.sequence;
        }
    };
    // Ordinary routes use the inexpensive tile grid; dense placed-building
    // pockets use half tiles to represent their physical escape corridors.
    // Contact approaches use a bounded quarter-tile patch so a body-sized
    // corridor need not detour to a distant integer arrival connector.
    const bool local = t.id == 0;
    int nearbyBuildings = 0;
    if (!local)
        for (const auto &obstacle : blockers)
            if (obstacle.kind == EntityKind::Building &&
                D(s.position, obstacle.position) < s.radius + obstacle.radius + 3.) ++nearbyBuildings;
    const int scale = local ? 4 : nearbyBuildings >= 2 ? 2 : 1;
    const int minX = !local ? -HalfX * scale : std::max(-HalfX * scale,
        static_cast<int>(std::floor(std::min(s.position.x, t.position.x) * scale)) - 5 * scale);
    const int maxX = !local ? HalfX * scale : std::min(HalfX * scale,
        static_cast<int>(std::ceil(std::max(s.position.x, t.position.x) * scale)) + 5 * scale);
    const int minZ = !local ? -HalfZ * scale : std::max(-HalfZ * scale,
        static_cast<int>(std::floor(std::min(s.position.z, t.position.z) * scale)) - 5 * scale);
    const int maxZ = !local ? HalfZ * scale : std::min(HalfZ * scale,
        static_cast<int>(std::ceil(std::max(s.position.z, t.position.z) * scale)) + 5 * scale);
    const int width = maxX - minX + 1, size = width * (maxZ - minZ + 1);
    auto index = [&](Cell c) { return (c.z - minZ) * width + c.x - minX; };
    auto cell = [&](int n) { return Cell{n % width + minX, n / width + minZ}; };
    auto navPoint = [&](Cell c) { return Vec2{double(c.x) / scale, double(c.z) / scale}; };
    auto grid = [&](Vec2 p) { return Cell{int(std::floor(p.x * scale + .5)), int(std::floor(p.z * scale + .5))}; };
    auto heuristic = [&](Cell a, Cell b) { return Heuristic(a, b) / scale; };
    struct Obstacle {
        Vec2 position;
        double clearanceSquared;
        double radius = 0;
    };
    std::vector<Obstacle> obstacles;
    for (const auto &e : blockers)
        if (e.id != t.id && e.id != s.id) {
            const double radius = BodyClearance(s, e.kind, e.radius);
            obstacles.push_back({e.position, radius * radius, radius});
        }
    const double edge = .40 + s.radius * .72, half = std::max(.34, arena::BridgeWidth / 2 - .16 - s.radius * .92);
    std::vector<std::uint8_t> walkable(size, 0), closed(size, 0);
    for (int z = minZ; z <= maxZ; ++z)
        for (int x = minX; x <= maxX; ++x) {
            Cell c{x, z};
            const Vec2 location = navPoint(c);
            bool valid = std::abs(location.x) <= HalfX - edge && std::abs(location.z) <= HalfZ - edge;
            if (valid && std::abs(location.z) < arena::RiverHalfWidth + .28) {
                valid = false;
                for (int lane : {-1, 1})
                    if ((!bridge || bridge == lane) && std::abs(location.x - lane * arena::BridgeCenterX) <= half)
                        valid = true;
            }
            walkable[index(c)] = valid ? 1 : 0;
        }
    // A cell can be blocked only inside an obstacle's conservative bounds.
    // Keep the exact distance predicate while avoiding every-obstacle work
    // for the rest of the arena. One extra cell protects rounding at bounds.
    for (const auto &o : obstacles) {
        const double radius = std::sqrt(o.clearanceSquared);
        const int firstX = std::max(minX, int(std::floor((o.position.x - radius) * scale)) - 1);
        const int lastX = std::min(maxX, int(std::ceil((o.position.x + radius) * scale)) + 1);
        const int firstZ = std::max(minZ, int(std::floor((o.position.z - radius) * scale)) - 1);
        const int lastZ = std::min(maxZ, int(std::ceil((o.position.z + radius) * scale)) + 1);
        for (int z = firstZ; z <= lastZ; ++z)
            for (int x = firstX; x <= lastX; ++x) {
                const Vec2 location = navPoint({x, z});
                const double dx = location.x - o.position.x, dz = location.z - o.position.z;
                if (dx * dx + dz * dz < o.clearanceSquared)
                    walkable[index({x, z})] = 0;
            }
    }
    auto valid = [&](Cell c) {
        return c.x >= minX && c.x <= maxX && c.z >= minZ && c.z <= maxZ && walkable[index(c)] != 0;
    };
    auto segmentValid = [&](Vec2 from, Vec2 to) {
        if (!ArenaSegmentValid(from, to, s.radius, bridge))
            return false;
        const double minX = std::min(from.x, to.x), maxX = std::max(from.x, to.x);
        const double minZ = std::min(from.z, to.z), maxZ = std::max(from.z, to.z);
        for (const auto &o : obstacles) {
            if (o.position.x + o.radius + 1e-6 < minX || o.position.x - o.radius - 1e-6 > maxX ||
                o.position.z + o.radius + 1e-6 < minZ || o.position.z - o.radius - 1e-6 > maxZ)
                continue;
            if (SegmentDistanceSquared(from, to, o.position) < o.clearanceSquared)
                return false;
        }
        return true;
    };
    // Moving troops add bounded local route cost, never solid grid cells.
    // Rasterizing just their nearby cells keeps A* independent of crowd size
    // at each visited node and lets a queue continue through its own bridge.
    std::vector<double> crowdCost(size, 0.);
    for (const auto &other : state_.entities) {
        if (other.dead || other.flying || other.kind != EntityKind::Troop ||
            other.id == s.id || other.id == t.id)
            continue;
        const Cell center = grid(other.position);
        const double reach = s.radius + other.radius + .9;
        const int ring = static_cast<int>(std::ceil(reach * scale));
        for (int z = std::max(minZ, center.z - ring); z <= std::min(maxZ, center.z + ring); ++z)
            for (int x = std::max(minX, center.x - ring); x <= std::min(maxX, center.x + ring); ++x) {
                const double distance = D(navPoint({x, z}), other.position);
                if (distance < reach) {
                    double &value = crowdCost[index({x, z})];
                    value = std::min(.85, value + .42 * (1. - distance / reach));
                }
            }
    }
    Cell start = grid(s.position);
    Cell goal = grid(t.position);
    if (start.x < minX || start.x > maxX || start.z < minZ || start.z > maxZ)
        return {};
    if (!valid(goal) || !segmentValid(navPoint(goal), t.position)) {
        const Cell raw = goal;
        bool found = false;
        for (int ring = 1; ring <= 5 * scale && !found; ++ring) {
            double best = 1e9;
            for (int x = -ring; x <= ring; ++x)
                for (int z = -ring; z <= ring; ++z)
                    if (std::abs(x) == ring || std::abs(z) == ring) {
                        Cell c{raw.x + x, raw.z + z};
                        if (valid(c) && segmentValid(navPoint(c), t.position)) {
                            double distance = D(navPoint(c), t.position);
                            if (distance < best) {
                                best = distance;
                                goal = c;
                                found = true;
                            }
                        }
                    }
        }
        if (!found)
            return {};
    }
    std::priority_queue<Entry, std::vector<Entry>, Order> open;
    std::vector<double> cost(size, std::numeric_limits<double>::infinity());
    std::vector<int> parents(size, -1);
    std::uint64_t sequence = 0;
    // A half-tile source may sit inside a pocket whose nearest grid node
    // sends it into its neighbours. Start from every visible local connector
    // and include that connector's real distance and bounded crowd cost. This
    // allows a short retreat around a friendly building without making moving
    // troops permanent walls or privileging one snapped starting tile.
    const double ConnectorReach = scale == 1 ? 4. : 1.25;
    const int connectorCells = int(std::ceil(ConnectorReach * scale));
    const Cell origin = grid(s.position);
    std::vector<Obstacle> localCrowd;
    for (const auto &other : state_.entities) {
        if (other.dead || other.flying || other.kind != EntityKind::Troop ||
            other.id == s.id || other.id == t.id)
            continue;
        const double radius = s.radius + other.radius + .04;
        if (D(s.position, other.position) <= ConnectorReach + radius)
            localCrowd.push_back({other.position, radius * radius});
    }
    for (int z = std::max(minZ, origin.z - connectorCells);
         z <= std::min(maxZ, origin.z + connectorCells); ++z)
        for (int x = std::max(minX, origin.x - connectorCells);
             x <= std::min(maxX, origin.x + connectorCells); ++x) {
            const Cell candidate{x, z};
            const Vec2 point = navPoint(candidate);
            const double distance = D(s.position, point);
            if (distance > ConnectorReach || !valid(candidate) || !segmentValid(s.position, point))
                continue;
            double traffic = 0.;
            for (const auto &other : localCrowd) {
                const double radius = std::sqrt(other.clearanceSquared);
                const double closest = std::sqrt(SegmentDistanceSquared(s.position, point, other.position));
                if (closest < radius)
                    traffic += 5. * (1. - closest / (radius + 1e-6));
            }
            const double g = distance + std::min(8., traffic);
            cost[index(candidate)] = g;
            open.push({candidate, g + heuristic(candidate, goal), g, sequence++});
        }
    if (open.empty()) {
        // Only an isolated pocket needs the full-arena nearest-visible search.
        // Never connect a route through a blocked start or across a tower corner.
        double best = std::numeric_limits<double>::infinity();
        bool found = false;
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x) {
                const Cell candidate{x, z};
                const double distance = D(s.position, navPoint(candidate));
                if (distance < best && valid(candidate) && segmentValid(s.position, navPoint(candidate))) {
                    best = distance;
                    start = candidate;
                    found = true;
                }
            }
        if (!found)
            return {};
        cost[index(start)] = best;
        open.push({start, best + heuristic(start, goal), best, sequence++});
    }
    int examined = 0;
    while (!open.empty() && examined < size) {
        const auto e = open.top();
        open.pop();
        const int current = index(e.c);
        if (closed[current])
            continue;
        closed[current] = 1;
        ++examined;
        if (e.c == goal) {
            std::vector<Cell> cells{goal};
            int n = index(goal);
            while (parents[n] >= 0) {
                n = parents[n];
                cells.push_back(cell(n));
            }
            const Cell root = cell(n);
            std::reverse(cells.begin(), cells.end());
            std::vector<Vec2> path;
            for (std::size_t i = 1; i < cells.size(); ++i) {
                if (i + 1 < cells.size()) {
                    const auto a = cells[i - 1], b = cells[i], next = cells[i + 1];
                    if ((b.x - a.x) == (next.x - b.x) && (b.z - a.z) == (next.z - b.z))
                        continue;
                }
                path.push_back(navPoint(cells[i]));
            }
            if ((path.empty() && D(s.position, navPoint(root)) > .01) ||
                (!path.empty() && !segmentValid(s.position, path.front())))
                path.insert(path.begin(), navPoint(root));
            if (segmentValid(path.empty() ? s.position : path.back(), t.position) &&
                (path.empty() || D(path.back(), t.position) > .01))
                path.push_back(t.position);
            return path;
        }
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0)
                    continue;
                Cell n{e.c.x + dx, e.c.z + dz};
                if (!valid(n) || closed[index(n)])
                    continue;
                if (dx && dz && (!valid({e.c.x + dx, e.c.z}) || !valid({e.c.x, e.c.z + dz})))
                    continue;
                if (!segmentValid(navPoint(e.c), navPoint(n)))
                    continue;
                const double traffic = crowdCost[index(n)];
                double g = e.g + ((dx && dz ? 1.4142135623730951 : 1) + traffic) / scale;
                if (g < cost[index(n)]) {
                    cost[index(n)] = g;
                    parents[index(n)] = current;
                    open.push({n, g + heuristic(n, goal), g, sequence++});
                }
            }
    }
    return {};
}
bool Match::EngagementPoint(const Entity &s, const Entity &t,
                             const std::vector<NavBlocker> &blockers, Vec2 &point) {
    const Card *card = FindEntityDefinition(s.cardId);
    const double reach = AttackReach(s, t);
    auto pending = engagementSlots_.find(s.id);
    if (pending != engagementSlots_.end() && pending->second.pendingAcquisition) {
        const Entity *future = Get(pending->second.target);
        if (pending->second.target == t.id) pending->second.pendingAcquisition = false;
        else if (card && card->structuresOnly && future && !future->dead &&
                 (t.kind == EntityKind::Guard || t.kind == EntityKind::Core) &&
                 D(s.position, future->position) <= AttackReach(s, *future) + 12.)
            return false;
        else engagementSlots_.erase(pending);
    }
    if (!card || card->projectileSpeed > 0 || s.kind != EntityKind::Troop ||
        D(s.position, t.position) > reach + 12.) {
        engagementSlots_.erase(s.id);
        return false;
    }
    if (s.position.z * t.position.z < 0 || D(s.position, t.position) > reach + 4.)
        return false;
    // Reservations follow a live target and vanish when their owner changes
    // targets, dies, or leaves the local approach. Stable IDs define ordering;
    // there is no random repulsion or crowd-wide position correction.
    // Authoritative entities append increasing IDs and retire through stable
    // erasure, so lookups need not scan the entire crowd for every reservation.
    auto entity = [&](EntityId id) -> const Entity * {
        const auto it = std::lower_bound(state_.entities.begin(), state_.entities.end(), id,
            [](const Entity &body, EntityId value) { return body.id < value; });
        return it != state_.entities.end() && it->id == id ? &*it : nullptr;
    };
    if (engagementCleanupAt_ != state_.elapsed) {
        for (auto it = engagementSlots_.begin(); it != engagementSlots_.end();) {
            const Entity *owner = entity(it->first), *target = entity(it->second.target);
            const Entity *current = owner ? entity(owner->target) : nullptr;
            const bool approaching = owner && it->second.pendingAcquisition && current &&
                (current->kind == EntityKind::Guard || current->kind == EntityKind::Core);
            if (!owner || owner->dead || !target || target->dead ||
                (owner->target != target->id && !approaching) ||
                D(owner->position, target->position) > AttackReach(*owner, *target) + 12.5)
                it = engagementSlots_.erase(it);
            else
                ++it;
        }
        engagementCleanupAt_ = state_.elapsed;
    }
    const double physical = SameCollisionLayer(s, t) ? BodyClearance(s, t.kind, t.radius) : 0.;
    if (reach <= physical + .0001)
        return false;
    const double radius = reach - std::min(.001, (reach - physical) * .25);
    struct ContactObstacle {
        EntityId owner;
        Vec2 position;
        double clearance;
        bool settled;
    };
    std::vector<ContactObstacle> physicalBodies, occupiedArrivals;
    auto refreshContacts = [&]() {
        physicalBodies.clear();
        occupiedArrivals.clear();
        for (const auto &other : state_.entities) {
            if (other.dead || other.id == s.id || other.id == t.id || !SameCollisionLayer(s, other))
                continue;
            const auto reservation = engagementSlots_.find(other.id);
            if (reservation != engagementSlots_.end() &&
                (reservation->second.target == other.target || reservation->second.pendingAcquisition))
                continue;
            const Entity *otherTarget = entity(other.target);
            const bool settled = other.kind != EntityKind::Troop || other.stunUntil > state_.elapsed ||
                (otherTarget && CombatDistance(other, *otherTarget) <= AttackReach(other, *otherTarget) + 1e-9);
            physicalBodies.push_back({other.id, other.position,
                BodyClearance(s, other.kind, other.radius) - 1e-9, settled});
        }
        for (const auto &[ownerId, slot] : engagementSlots_) {
            if (ownerId == s.id)
                continue;
            const Entity *owner = entity(ownerId), *target = entity(slot.target);
            if (!owner || owner->dead || !target || target->dead ||
                (owner->target != target->id && !slot.pendingAcquisition) || !SameCollisionLayer(s, *owner))
                continue;
            occupiedArrivals.push_back({ownerId,
                {target->position.x + slot.offset.x, target->position.z + slot.offset.z},
                BodyClearance(s, owner->kind, owner->radius) - 1e-9, true});
        }
    };
    // Candidate goals share immutable body classifications inside this call.
    // Rebuild after formation reservations change, never across movers/ticks.
    refreshContacts();
    auto free = [&](Vec2 candidate, bool keepReservation, EntityId ignoredOwner = 0) {
        if (!s.flying && !NavValid(candidate, s, 0, s.bridge, blockers))
            return false;
        const double edge = .40 + s.radius * .72;
        if (std::abs(candidate.x) > HalfX - edge || std::abs(candidate.z) > HalfZ - edge)
            return false;
        // Leave a real approach outside a contact point. Wall-facing points
        // can be geometrically free yet seal a waiting body into a tiny pocket
        // once both adjacent attackers arrive. Prefer the other free arcs.
        const Vec2 radial{candidate.x - t.position.x, candidate.z - t.position.z};
        const double length = std::hypot(radial.x, radial.z);
        const double approach = length + s.radius * .48;
        if (length > 1e-9 &&
            (std::abs(t.position.x + radial.x / length * approach) > HalfX - edge ||
             std::abs(t.position.z + radial.z / length * approach) > HalfZ - edge))
            return false;
        for (const auto &other : physicalBodies)
            if (other.owner != ignoredOwner && (!keepReservation || other.settled) &&
                D(candidate, other.position) < other.clearance)
                return false;
        for (const auto &other : occupiedArrivals)
            if (other.owner != ignoredOwner && D(candidate, other.position) < other.clearance)
                return false;
        return true;
    };
    auto existing = engagementSlots_.find(s.id);
    constexpr double Tau = 6.28318530717958647692;
    const double separation = BodyClearance(s, EntityKind::Troop, s.radius);
    const double angle = 2. * std::asin(std::min(1., separation / (2. * radius)));
    const int count = std::max(2, std::min(128, static_cast<int>(std::floor(Tau / angle))));
    // Plan a paid formation together before its front rank reaches contact.
    // Minimize its longest approach first: leading outer members can wrap the
    // target while the following ranks enter nearer arcs. Greedy nearest-goal
    // assignment lets the front rank seal a wall corridor before the rear rank
    // passes it. Assignment changes goals only; every body still walks/sweeps.
    if (existing == engagementSlots_.end() && card->count > 1 && s.playId) {
        bool planned = false;
        std::vector<const Entity *> formation;
        for (const auto &body : state_.entities) {
            const Entity *currentTarget = entity(body.target);
            const bool willApproach = card->structuresOnly && t.kind == EntityKind::Building &&
                body.lane == s.lane && currentTarget &&
                (currentTarget->kind == EntityKind::Guard || currentTarget->kind == EntityKind::Core);
            if (body.dead || body.playId != s.playId || body.cardId != s.cardId ||
                body.team != s.team || (body.target != t.id && !willApproach) ||
                D(body.position, t.position) > AttackReach(body, t) + 12.) continue;
            formation.push_back(&body);
            const auto assigned = engagementSlots_.find(body.id);
            planned = planned || (assigned != engagementSlots_.end() && assigned->second.target == t.id);
        }
        std::vector<Vec2> goals;
        if (!planned && formation.size() > 1)
            for (int n = 0; n < count; ++n) {
                const double bearing = Tau * n / count;
                Vec2 candidate{t.position.x + std::sin(bearing) * radius,
                               t.position.z + std::cos(bearing) * radius};
                if (free(candidate, false)) goals.push_back(candidate);
            }
        if (goals.size() >= formation.size() && formation.size() > 1) {
            const int rows = int(formation.size()), columns = int(goals.size());
            std::vector<std::vector<double>> cost(rows, std::vector<double>(columns));
            std::vector<double> limits;
            for (int i = 0; i < rows; ++i) {
                const double fromAngle = std::atan2(formation[i]->position.x - t.position.x,
                                                   formation[i]->position.z - t.position.z);
                const double radial = D(formation[i]->position, t.position);
                for (int j = 0; j < columns; ++j) {
                    const double toAngle = std::atan2(goals[j].x - t.position.x, goals[j].z - t.position.z);
                    const double turn = std::abs(std::remainder(toAngle - fromAngle, Tau));
                    cost[i][j] = std::max(0., radial - radius) +
                        std::max(0., turn - .35) * (radius + separation);
                    limits.push_back(cost[i][j]);
                }
            }
            std::sort(limits.begin(), limits.end());
            std::vector<std::vector<int>> preference(rows, std::vector<int>(columns));
            for (int i = 0; i < rows; ++i) {
                for (int j = 0; j < columns; ++j) preference[i][j] = j;
                std::stable_sort(preference[i].begin(), preference[i].end(),
                    [&](int a, int b) { return cost[i][a] < cost[i][b]; });
            }
            std::vector<int> chosen(rows, -1);
            auto match = [&](double limit) {
                std::vector<int> owners(columns, -1);
                for (int i = 0; i < rows; ++i) {
                    std::vector<bool> seen(columns, false);
                    auto augment = [&](auto &&self, int row) -> bool {
                        for (int goal : preference[row])
                            if (!seen[goal] && cost[row][goal] <= limit + 1e-9) {
                                seen[goal] = true;
                                if (owners[goal] < 0 || self(self, owners[goal])) {owners[goal] = row;return true;}
                            }
                        return false;
                    };
                    if (!augment(augment, i)) return false;
                }
                std::fill(chosen.begin(), chosen.end(), -1);
                for (int j = 0; j < columns; ++j) if (owners[j] >= 0) chosen[owners[j]] = j;
                return true;
            };
            int low = 0, high = int(limits.size()) - 1;
            while (low < high) {
                const int middle = (low + high) / 2;
                if (match(limits[middle])) high = middle;else low = middle + 1;
            }
            if (match(limits[low])) {
                // Among equal longest-approach solutions, keep total travel
                // minimal instead of letting augmenting-path order exchange
                // already short assignments for unnecessary opposite-side arcs.
                std::vector<double> u(rows + 1), v(columns + 1);
                std::vector<int> owner(columns + 1), previous(columns + 1);
                for (int row = 1; row <= rows; ++row) {
                    owner[0] = row;
                    int column = 0;
                    std::vector<double> minimum(columns + 1, 1e12);
                    std::vector<bool> used(columns + 1, false);
                    do {
                        used[column] = true;
                        const int currentRow = owner[column];
                        int nextColumn = 0;double delta = 1e12;
                        for (int j = 1; j <= columns; ++j) if (!used[j]) {
                            const double distance = cost[currentRow - 1][j - 1];
                            const double reduced = (distance <= limits[low] + 1e-9 ? distance : 1e6) - u[currentRow] - v[j];
                            if (reduced < minimum[j]) {minimum[j] = reduced;previous[j] = column;}
                            if (minimum[j] < delta) {delta = minimum[j];nextColumn = j;}
                        }
                        for (int j = 0; j <= columns; ++j)
                            if (used[j]) {u[owner[j]] += delta;v[j] -= delta;}
                            else minimum[j] -= delta;
                        column = nextColumn;
                    } while (owner[column]);
                    do {
                        const int last = previous[column];owner[column] = owner[last];column = last;
                    } while (column);
                }
                for (int j = 1; j <= columns; ++j) if (owner[j]) chosen[owner[j] - 1] = j - 1;
                for (int i = 0; i < rows; ++i) {
                    const Vec2 goal = goals[chosen[i]];
                    engagementSlots_[formation[i]->id] = {t.id,
                        {goal.x - t.position.x, goal.z - t.position.z}, formation[i]->position,
                        D(formation[i]->position, goal), state_.elapsed, formation[i]->target != t.id};
                }
                existing = engagementSlots_.find(s.id);
                refreshContacts();
            }
        }
    }
    bool stalled = false;
    if (existing != engagementSlots_.end() && existing->second.target == t.id) {
        const Vec2 saved{t.position.x + existing->second.offset.x, t.position.z + existing->second.offset.z};
        double nearest = D(s.position, saved) - .20;
        Vec2 vacancy = saved;
        if (nearest > .12)
            for (int n = 0; n < count; ++n) {
                const double bearing = Tau * n / count;
                const Vec2 candidate{t.position.x + std::sin(bearing) * radius,
                                     t.position.z + std::cos(bearing) * radius};
                const double distance = D(s.position, candidate);
                if (distance >= nearest || !free(candidate, true)) continue;
                const auto traffic = CollisionBodies(s, distance + .05);
                if (BodyMotionFraction(s, candidate, traffic, blockers, true) < 1. - 1e-6) continue;
                nearest = distance;vacancy = candidate;
            }
        if (D(vacancy, saved) > .01) {
            existing->second.offset = {vacancy.x - t.position.x, vacancy.z - t.position.z};
            existing->second.progressAt = state_.elapsed;
            existing->second.progressDistance = D(s.position, vacancy);
            existing->second.progressPosition = s.position;
            if (Entity *owner = Get(s.id)) {owner->path.clear();owner->repathClock = 0;}
        }
        const Vec2 destination{t.position.x + existing->second.offset.x,
                                t.position.z + existing->second.offset.z};
        const double remaining = D(s.position, destination);
        if (remaining + .025 < existing->second.progressDistance) {
            existing->second.progressDistance = remaining;
            existing->second.progressPosition = s.position;
            existing->second.progressAt = state_.elapsed;
        }
        stalled = state_.elapsed - existing->second.progressAt > .35;
    }
    if (existing != engagementSlots_.end() && existing->second.target == t.id &&
        stalled) {
        const Vec2 destination{t.position.x + existing->second.offset.x,
                                t.position.z + existing->second.offset.z};
        EntityId partner = 0;
        double closest = D(s.position, destination);
        for (const auto &[ownerId, slot] : engagementSlots_) {
            const Entity *owner = entity(ownerId);
            if (ownerId == s.id || slot.target != t.id || !owner || owner->dead ||
                owner->team != s.team || owner->cardId != s.cardId || owner->playId != s.playId ||
                owner->target != t.id)
                continue;
            const Vec2 occupied{t.position.x + slot.offset.x, t.position.z + slot.offset.z};
            const double distance = D(s.position, occupied);
            if (distance + .1 >= closest || D(owner->position, occupied) > .04 ||
                !free(occupied, true, ownerId) ||
                !NavSegmentValid(owner->position, destination, *owner, 0, owner->bridge, blockers))
                continue;
            const double length = D(owner->position, destination);
            const double firstStep = std::min(length, card->moveSpeed / 60.);
            const Vec2 next{owner->position.x + (destination.x - owner->position.x) / length * firstStep,
                             owner->position.z + (destination.z - owner->position.z) / length * firstStep};
            const auto traffic = CollisionBodies(*owner, firstStep + .05);
            // Other approaching bodies may still be vacating a later part of
            // the arc. Require a safe first normal step; each subsequent step
            // is checked against that tick's complete relative body sweeps.
            if (BodyMotionFraction(*owner, next, traffic, blockers, true) < 1. - 1e-6)
                continue;
            closest = distance;
            partner = ownerId;
        }
        if (partner) {
            // Slide an adjacent settled member into the empty reserved contact
            // so the waiting member can enter from its own clear side. Both
            // bodies walk normally; only their arrival goals exchange places.
            auto &other = engagementSlots_.at(partner);
            std::swap(other.offset, existing->second.offset);
            other.yieldingToFriend = existing->second.yieldingToFriend = true;
            for (EntityId ownerId : {s.id, partner})
                if (Entity *owner = Get(ownerId)) {
                    owner->path.clear();
                    owner->repathClock = 0;
                    owner->stuckTime = 0;
                    auto &goal = engagementSlots_.at(ownerId);
                    goal.progressAt = state_.elapsed;
                    goal.progressPosition = owner->position;
                    goal.progressDistance = D(owner->position, {t.position.x + goal.offset.x,
                                                                t.position.z + goal.offset.z});
                }
            point = {t.position.x + existing->second.offset.x,
                      t.position.z + existing->second.offset.z};
            return true;
        }
    }
    Vec2 previous;
    bool alternate = false;
    if (existing != engagementSlots_.end() && existing->second.target == t.id) {
        const Vec2 saved{t.position.x + existing->second.offset.x,
                         t.position.z + existing->second.offset.z};
        previous = saved;
        alternate = stalled && D(s.position, saved) > .12;
        if (!alternate && free(saved, true)) {
            point = saved;
            return true;
        }
    }
    engagementSlots_.erase(s.id);
    double best = std::numeric_limits<double>::infinity();
    bool found = false;
    for (int n = 0; n < count; ++n) {
        const double bearing = Tau * n / count;
        const Vec2 candidate{t.position.x + std::sin(bearing) * radius,
                             t.position.z + std::cos(bearing) * radius};
        const double distance = D(s.position, candidate) +
            (alternate && D(candidate, previous) < .01 ? 8. : 0.);
        if (distance + 1e-9 < best && free(candidate, false)) {
            best = distance;
            point = candidate;
            found = true;
        }
    }
    if (found)
        engagementSlots_[s.id] = {t.id, {point.x - t.position.x, point.z - t.position.z}, s.position,
                                  D(s.position, point), state_.elapsed};
    return found;
}
void Match::Move(Entity &s, const Entity &t, double dt) {
    const Card *c = FindEntityDefinition(s.cardId);
    if (!c || c->moveSpeed <= 0)
        return;
    const double slow = state_.elapsed < s.slowUntil ? s.slowPct : 0;
    const double speed = s.charged && c->chargedMoveSpeed > 0 ? c->chargedMoveSpeed : c->moveSpeed;
    const double step = speed * (1 - slow) * dt;
    const Vec2 before = s.position;
    // Static geometry cannot change inside one Move. Keep encounter order and
    // per-query goal exclusions; the next mover collects any intervening deaths.
    auto blockers = s.flying ? std::vector<NavBlocker>{} : CollectNavBlockers();
    Vec2 arrival = t.position;
    const bool engagement = EngagementPoint(s, t, blockers, arrival);
    if (engagement && !s.flying) {
        for (const auto &other : state_.entities) {
            if (other.dead || other.id == s.id || other.kind != EntityKind::Troop ||
                !SameCollisionLayer(s, other))
                continue;
            const auto targetIt = std::lower_bound(state_.entities.begin(), state_.entities.end(), other.target,
                [](const Entity &body, EntityId value) { return body.id < value; });
            const Entity *otherTarget = targetIt != state_.entities.end() && targetIt->id == other.target
                                            ? &*targetIt : nullptr;
            const auto reservation = engagementSlots_.find(other.id);
            const bool hasArrival = reservation != engagementSlots_.end() && reservation->second.target == other.target;
            const bool atArrival = hasArrival && otherTarget &&
                D(other.position, {otherTarget->position.x + reservation->second.offset.x,
                                   otherTarget->position.z + reservation->second.offset.z}) < .04;
            if (other.id == t.id || other.stunUntil > state_.elapsed ||
                (otherTarget && !otherTarget->dead &&
                 (hasArrival ? atArrival :
                  CombatDistance(other, *otherTarget) <= AttackReach(other, *otherTarget) + 1e-9)))
                blockers.push_back({other.id, other.position, other.radius, other.kind});
        }
    }
    Entity routeTarget;
    routeTarget.position = arrival;
    // An approach to a free contact slot must route around the target's real
    // body. Excluding that footprint would draw every far-side route through it.
    const EntityId routeGoal = engagement ? 0 : t.id;
    routeTarget.id = routeGoal;
    Vec2 waypoint = arrival;
    if (!s.flying) {
        if (!NavValid(s.position, s, routeGoal, s.bridge, blockers)) {
            // Deliberately invalid DEV river fixtures remain in place. Never
            // repair a body by teleporting it into another troop's footprint.
            s.path.clear();
            s.repathClock = 0;
            return;
        }
        const bool crossing = s.position.z * t.position.z < 0;
        const bool targetChanged = D(s.pathTarget, arrival) > (engagement ? .05 : 1.05);
        if (s.bridge && std::abs(s.position.z) > arena::RiverHalfWidth + .92 + .12 && !crossing)
            s.bridge = 0;
        if (crossing && !s.bridge) {
            // Hold the current-side bridge through a crossing, including when
            // a defender or the surviving opposite crown is across the arena.
            s.bridge = s.position.x < 0 ? -1 : s.position.x > 0 ? 1 : s.lane < 0 ? -1 : 1;
            s.lane = s.bridge;
            s.path.clear();
        }
        s.repathClock -= dt;
        bool invalid = !s.path.empty() &&
            !NavSegmentValid(s.position, s.path.front(), s, routeGoal, s.bridge, blockers);
        for (std::size_t i = 0; i < std::min<std::size_t>(4, s.path.size()); ++i)
            if (!NavValid(s.path[i], s, routeGoal, s.bridge, blockers)) {
                invalid = true;
                break;
            }
        if (s.path.empty() || targetChanged || invalid) {
            if (engagement && NavSegmentValid(s.position, arrival, s, routeGoal, s.bridge, blockers))
                s.path = {arrival};
            else {
                Entity connectorTarget = routeTarget;
                if (engagement) {
                    // Join an occupied contact ring from outside. A final goal
                    // squeezed between two bodies may have no visible integer
                    // tile even though a continuous radial approach is clear.
                    const Vec2 radial{arrival.x - t.position.x, arrival.z - t.position.z};
                    const double inner = std::hypot(radial.x, radial.z);
                    const Vec2 direction{radial.x / inner, radial.z / inner};
                    const double edge = .40 + s.radius * .72 + .002;
                    double outer = inner + s.radius * 2. + .25;
                    if (std::abs(direction.x) > 1e-9)
                        outer = std::min(outer,
                            ((direction.x > 0 ? HalfX - edge : -HalfX + edge) - t.position.x) / direction.x);
                    if (std::abs(direction.z) > 1e-9)
                        outer = std::min(outer,
                            ((direction.z > 0 ? HalfZ - edge : -HalfZ + edge) - t.position.z) / direction.z);
                    const Vec2 staging{t.position.x + direction.x * outer,
                                       t.position.z + direction.z * outer};
                    if (outer > inner + .02 && NavValid(staging, s, 0, s.bridge, blockers))
                        connectorTarget.position = staging;
                }
                s.path = FindPath(s, connectorTarget, s.bridge, blockers);
                if (engagement && s.path.empty() &&
                    NavSegmentValid(s.position, connectorTarget.position, s, 0, s.bridge, blockers))
                    s.path = {connectorTarget.position};
                if (engagement && !s.path.empty() && D(s.path.back(), arrival) > .0001 &&
                    NavSegmentValid(s.path.back(), arrival, s, 0, s.bridge, blockers))
                    s.path.push_back(arrival);
            }
            s.pathTarget = arrival;
            s.repathClock = .68;
            if (s.path.empty() && crossing) {
                const double direction = s.position.z > 0 ? -1 : 1;
                const double half = std::max(.28, arena::BridgeWidth / 2 - s.radius - .30);
                const double slot = Clamp((static_cast<int>(s.id % 5) - 2) * .22, -half * .58, half * .58);
                const double x = s.bridge * arena::BridgeCenterX + slot;
                s.path = {{x, -direction * (arena::RiverHalfWidth + .78)}, {x, -direction * (arena::RiverHalfWidth + .18)}, {x, 0},
                          {x, direction * (arena::RiverHalfWidth + .18)}, {x, direction * (arena::RiverHalfWidth + .92)}, arrival};
            }
        }
        // Follow the furthest visible static waypoint. Requiring every member
        // of a crowd to visit the same exact grid corner creates an artificial
        // bottleneck even when the bridge has room for several physical lanes.
        while (s.path.size() > 1 && NavSegmentValid(s.position, s.path[1], s, routeGoal, s.bridge, blockers))
            s.path.erase(s.path.begin());
        if (!s.path.empty() && D(s.position, s.path.front()) < (engagement ? .0005 : .34))
            s.path.erase(s.path.begin());
        if (s.path.empty())
            return;
        waypoint = s.path.front();
        if (s.bridge && (crossing || std::abs(s.position.z) < arena::RiverHalfWidth + .65)) {
            // A visible continuous approach can use a narrow building gap
            // that contains no integer grid node. Preserve independent lateral
            // lanes along that approach and across the complete bridge.
            const double legalHalf = std::max(.34, arena::BridgeWidth / 2 - .16 - s.radius * .92);
            const double half = legalHalf - .06;
            const double center = s.bridge * arena::BridgeCenterX;
            const double x = Clamp(s.position.x, center - half, center + half);
            const double direction = t.position.z < 0 ? -1. : 1.;
            Vec2 corridor{x, direction * (arena::RiverHalfWidth + .9)};
            if (std::abs(s.position.x - center) > legalHalf + .001) {
                const double ownBank = s.position.z < 0 ? -1. : 1.;
                // Join the bridge at a safe depth, not along the bank's hard
                // boundary where a crowded sideways slide has no passing room.
                corridor = {center, ownBank * (arena::RiverHalfWidth + .28 + s.radius + .35)};
            }
            if (NavSegmentValid(s.position, corridor, s, routeGoal, s.bridge, blockers))
                waypoint = corridor;
        }
    }
    Vec2 preferred{waypoint.x - s.position.x, waypoint.z - s.position.z};
    const double distance = std::hypot(preferred.x, preferred.z);
    if (distance <= 1e-9)
        return;
    preferred = {preferred.x / distance, preferred.z / distance};
    const double amount = std::min(step, distance);
    const double forecast = std::max(.35, speed * (1 - slow) * .48);
    // A final contact forecast ends at the arrival goal. Looking past a clear
    // destination into the target's body otherwise rewards sideways orbiting
    // instead of the last safe few centimetres of the approach.
    const double lookAhead = engagement ? std::min(forecast, distance) : forecast;
    const auto bodies = CollisionBodies(s, lookAhead + amount + .05);
    // Every steering candidate stays inside this local motion square. Distant
    // route blockers cannot intersect its present or forecast sweep; preserve
    // blocker order and the original narrow-phase tests for the remaining ones.
    std::vector<NavBlocker> motionBlockers;
    const double motionRadius = std::max(lookAhead, amount);
    for (const auto &blocker : blockers) {
        const double extent = motionRadius + BodyClearance(s, blocker.kind, blocker.radius) + 1e-6;
        if (std::abs(blocker.position.x - s.position.x) <= extent &&
            std::abs(blocker.position.z - s.position.z) <= extent)
            motionBlockers.push_back(blocker);
    }
    const bool opposingTraffic = std::any_of(bodies.begin(), bodies.end(), [&](const CollisionBody &body) {
        return body.kind == EntityKind::Troop && body.team != s.team;
    });
    Vec2 best = s.position;
    double bestScore = -1e9;
    Vec2 yield = s.position;
    double yieldScore = -1e9;
    // Fixed candidate order creates stable right-hand passing for opposing
    // traffic. A blocked troop can slide sideways or wait, never step through
    // another troop, an attacking/stunned body, or a solid tower footprint.
    constexpr std::array<double, 18> angles{0, .209439510239, -.209439510239,
        .436332312999, -.436332312999, .698131700798, -.698131700798,
        1.047197551197, -1.047197551197, 1.396263401595, -1.396263401595,
        1.570796326795, -1.570796326795, 1.745329251994, -1.745329251994,
        2.094395102393, -2.094395102393, 3.141592653590};
    auto consider = [&](Vec2 direction) {
        const double cosine = preferred.x * direction.x + preferred.z * direction.z;
        const double sine = preferred.x * direction.z - preferred.z * direction.x;
        const Vec2 requested{s.position.x + direction.x * amount,
                             s.position.z + direction.z * amount};
        const double fraction = BodyMotionFraction(s, requested, bodies, motionBlockers);
        const Vec2 point{s.position.x + direction.x * amount * fraction,
                         s.position.z + direction.z * amount * fraction};
        const Vec2 probe{s.position.x + direction.x * lookAhead,
                         s.position.z + direction.z * lookAhead};
        const double clearance = fraction == 0. ? 0. : BodyMotionFraction(s, probe, bodies, motionBlockers, true);
        const double inertia = s.facing.x * direction.x + s.facing.z * direction.z;
        // A forecast cannot reward a direction that cannot move this tick.
        // Brief backward yielding may unpack a dense formation, but any clear
        // forward/sideways route scores higher and all displacements are swept.
        // A small consistent passing preference also applies when nearly equal
        // forecasts differ by roundoff. Candidate order alone can oscillate at
        // contact after a bridge-axis change. Zero motion receives no bonus.
        const double score = fraction * (cosine + .26 + .85 * clearance + .025 * inertia +
                                         (opposingTraffic ? .06 * sine : 0.)) -
                             .09 * std::abs(sine);
        if (score > bestScore + 1e-10) {
            best = point;
            bestScore = score;
        }
        if (fraction > .20) {
            const double escapeScore = fraction * (1. + .4 * clearance + .08 * inertia) +
                                       .05 * cosine - .01 * std::abs(sine);
            if (escapeScore > yieldScore + 1e-10) {
                yield = point;
                yieldScore = escapeScore;
            }
        }
    };
    for (double angle : angles) {
        const double cosine = std::cos(angle), sine = std::sin(angle);
        consider({preferred.x * cosine - preferred.z * sine,
                  preferred.x * sine + preferred.z * cosine});
    }
    // Exact tangents prevent a nearly touching body from trapping a neighbour
    // between two coarse angle candidates. A small outward component begins a
    // continuous slide while strict relative sweeps still police every pair.
    for (const auto &body : bodies) {
        Vec2 towards{body.to.x - s.position.x, body.to.z - s.position.z};
        const double length = std::hypot(towards.x, towards.z);
        if (length < 1e-9 || towards.x * preferred.x + towards.z * preferred.z <= 0)
            continue;
        towards = {towards.x / length, towards.z / length};
        for (double side : {1., -1.}) {
            Vec2 tangent{-towards.z * side - towards.x * .12,
                          towards.x * side - towards.z * .12};
            const double magnitude = std::hypot(tangent.x, tangent.z);
            consider({tangent.x / magnitude, tangent.z / magnitude});
        }
    }
    // Waiting is preferable to an unsafe step, but must not permanently beat
    // every safe retreat merely because it has a negative forward score. Only
    // a nearly stationary result uses this bounded, continuously swept yield;
    // ordinary forward steering and its candidate ordering remain unchanged.
    if (D(s.position, best) < amount * .10 && D(s.position, yield) > amount * .20)
        best = yield;
    s.position = best;
    const double travelled = D(before, s.position);
    if (travelled > 1e-7)
        s.facing = {(s.position.x - before.x) / travelled, (s.position.z - before.z) / travelled};
    s.stuckClock += dt;
    if (s.stuckClock >= .28) {
        const double remaining = D(s.position, waypoint);
        const bool routeChanged = D(waypoint, s.stuckPosition) > .055;
        const bool progress = routeChanged || s.stuckDistance - remaining > .055;
        s.stuckTime = progress ? std::max(0., s.stuckTime - s.stuckClock * 1.6)
                              : s.stuckTime + s.stuckClock;
        s.stuckDistance = remaining;
        s.stuckPosition = waypoint;
        s.stuckClock = 0;
        if (s.stuckTime > .82) {
            s.path.clear();
            s.repathClock = 0;
            s.stuckTime = 0;
        }
    }
    if (c->chargeDamage > 0 && travelled > .002) {
        s.chargeTime += dt;
        if (!s.charged && s.chargeTime > c->chargeMoveSeconds + 1e-9) {
            s.charged = true;
            auto &event = Emit("charge_ready", s.team);
            event.source = s.id;
            event.cardId = s.cardId;
            event.attributionCardId = s.attributionCardId;
            event.parentEntityId = s.parentEntityId;
            event.playId = s.playId;
            event.amount = c->chargedMoveSpeed > 0 ? c->chargedMoveSpeed : c->moveSpeed;
            event.requested = s.chargeTime;
            event.position = s.position;
        }
    }
}
} // namespace rift
