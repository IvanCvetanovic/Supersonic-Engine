// ensure_flow_fields (src/sim/flow.rs) and step_movement (src/sim/movement.rs).
//
// Movement runs in three passes: snapshot every unit (sorted by SimId) plus a
// spatial hash; compute each unit's velocity and arrival from the snapshot
// alone; then integrate, slide along walls and pop or advance orders.

#include "World.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace husk {

namespace {

constexpr float kSeparationGain = 5.0f;    // [TUNE]
constexpr float kArriveRadius = 1.1f;      // [TUNE]
constexpr float kCrowdArriveRadius = 12.0f; // [TUNE]
constexpr float kHashCell = 2.0f;          // at least the largest interaction diameter
constexpr float kDirectChaseRange = 4.0f;
constexpr float kChannelRange = 1.0f;      // economy::CHANNEL_RANGE

struct Goal {
    uint32_t cell;
    Vec2 target;
    bool pops; // a Point order pops on arrival; a Patrol bounces
};

struct Chase {
    Vec2 target;
    float stop; // centre distance
};

struct Channel {
    uint32_t cell;
    Vec2 target;
    float stop;
};

struct Snap {
    uint32_t id = 0;
    Vec2 pos;
    float radius = 0.0f;
    float speed = 0.0f;
    std::optional<Goal> goal;
    std::optional<Chase> chase;
    std::optional<Channel> channel;
    bool hold = false;
    std::optional<uint32_t> arrivedGoal;
};

struct Outcome {
    Vec2 vel;
    std::optional<uint32_t> arrive; // the goal cell of a point order completed this tick
};

std::pair<int32_t, int32_t> hashCell(Vec2 p) {
    // floor-divide so negative coordinates bucket correctly
    return {asI32(std::floor(p.x / kHashCell)), asI32(std::floor(p.y / kHashCell))};
}

// Along a flow field, falling back to straight-line inside the goal cell or
// where the field is flat.
Vec2 fieldDir(const FlowFields& fields, const NavGrid& grid, uint32_t goalCell, Vec2 target, Vec2 pos) {
    const Vec2 toGoal = target - pos;
    const float dist = toGoal.length();
    if (dist <= 1e-4f) return kZero;
    auto it = fields.find(goalCell);
    if (it == fields.end()) return kZero; // field not built yet
    const FlowField& field = it->second;
    const uint32_t cell = grid.cellAt(pos);
    if (field.cost[cell] == kUnreachable) return kZero;
    const Vec2 flow = field.dir[cell];
    if (cell == goalCell || flow == kZero) return toGoal / dist;
    return flow;
}

} // namespace

void ensureFlowFields(World& w) {
    std::set<uint32_t> referenced;
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        for (const Order& o : e.orders) {
            switch (o.kind) {
            case OrderKind::Point:
            case OrderKind::Patrol:
            case OrderKind::Extract:
            case OrderKind::Repair:
            case OrderKind::Build:
            case OrderKind::Pickup: referenced.insert(o.goalCell); break;
            case OrderKind::Cast:
                if (o.hasGoal) referenced.insert(o.goalCell);
                break;
            case OrderKind::Attack:
            case OrderKind::Hold: break;
            }
        }
    }
    for (auto it = w.flowFields.begin(); it != w.flowFields.end();) {
        it = referenced.count(it->first) ? std::next(it) : w.flowFields.erase(it);
    }
    for (uint32_t goal : referenced) {
        if (!w.flowFields.count(goal)) w.flowFields.emplace(goal, FlowField::compute(w.grid, goal));
    }
}

