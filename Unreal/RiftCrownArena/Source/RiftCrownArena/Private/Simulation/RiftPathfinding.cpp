#include "Simulation/RiftSimulation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>

namespace rift {
namespace {
double D(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
double Clamp(double v, double a, double b) {
    return std::max(a, std::min(b, v));
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
    if (std::abs(p.x) > 14 - edge || std::abs(p.z) > 21 - edge)
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
std::vector<Vec2> Match::FindPath(const Entity &s, const Entity &t, int bridge) const {
    if (s.flying)
        return {t.position};
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
    constexpr int Width = 29, Height = 43, Size = Width * Height;
    auto index = [](Cell c) { return (c.z + 21) * Width + c.x + 14; };
    auto cell = [](int n) { return Cell{n % Width - 14, n / Width - 21}; };
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
    for (int z = -21; z <= 21; ++z)
        for (int x = -14; x <= 14; ++x) {
            Cell c{x, z};
            bool valid = std::abs(x) <= 14 - edge && std::abs(z) <= 21 - edge;
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
        return c.x >= -14 && c.x <= 14 && c.z >= -21 && c.z <= 21 && walkable[index(c)] != 0;
    };
    std::array<double, 2> congestion{};
    for (const auto &other : state_.entities)
        if (!other.dead && !other.flying && other.kind == EntityKind::Troop && other.team == s.team &&
            other.id != s.id && std::abs(other.position.z) <= 3.2)
            for (int lane : {-1, 1})
                if (std::abs(other.position.x - lane * 7.2) < 2.1)
                    congestion[lane < 0 ? 0 : 1] += std::min(.34, .10 + std::max(.7, other.radius) * .07);
    for (auto &cost : congestion)
        cost = std::min(1.35, cost);
    const Cell start = Grid(s.position);
    Cell goal = Grid(t.position);
    if (start.x < -14 || start.x > 14 || start.z < -21 || start.z > 21)
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
    cost[index(start)] = 0;
    open.push({start, Heuristic(start, goal), 0, sequence++});
    int examined = 0;
    while (!open.empty() && examined < 1400) {
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
            while (n != index(start)) {
                n = parents[n];
                if (n < 0)
                    return {};
                cells.push_back(cell(n));
            }
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
            if (NavValid(t.position, s, t.id, bridge) && (path.empty() || D(path.back(), t.position) > .01))
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
                const double traffic = std::abs(n.z) <= 3 ? congestion[n.x < 0 ? 0 : 1] : 0;
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
    if (s.flying) {
        Vec2 d{t.position.x - s.position.x, t.position.z - s.position.z};
        double distance = std::hypot(d.x, d.z);
        if (distance > 1e-9) {
            const double amount = std::min(step, distance);
            s.position.x += d.x / distance * amount;
            s.position.z += d.z / distance * amount;
            s.facing = {d.x / distance, d.z / distance};
        }
    } else {
        const bool crossing = s.position.z * t.position.z < 0;
        const bool targetChanged = D(s.pathTarget, t.position) > 1.05;
        if (s.bridge && std::abs(s.position.z) > 1.65 + .92 + .12 && !crossing)
            s.bridge = 0;
        if (crossing && !s.bridge) {
            if (t.kind == EntityKind::Guard)
                s.bridge = t.lane;
            else {
                double left = D(s.position, {-7.2, 0}) + D(t.position, {-7.2, 0}),
                       right = D(s.position, {7.2, 0}) + D(t.position, {7.2, 0});
                for (const auto &o : state_.entities)
                    if (!o.dead && !o.flying && o.kind == EntityKind::Troop && o.team == s.team &&
                        std::abs(o.position.z) < 3.2) {
                        if (o.position.x < 0)
                            left += .20;
                        else
                            right += .20;
                    }
                s.bridge = left <= right ? -1 : 1;
            }
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
                          {x, direction * (1.65 + .18)},  {x, direction * (1.65 + .92)},  t.position};
            }
        }
        while (!s.path.empty() && D(s.position, s.path.front()) < .34)
            s.path.erase(s.path.begin());
        if (!s.path.empty()) {
            Vec2 d{s.path.front().x - s.position.x, s.path.front().z - s.position.z};
            const double distance = std::hypot(d.x, d.z);
            if (distance > 1e-9) {
                Vec2 next{s.position.x + d.x / distance * std::min(step, distance),
                          s.position.z + d.z / distance * std::min(step, distance)};
                if (NavValid(next, s, t.id, s.bridge)) {
                    s.position = next;
                    s.facing = {d.x / distance, d.z / distance};
                } else {
                    s.path.clear();
                    s.repathClock = 0;
                }
            }
        }
        const bool onBridge = std::abs(s.position.z) < 1.65 + .85;
        Vec2 push{};
        int overlapping = 0;
        for (const auto &o : state_.entities)
            if (!o.dead && o.kind == EntityKind::Troop && !o.flying && o.team == s.team && o.id != s.id) {
                const double dx = s.position.x - o.position.x, dz = s.position.z - o.position.z,
                             distance = std::hypot(dx, dz),
                             desired = s.radius + o.radius + (onBridge ? .03 : .12);
                if (distance < desired && distance > .001) {
                    const double amount = (desired - distance) / desired;
                    if (!onBridge)
                        push.x += dx / distance * amount;
                    push.z += dz / distance * amount * (onBridge ? .42 : 1);
                    ++overlapping;
                }
            }
        if (!onBridge)
            for (const auto &structure : state_.entities)
                if (!structure.dead && structure.kind != EntityKind::Troop && structure.id != t.id) {
                    const double dx = s.position.x - structure.position.x,
                                 dz = s.position.z - structure.position.z, distance = std::hypot(dx, dz),
                                 desired = s.radius + structure.radius * .72;
                    if (distance > .001 && distance < desired) {
                        const double force = (desired - distance) / desired;
                        push.x += dx / distance * force * 1.3;
                        push.z += dz / distance * force * 1.3;
                        ++overlapping;
                    }
                }
        if (overlapping > 0) {
            push.x *= dt * (onBridge ? .8 : 2.1) / overlapping;
            push.z *= dt * (onBridge ? .8 : 2.1) / overlapping;
        }
        Vec2 separated{s.position.x + push.x, s.position.z + push.z};
        if (NavValid(separated, s, t.id, s.bridge))
            s.position = separated;
        if (std::abs(s.position.z) < 1.65 + .28) {
            const int lane = s.bridge ? s.bridge : (s.position.x < 0 ? -1 : 1);
            const double half = std::max(.34, 2.1 - .16 - s.radius * .92);
            s.position.x = Clamp(s.position.x, lane * 7.2 - half, lane * 7.2 + half);
        }
        s.position.x = Clamp(s.position.x, -13.5 + s.radius * .72, 13.5 - s.radius * .72);
        s.position.z = Clamp(s.position.z, -20.5 + s.radius * .72, 20.5 - s.radius * .72);
        s.stuckClock += dt;
        if (s.stuckClock >= .28) {
            const double distance = D(s.position, t.position);
            const bool progress = s.stuckDistance - distance > .055 || D(s.position, s.stuckPosition) > .055;
            s.stuckTime =
                progress ? std::max(0., s.stuckTime - s.stuckClock * 1.6) : s.stuckTime + s.stuckClock;
            s.stuckDistance = distance;
            s.stuckPosition = s.position;
            s.stuckClock = 0;
            if (s.stuckTime > .82) {
                s.path.clear();
                s.repathClock = 0;
                s.stuckTime = 0;
                Vec2 forward{t.position.x - s.position.x, t.position.z - s.position.z};
                double length = std::hypot(forward.x, forward.z);
                if (length > 1e-9) {
                    forward = {forward.x / length, forward.z / length};
                    for (Vec2 probe : {Vec2{forward.x, forward.z}, Vec2{-forward.z, forward.x},
                                       Vec2{forward.z, -forward.x}}) {
                        Vec2 p{s.position.x + probe.x * .22, s.position.z + probe.z * .22};
                        if (NavValid(p, s, t.id, s.bridge)) {
                            s.position.x += probe.x * .14;
                            s.position.z += probe.z * .14;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (c->chargeDamage > 0 && D(before, s.position) > .002) {
        s.chargeTime += dt;
        if (s.chargeTime > 1.65)
            s.charged = true;
    }
}
} // namespace rift
