#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>

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
    constexpr double Bank = 1.65 + .28;
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
    const double half = std::max(.34, 2.1 - .16 - radius * .92);
    const double x1 = from.x + (to.x - from.x) * enter,
                 x2 = from.x + (to.x - from.x) * leave;
    for (int lane : {-1, 1})
        if ((!bridge || bridge == lane) && std::abs(x1 - lane * 7.2) <= half &&
            std::abs(x2 - lane * 7.2) <= half)
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
Vec2 Point(Cell c) {
    return {static_cast<double>(c.x), static_cast<double>(c.z)};
}
Cell Grid(Vec2 p) {
    return {static_cast<int>(std::floor(p.x + .5)), static_cast<int>(std::floor(p.z + .5))};
}
double Heuristic(Cell a, Cell b) {
    double x = std::abs(a.x - b.x), z = std::abs(a.z - b.z);
    return x + z + (1.4142135623730951 - 2) * std::min(x, z);
}
} // namespace
bool Match::NavValid(Vec2 p, const Entity &s, EntityId goal, int bridge) const {
    const double edge = .40 + s.radius * .72;
    if (std::abs(p.x) > HalfX - edge || std::abs(p.z) > HalfZ - edge)
        return false;
    if (std::abs(p.z) < 1.65 + .28) {
        const double half = std::max(.34, 2.1 - .16 - s.radius * .92);
        bool legal = false;
        for (int lane : {-1, 1})
            if ((!bridge || bridge == lane) && std::abs(p.x - lane * 7.2) <= half)
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
bool Match::BodyPlacementValid(Vec2 point, const Entity &s, bool sandbox,
                               const std::vector<Entity> &planned) const {
    const Card *card = FindCard(s.cardId);
    if (!sandbox && (!card || !CanPlace(s.team, *card, point)))
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
        bodies.push_back({from, other.position, other.radius, other.kind});
    }
    return bodies;
}
double Match::BodyMotionFraction(const Entity &s, Vec2 to,
                                 const std::vector<CollisionBody> &bodies, bool forecast) const {
    const Vec2 from = s.position;
    auto valid = [&](double fraction) {
        const Vec2 point{from.x + (to.x - from.x) * fraction,
                         from.z + (to.z - from.z) * fraction};
        if (s.flying) {
            const double edge = .40 + s.radius * .72;
            if (std::abs(point.x) > HalfX - edge || std::abs(point.z) > HalfZ - edge)
                return false;
        } else if (!NavSegmentValid(from, point, s, 0, s.bridge))
            return false;
        for (const auto &body : bodies) {
            const Vec2 start = forecast ? body.to : body.from;
            const Vec2 relativeFrom{from.x - start.x, from.z - start.z};
            const Vec2 relativeTo{point.x - body.to.x, point.z - body.to.z};
            const double clearance = BodyClearance(s, body.kind, body.radius);
            const double clearanceSquared = clearance * clearance;
            const double startSquared = relativeFrom.x * relativeFrom.x + relativeFrom.z * relativeFrom.z;
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
    // The arena is bounded. Flat arrays avoid allocating tree nodes for every
    // visited tile in thousands of background matches; A* ordering is unchanged.
    constexpr int Width = HalfX * 2 + 1, Height = HalfZ * 2 + 1, Size = Width * Height;
    auto index = [](Cell c) { return (c.z + HalfZ) * Width + c.x + HalfX; };
    auto cell = [](int n) { return Cell{n % Width - HalfX, n / Width - HalfZ}; };
    struct Obstacle {
        Vec2 position;
        double clearanceSquared;
    };
    std::vector<Obstacle> obstacles;
    for (const auto &e : state_.entities)
        if (!e.dead && e.kind != EntityKind::Troop && e.id != t.id && e.id != s.id) {
            const double radius = e.radius + s.radius + .22;
            obstacles.push_back({e.position, radius * radius});
        }
    const double edge = .40 + s.radius * .72, half = std::max(.34, 2.1 - .16 - s.radius * .92);
    std::array<std::uint8_t, Size> walkable{}, closed{};
    for (int z = -HalfZ; z <= HalfZ; ++z)
        for (int x = -HalfX; x <= HalfX; ++x) {
            Cell c{x, z};
            bool valid = std::abs(x) <= HalfX - edge && std::abs(z) <= HalfZ - edge;
            if (valid && std::abs(z) < 1.65 + .28) {
                valid = false;
                for (int lane : {-1, 1})
                    if ((!bridge || bridge == lane) && std::abs(x - lane * 7.2) <= half)
                        valid = true;
            }
            if (valid)
                for (const auto &o : obstacles) {
                    const double dx = x - o.position.x, dz = z - o.position.z;
                    if (dx * dx + dz * dz < o.clearanceSquared) {
                        valid = false;
                        break;
                    }
                }
            walkable[index(c)] = valid ? 1 : 0;
        }
    auto valid = [&](Cell c) {
        return c.x >= -HalfX && c.x <= HalfX && c.z >= -HalfZ && c.z <= HalfZ && walkable[index(c)] != 0;
    };
    auto segmentValid = [&](Vec2 from, Vec2 to) {
        if (!ArenaSegmentValid(from, to, s.radius, bridge))
            return false;
        for (const auto &o : obstacles)
            if (SegmentDistanceSquared(from, to, o.position) < o.clearanceSquared)
                return false;
        return true;
    };
    // Moving troops add bounded local route cost, never solid grid cells.
    // Rasterizing just their nearby cells keeps A* independent of crowd size
    // at each visited node and lets a queue continue through its own bridge.
    std::array<double, Size> crowdCost{};
    for (const auto &other : state_.entities) {
        if (other.dead || other.flying || other.kind != EntityKind::Troop ||
            other.id == s.id || other.id == t.id)
            continue;
        const Cell center = Grid(other.position);
        const double reach = s.radius + other.radius + .9;
        const int ring = static_cast<int>(std::ceil(reach));
        for (int z = std::max(-HalfZ, center.z - ring); z <= std::min(HalfZ, center.z + ring); ++z)
            for (int x = std::max(-HalfX, center.x - ring); x <= std::min(HalfX, center.x + ring); ++x) {
                const double distance = D(Point({x, z}), other.position);
                if (distance < reach) {
                    double &value = crowdCost[index({x, z})];
                    value = std::min(.85, value + .42 * (1. - distance / reach));
                }
            }
    }
    Cell start = Grid(s.position);
    Cell goal = Grid(t.position);
    if (start.x < -HalfX || start.x > HalfX || start.z < -HalfZ || start.z > HalfZ)
        return {};
    if (!valid(goal)) {
        const Cell raw = goal;
        bool found = false;
        for (int ring = 1; ring <= 5 && !found; ++ring) {
            double best = 1e9;
            for (int x = -ring; x <= ring; ++x)
                for (int z = -ring; z <= ring; ++z)
                    if (std::abs(x) == ring || std::abs(z) == ring) {
                        Cell c{raw.x + x, raw.z + z};
                        if (valid(c)) {
                            double distance = D(Point(c), t.position);
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
    std::array<double, Size> cost;
    cost.fill(std::numeric_limits<double>::infinity());
    std::array<int, Size> parents;
    parents.fill(-1);
    std::uint64_t sequence = 0;
    // A half-tile source may sit inside a pocket whose nearest grid node
    // sends it into its neighbours. Start from every visible local connector
    // and include that connector's real distance and bounded crowd cost. This
    // allows a short retreat around a friendly building without making moving
    // troops permanent walls or privileging one snapped starting tile.
    constexpr int ConnectorReach = 4;
    const Cell origin = Grid(s.position);
    std::vector<Obstacle> localCrowd;
    for (const auto &other : state_.entities) {
        if (other.dead || other.flying || other.kind != EntityKind::Troop ||
            other.id == s.id || other.id == t.id)
            continue;
        const double radius = s.radius + other.radius + .04;
        if (D(s.position, other.position) <= ConnectorReach + radius)
            localCrowd.push_back({other.position, radius * radius});
    }
    for (int z = std::max(-HalfZ, origin.z - ConnectorReach);
         z <= std::min(HalfZ, origin.z + ConnectorReach); ++z)
        for (int x = std::max(-HalfX, origin.x - ConnectorReach);
             x <= std::min(HalfX, origin.x + ConnectorReach); ++x) {
            const Cell candidate{x, z};
            const Vec2 point = Point(candidate);
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
            open.push({candidate, g + Heuristic(candidate, goal), g, sequence++});
        }
    if (open.empty()) {
        // Only an isolated pocket needs the full-arena nearest-visible search.
        // Never connect a route through a blocked start or across a tower corner.
        double best = std::numeric_limits<double>::infinity();
        bool found = false;
        for (int z = -HalfZ; z <= HalfZ; ++z)
            for (int x = -HalfX; x <= HalfX; ++x) {
                const Cell candidate{x, z};
                const double distance = D(s.position, Point(candidate));
                if (distance < best && valid(candidate) && segmentValid(s.position, Point(candidate))) {
                    best = distance;
                    start = candidate;
                    found = true;
                }
            }
        if (!found)
            return {};
        cost[index(start)] = best;
        open.push({start, best + Heuristic(start, goal), best, sequence++});
    }
    int examined = 0;
    while (!open.empty() && examined < Size) {
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
                path.push_back(Point(cells[i]));
            }
            if ((path.empty() && D(s.position, Point(root)) > .01) ||
                (!path.empty() && !segmentValid(s.position, path.front())))
                path.insert(path.begin(), Point(root));
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
                if (!segmentValid(Point(e.c), Point(n)))
                    continue;
                const double traffic = crowdCost[index(n)];
                double g = e.g + (dx && dz ? 1.4142135623730951 : 1) + traffic;
                if (g < cost[index(n)]) {
                    cost[index(n)] = g;
                    parents[index(n)] = current;
                    open.push({n, g + Heuristic(n, goal), g, sequence++});
                }
            }
    }
    return {};
}
void Match::Move(Entity &s, const Entity &t, double dt) {
    const Card *c = FindCard(s.cardId);
    if (!c || c->moveSpeed <= 0)
        return;
    const double slow = state_.elapsed < s.slowUntil ? s.slowPct : 0;
    const double step = c->moveSpeed * (1 - slow) * dt;
    const Vec2 before = s.position;
    Vec2 waypoint = t.position;
    if (!s.flying) {
        if (!NavValid(s.position, s, t.id, s.bridge)) {
            // Deliberately invalid DEV river fixtures remain in place. Never
            // repair a body by teleporting it into another troop's footprint.
            s.path.clear();
            s.repathClock = 0;
            return;
        }
        const bool crossing = s.position.z * t.position.z < 0;
        const bool targetChanged = D(s.pathTarget, t.position) > 1.05;
        if (s.bridge && std::abs(s.position.z) > 1.65 + .92 + .12 && !crossing)
            s.bridge = 0;
        if (crossing && !s.bridge) {
            // Hold the current-side bridge through a crossing, including when
            // a defender or the surviving opposite crown is across the arena.
            s.bridge = s.position.x < 0 ? -1 : s.position.x > 0 ? 1 : s.lane < 0 ? -1 : 1;
            s.lane = s.bridge;
            s.path.clear();
        }
        s.repathClock -= dt;
        bool invalid = false;
        for (std::size_t i = 0; i < std::min<std::size_t>(4, s.path.size()); ++i)
            if (!NavValid(s.path[i], s, t.id, s.bridge)) {
                invalid = true;
                break;
            }
        if (s.path.empty() || targetChanged || s.repathClock <= 0 || invalid) {
            s.path = FindPath(s, t, s.bridge);
            s.pathTarget = t.position;
            s.repathClock = .68;
            if (s.path.empty() && crossing) {
                const double direction = s.position.z > 0 ? -1 : 1;
                const double half = std::max(.28, 2.1 - s.radius - .30);
                const double slot = Clamp((static_cast<int>(s.id % 5) - 2) * .22, -half * .58, half * .58);
                const double x = s.bridge * 7.2 + slot;
                s.path = {{x, -direction * (1.65 + .78)}, {x, -direction * (1.65 + .18)}, {x, 0},
                          {x, direction * (1.65 + .18)}, {x, direction * (1.65 + .92)}, t.position};
            }
        }
        // Follow the furthest visible static waypoint. Requiring every member
        // of a crowd to visit the same exact grid corner creates an artificial
        // bottleneck even when the bridge has room for several physical lanes.
        while (s.path.size() > 1 && NavSegmentValid(s.position, s.path[1], s, t.id, s.bridge))
            s.path.erase(s.path.begin());
        if (!s.path.empty() && D(s.position, s.path.front()) < .34)
            s.path.erase(s.path.begin());
        if (s.path.empty())
            return;
        waypoint = s.path.front();
        if (s.bridge && (crossing || std::abs(s.position.z) < arena::RiverHalfWidth + .65)) {
            // A visible continuous approach can use a narrow building gap
            // that contains no integer grid node. Preserve independent lateral
            // lanes along that approach and across the complete bridge.
            const double legalHalf = std::max(.34, 2.1 - .16 - s.radius * .92);
            const double half = legalHalf - .06;
            const double center = s.bridge * arena::BridgeCenterX;
            const double x = Clamp(s.position.x, center - half, center + half);
            const double direction = t.position.z < 0 ? -1. : 1.;
            Vec2 corridor{x, direction * (arena::RiverHalfWidth + .9)};
            if (std::abs(s.position.x - center) > legalHalf + .001) {
                const double ownBank = s.position.z < 0 ? -1. : 1.;
                corridor.z = ownBank * (arena::RiverHalfWidth + .63);
                if (D(s.position, corridor) < .1)
                    corridor.z = direction * (arena::RiverHalfWidth + .9);
            }
            if (NavSegmentValid(s.position, corridor, s, t.id, s.bridge))
                waypoint = corridor;
        }
    }
    Vec2 preferred{waypoint.x - s.position.x, waypoint.z - s.position.z};
    const double distance = std::hypot(preferred.x, preferred.z);
    if (distance <= 1e-9)
        return;
    preferred = {preferred.x / distance, preferred.z / distance};
    const double amount = std::min(step, distance);
    const double lookAhead = std::max(.35, c->moveSpeed * (1 - slow) * .48);
    const auto bodies = CollisionBodies(s, lookAhead + amount + .05);
    Vec2 best = s.position;
    double bestScore = -1e9;
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
        const double fraction = BodyMotionFraction(s, requested, bodies);
        const Vec2 point{s.position.x + direction.x * amount * fraction,
                         s.position.z + direction.z * amount * fraction};
        const Vec2 probe{s.position.x + direction.x * lookAhead,
                         s.position.z + direction.z * lookAhead};
        const double clearance = BodyMotionFraction(s, probe, bodies, true);
        const double inertia = s.facing.x * direction.x + s.facing.z * direction.z;
        // A forecast cannot reward a direction that cannot move this tick.
        // Brief backward yielding may unpack a dense formation, but any clear
        // forward/sideways route scores higher and all displacements are swept.
        const double score = fraction * (cosine + .26 + .85 * clearance + .025 * inertia) -
                             .09 * std::abs(sine);
        if (score > bestScore + 1e-10) {
            best = point;
            bestScore = score;
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
    s.position = best;
    const double travelled = D(before, s.position);
    if (travelled > 1e-7)
        s.facing = {(s.position.x - before.x) / travelled, (s.position.z - before.z) / travelled};
    s.stuckClock += dt;
    if (s.stuckClock >= .28) {
        const double remaining = D(s.position, t.position);
        const bool progress = s.stuckDistance - remaining > .055 || D(s.position, s.stuckPosition) > .055;
        s.stuckTime = progress ? std::max(0., s.stuckTime - s.stuckClock * 1.6)
                              : s.stuckTime + s.stuckClock;
        s.stuckDistance = remaining;
        s.stuckPosition = s.position;
        s.stuckClock = 0;
        if (s.stuckTime > .82) {
            s.path.clear();
            s.repathClock = 0;
            s.stuckTime = 0;
        }
    }
    if (c->chargeDamage > 0 && travelled > .002) {
        s.chargeTime += dt;
        if (s.chargeTime > 1.65)
            s.charged = true;
    }
}
} // namespace rift