void stepMovement(World& w) {
    const Catalogs& c = w.cat();

    // ---- pass 1a: unit positions by id, for chase-target resolution ----
    std::map<uint32_t, std::pair<Vec2, float>> posById;
    for (const auto& [id, e] : w.entities) {
        if (e.isUnit()) posById[id] = {e.pos.cur, e.mover.radius};
    }
    auto resolve = [&](uint32_t id) -> std::optional<std::pair<Vec2, float>> {
        if (auto it = posById.find(id); it != posById.end()) return it->second;
        if (auto it = w.statics.find(id); it != w.statics.end()) return it->second;
        return std::nullopt;
    };

    // ---- pass 1b: the snapshot, in SimId order ----
    std::vector<Snap> snaps;
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        Snap s;
        s.id = id;
        s.pos = e.pos.cur;
        s.radius = e.mover.radius;
        s.speed = e.mover.speed * fmaxr(e.mods.moveMult, 0.1f);
        s.arrivedGoal = e.arrived;
        const Order* front = e.orders.empty() ? nullptr : &e.orders.front();
        s.hold = front && front->kind == OrderKind::Hold;
        if (front && front->kind == OrderKind::Point) s.goal = Goal{front->goalCell, front->target, true};
        if (front && front->kind == OrderKind::Patrol) s.goal = Goal{front->goalCell, front->target, false};
        if (front) {
            switch (front->kind) {
            case OrderKind::Extract:
            case OrderKind::Repair:
            case OrderKind::Build:
                if (auto t = resolve(front->id)) {
                    s.channel = Channel{front->goalCell, t->first, e.mover.radius + t->second + kChannelRange * 0.9f};
                }
                break;
            case OrderKind::Pickup:
                if (auto t = resolve(front->id)) s.channel = Channel{front->goalCell, t->first, 1.2f};
                break;
            case OrderKind::Cast:
                if (front->hasGoal) {
                    // walk into cast range of the target or point
                    const AbilityDef& def = c.abilities.def(c.abilities.id(c.hero.abilities[front->ability]));
                    const float stop = fmaxr(def.castRange * 0.92f, 0.5f);
                    if (front->castTarget.kind == CastTarget::Kind::Id) {
                        if (auto t = resolve(front->castTarget.id)) s.channel = Channel{front->goalCell, t->first, stop};
                    } else if (front->castTarget.kind == CastTarget::Kind::Point) {
                        s.channel = Channel{front->goalCell, front->castTarget.point, stop};
                    }
                }
                break;
            default: break;
            }
        }
        if (!s.hold && e.combat.target) {
            if (const auto& attack = c.units.def(e.unitKind).attack) {
                if (auto t = resolve(*e.combat.target)) {
                    s.chase = Chase{t->first, attack->range + e.mover.radius + t->second};
                }
            }
        }
        snaps.push_back(s);
    }

    // Spatial hash over the snapshot; buckets stay in ascending SimId order
    // because they are filled in order. Looked up, never iterated.
    std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> buckets;
    for (size_t i = 0; i < snaps.size(); ++i) buckets[hashCell(snaps[i].pos)].push_back(i);

    // ---- pass 2: steer from the snapshot only ----
    std::vector<Outcome> outcomes;
    outcomes.reserve(snaps.size());
    for (size_t i = 0; i < snaps.size(); ++i) {
        const Snap& s = snaps[i];
        Vec2 vel = kZero;
        std::optional<uint32_t> arrive;
        bool engaged = false;

        if (s.hold) {
            // stand ground
        } else if (s.chase) {
            // straight-line combat chase until inside weapon range
            const float d = s.pos.distance(s.chase->target);
            if (d > s.chase->stop && d > 1e-4f) vel = (s.chase->target - s.pos) / d * s.speed;
            engaged = true;
        } else if (s.channel) {
            // walk to the channel target via its flow field
            const float d = s.pos.distance(s.channel->target);
            if (d > s.channel->stop) {
                const Vec2 dir = d <= kDirectChaseRange
                                     ? (s.channel->target - s.pos) / d
                                     : fieldDir(w.flowFields, w.grid, s.channel->cell, s.channel->target, s.pos);
                vel = dir * s.speed;
            }
            engaged = true;
        } else if (s.goal) {
            const float distGoal = (s.goal->target - s.pos).length();
            if (distGoal <= kArriveRadius) {
                arrive = s.goal->cell;
            } else if (auto it = w.flowFields.find(s.goal->cell); it != w.flowFields.end()) {
                if (it->second.cost[w.grid.cellAt(s.pos)] == kUnreachable) {
                    arrive = s.goal->cell; // no path from here - abandon the order
                } else {
                    vel = fieldDir(w.flowFields, w.grid, s.goal->cell, s.goal->target, s.pos) * s.speed;
                }
            }
        }

        // separation and crowd arrival from neighbours: fixed 3x3 cell order,
        // ascending id inside each bucket
        Vec2 push = kZero;
        const auto center = hashCell(s.pos);
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                auto it = buckets.find({center.first + dx, center.second + dy});
                if (it == buckets.end()) continue;
                for (size_t j : it->second) {
                    if (j == i) continue;
                    const Snap& other = snaps[j];
                    const Vec2 off = s.pos - other.pos;
                    const float dist = off.length();
                    const float touch = s.radius + other.radius;
                    // crowd arrival only applies to popping point orders
                    if (!arrive && !engaged && s.goal && s.goal->pops && other.arrivedGoal == s.goal->cell &&
                        dist <= touch + 0.2f && (s.goal->target - s.pos).length() <= kCrowdArriveRadius) {
                        arrive = s.goal->cell;
                    }
                    const float minD = touch + 0.08f;
                    if (dist < minD) {
                        Vec2 dir;
                        if (dist > 1e-4f) {
                            dir = off / dist;
                        } else {
                            dir = s.id < other.id ? kUnitX : -kUnitX;
                        }
                        push += dir * (minD - dist) * kSeparationGain;
                    }
                }
            }
        }

        const bool moving = (s.goal || s.chase || s.channel) && !arrive && !s.hold;
        if (moving) {
            vel = (vel + push).clampLengthMax(s.speed);
        } else {
            // idle, arrived or holding units only unstack, slowly, with a
            // deadzone so settled crowds don't jitter
            vel = push.lengthSquared() > 0.0025f ? push.clampLengthMax(s.speed * 0.5f) : kZero;
        }
        outcomes.push_back({vel, arrive});
    }

    // ---- pass 3: apply ----
    const float half = w.grid.halfExtent();
    for (size_t i = 0; i < snaps.size(); ++i) {
        const Snap& s = snaps[i];
        const Outcome& out = outcomes[i];
        Entity* e = w.get(s.id);
        if (!e) continue;
        const Vec2 old = e->pos.cur;
        Vec2 newPos = old + out.vel * kSimDt;
        if (w.grid.isBlockedWorld(old)) {
            // trapped inside freshly blocked cells: walk straight out to the
            // nearest open cell, ignoring collision
            const Vec2 escape = w.grid.cellCenter(w.grid.nearestOpen(w.grid.cellAt(old)));
            const Vec2 dir = escape - old;
            if (dir.length() > 1e-4f) newPos = old + dir.normalize() * s.speed * kSimDt;
        } else if (!w.grid.stepOkWorld(old, newPos)) {
            // wall and cliff slide: try the axis-separated components first
            const Vec2 xOnly(newPos.x, old.y);
            const Vec2 yOnly(old.x, newPos.y);
            if (w.grid.stepOkWorld(old, xOnly)) {
                newPos = xOnly;
            } else if (w.grid.stepOkWorld(old, yOnly)) {
                newPos = yOnly;
            } else {
                newPos = old;
            }
        }
        newPos = newPos.clamp(Vec2::splat(-half + s.radius), Vec2::splat(half - s.radius));
        e->pos.prev = old;
        e->pos.cur = newPos;

        if (out.arrive && !e->orders.empty()) {
            Order& front = e->orders.front();
            if (front.kind == OrderKind::Patrol) {
                // bounce: swap the endpoints, snap the new leg
                const Vec2 next = front.home;
                front.home = front.target;
                const uint32_t cell = w.grid.nearestOpen(w.grid.cellAt(next));
                front.goalCell = cell;
                front.target = w.grid.cellCenter(cell);
            } else if (front.kind == OrderKind::Point) {
                e->orders.pop_front();
                if (e->orders.empty()) e->arrived = out.arrive;
            }
        }
    }
}

} // namespace husk
