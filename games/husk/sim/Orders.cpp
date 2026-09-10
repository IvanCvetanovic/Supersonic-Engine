// drain_orders (src/sim/orders.rs): every command pushed since the last tick,
// validated and paid for inside the tick. The UI only previews affordability;
// this is where it is decided.

#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace husk {

namespace {

// ~1 world unit of gap on top of a typical radius, for group moves. [TUNE]
constexpr float kFormationSpacing = 1.8f;

bool eqIgnoreAsciiCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool offers(const std::vector<std::string>& names, std::string_view name) {
    return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return eqIgnoreAsciiCase(n, name); });
}

// A goal cell next to an addressed target: statics first, then live units.
std::optional<uint32_t> goalNear(const World& w, uint32_t id) {
    std::optional<Vec2> pos;
    if (auto it = w.statics.find(id); it != w.statics.end()) {
        pos = it->second.first;
    } else if (const Entity* e = w.indexed(id); e && e->isUnit()) {
        pos = e->pos.cur;
    }
    if (!pos) return std::nullopt;
    return w.grid.nearestOpen(w.grid.cellAt(*pos));
}

} // namespace

void drainOrders(World& w) {
    if (w.orderQueue.empty()) return;
    const Catalogs& c = w.cat();

    // Will headroom for this drain. update_will recomputes the reservations
    // next tick; within one drain they are tracked here, so two messages cannot
    // both spend the last slot.
    uint32_t willUsed = w.will.used;

    std::vector<OrderMsg> queue;
    queue.swap(w.orderQueue);

    for (const OrderMsg& msg : queue) {
        switch (msg.kind) {
        case OrderMsg::Kind::Produce: {
            Entity* b = w.indexed(msg.id);
            if (!b || !b->isBuilding()) break;
            if (msg.kind16 >= c.units.defs.size()) break;
            const UnitDef& def = c.units.def(msg.kind16);
            const bool producible = offers(c.buildings.def(b->building.kind).produces, def.name);
            if (b->team != 0 || !b->building.complete() || !producible || b->building.queue.size() >= kQueueCap ||
                !w.affinity.satisfies(def.requirement) || w.economy.essence < def.costEssence ||
                w.economy.anima < def.costAnima || willUsed + def.will > w.will.cap) {
                break;
            }
            w.economy.essence -= def.costEssence;
            w.economy.anima -= def.costAnima;
            willUsed += def.will;
            b->building.queue.push_back(msg.kind16);
            break;
        }
        case OrderMsg::Kind::CancelProduce: {
            Entity* b = w.indexed(msg.id);
            if (!b || !b->isBuilding()) break;
            // Not gated on complete(): a refund must never be unreachable.
            if (b->team != 0) break;
            if (b->building.queue.empty()) break;
            const uint16_t kind = b->building.queue.back();
            if (kind >= c.units.defs.size()) break;
            const UnitDef& def = c.units.def(kind);
            w.economy.essence += def.costEssence;
            w.economy.anima += def.costAnima;
            willUsed = willUsed > def.will ? willUsed - def.will : 0; // saturating_sub
            // The back is the in-progress front only when it is the sole entry.
            if (b->building.queue.size() == 1) b->building.queueProgress = 0.0f;
            b->building.queue.pop_back();
            break;
        }
        case OrderMsg::Kind::SetRally: {
            Entity* b = w.indexed(msg.id);
            if (b && b->isBuilding() && b->team == 0) b->building.rally = msg.target;
            break;
        }
        case OrderMsg::Kind::Cast: {
            Entity* hero = w.indexed(msg.id);
            if (!hero) break;
            // bound-check before indexing ranks[ability]
            if (msg.ability >= 4) break;
            if (!hero->hero || hero->hero->ranks[msg.ability] == 0) break;
            std::optional<uint32_t> goal;
            if (msg.cast.kind == CastTarget::Kind::Id) {
                goal = goalNear(w, msg.cast.id);
                if (!goal) break;
            } else if (msg.cast.kind == CastTarget::Kind::Point) {
                goal = w.grid.nearestOpen(w.grid.cellAt(msg.cast.point));
            }
            if (!hero->isUnit()) break;
            if (!msg.queued) hero->orders.clear();
            hero->orders.push_back(Order::cast(msg.ability, msg.cast, goal));
            hero->arrived.reset();
            break;
        }
        case OrderMsg::Kind::Learn: {
            Entity* e = w.indexed(msg.id);
            if (!e || !e->hero) break;
            Hero& h = *e->hero;
            if (msg.ability >= 4 || h.points == 0) break;
            const AbilityDef& def = c.abilities.def(c.abilities.id(c.hero.abilities[msg.ability]));
            const uint8_t rank = h.ranks[msg.ability];
            if (rank >= def.maxRank() || h.level < def.maxLevelGate[rank]) break;
            h.ranks[msg.ability] += 1;
            h.points -= 1;
            break;
        }
        case OrderMsg::Kind::Revive: {
            if (w.heroState.kind != HeroState::Kind::Dead) break;
            const Hero saved = w.heroState.saved;
            const float cost = c.hero.reviveCost(saved.level);
            if (w.economy.essence < cost) break;
            w.economy.essence -= cost;
            const uint64_t readyTick = w.tick + asU64(c.hero.reviveTime * kSimHzF);
            w.heroState = HeroState::reviving(saved, readyTick);
            break;
        }
        case OrderMsg::Kind::Pickup: {
            Entity* hero = w.indexed(msg.id);
            if (!hero || !hero->hero) break;
            const Entity* item = w.indexed(msg.item);
            if (!item || !item->isItem()) break;
            auto it = w.statics.find(msg.item);
            if (it == w.statics.end()) break;
            const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(it->second.first));
            if (!msg.queued) hero->orders.clear();
            hero->orders.push_back(Order::pickup(msg.item, goal));
            hero->arrived.reset();
            break;
        }
        case OrderMsg::Kind::DropItem: {
            Entity* hero = w.indexed(msg.id);
            if (!hero || !hero->hero) break;
            if (msg.slot >= kInventorySlots) break;
            std::optional<uint16_t> kind = hero->hero->inventory[msg.slot];
            hero->hero->inventory[msg.slot].reset();
            if (!kind) break;
            w.pendingDrops.emplace_back(*kind, hero->pos.cur + Vec2(0.9f, 0.7f));
            break;
        }
        case OrderMsg::Kind::UseItem: {
            Entity* hero = w.indexed(msg.id);
            if (!hero) break;
            if (msg.slot >= kInventorySlots) break;
            // must actually hold a CONSUMABLE
            if (!hero->hero) break;
            const std::optional<uint16_t> kind = hero->hero->inventory[msg.slot];
            if (!kind) break;
            const std::optional<ActiveEffect>& active = c.items.def(*kind).active;
            if (!active) break;
            // spent only if it did something: a heal at full hp is a no-op
            bool used = false;
            if (active->kind == ActiveEffect::Kind::Heal) {
                if (hero->hasTeam() && hero->health.cur > 0.0f && hero->health.cur < hero->health.max) {
                    hero->health.cur = fminr(hero->health.cur + active->amount, hero->health.max);
                    used = true;
                }
            } else {
                // The Overcharge buff with no burn - through Commands, so it
                // lands after this system, as in the game.
                const OverchargeBuff buff{w.tick + asU64(active->secs * kSimHzF), active->atkSpeed,
                                          active->moveSpeed, 0.0f};
                const uint32_t id = msg.id;
                w.defer([id, buff](World& world) {
                    if (Entity* e = world.get(id)) e->overcharge = buff;
                });
                used = true;
            }
            if (!used) break;
            hero->hero->inventory[msg.slot].reset();
            if (hero->isUnit()) {
                CastFx fx;
                fx.kind = active->kind == ActiveEffect::Kind::Heal ? CastFx::Kind::Heal : CastFx::Kind::Overcharge;
                fx.pos = hero->pos.cur;
                w.events.casts.push_back(fx);
            }
            break;
        }
        case OrderMsg::Kind::Research: {
            if (msg.kind16 >= c.upgrades.defs.size()) break;
            const Entity* b = w.indexed(msg.id);
            if (!b || !b->isBuilding()) break;
            const UpgradeDef& def = c.upgrades.def(msg.kind16);
            const bool offered = offers(c.buildings.def(b->building.kind).researches, def.name);
            // one research at a time; not past max level; no overspending
            if (b->team != 0 || !b->building.complete() || !offered || w.research.inProgress ||
                w.research.level(msg.kind16) >= def.maxLevel || w.economy.essence < def.costEssence ||
                w.economy.anima < def.costAnima) {
                break;
            }
            w.economy.essence -= def.costEssence;
            w.economy.anima -= def.costAnima;
            w.research.inProgress = Research{msg.kind16, w.tick + asU64(def.time * kSimHzF)};
            break;
        }
        case OrderMsg::Kind::PlaceBuilding: {
            if (msg.kind16 >= c.buildings.defs.size()) break;
            const BuildingDef& def = c.buildings.def(msg.kind16);
            if (w.economy.essence < def.costEssence) break;
            const Entity* builder = w.indexed(msg.id);
            if (!builder || !builder->isUnit()) break;
            // the hero raises only the Sanctum; Siphons build everything else
            const bool isWorker = c.units.def(builder->unitKind).worker;
            const bool isHero = builder->hero.has_value();
            if (!isWorker && !isHero) break;
            if (isHero && !isWorker && !eqIgnoreAsciiCase(def.name, "sanctum")) break;
            const uint32_t fw = def.footprint[0];
            const uint32_t fh = def.footprint[1];
            const auto [snapped, min] = w.grid.snapFootprint(msg.target, fw, fh);
            std::vector<std::pair<Vec2, float>> units;
            for (const auto& [id, e] : w.entities) {
                if (e.isUnit()) units.emplace_back(e.pos.cur, e.mover.radius);
            }
            if (!placementClear(w.grid, min, fw, fh, units)) break;
            w.economy.essence -= def.costEssence;
            w.placements.push_back({msg.id, msg.kind16, snapped});
            break;
        }
        case OrderMsg::Kind::Point:
        case OrderMsg::Kind::Attack:
        case OrderMsg::Kind::Patrol:
        case OrderMsg::Kind::Hold:
        case OrderMsg::Kind::Stop:
        case OrderMsg::Kind::Extract:
        case OrderMsg::Kind::Repair:
        case OrderMsg::Kind::Build: {
            std::vector<uint32_t> ids = msg.units;
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

            // A group move spreads into a centred grid around the target. A
            // unit's slot is its place in the sorted ids, so the layout is
            // deterministic.
            const size_t groupN = ids.size();
            size_t cols = static_cast<size_t>(asU64(std::ceil(std::sqrt(static_cast<float>(groupN)))));
            if (cols < 1) cols = 1;
            auto formationOffset = [&](size_t slot) -> Vec2 {
                if (groupN <= 1) return kZero;
                const size_t rows = (groupN + cols - 1) / cols;
                const size_t col = slot % cols;
                const size_t row = slot / cols;
                const float cx = static_cast<float>(cols - 1) * 0.5f;
                const float cy = static_cast<float>(rows - 1) * 0.5f;
                return {(static_cast<float>(col) - cx) * kFormationSpacing,
                        (static_cast<float>(row) - cy) * kFormationSpacing};
            };
            auto snap = [&](Vec2 target) -> std::pair<uint32_t, Vec2> {
                const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(target));
                return {goal, w.grid.cellCenter(goal)};
            };

            std::optional<uint32_t> channelGoal;
            if (msg.kind == OrderMsg::Kind::Extract || msg.kind == OrderMsg::Kind::Repair ||
                msg.kind == OrderMsg::Kind::Build) {
                channelGoal = goalNear(w, msg.id);
            }

            for (size_t slot = 0; slot < ids.size(); ++slot) {
                const uint32_t id = ids[slot];
                Entity* e = w.indexed(id);
                if (!e || !e->isUnit()) continue;
                // the hero can do everything a worker can
                const bool worker = c.units.def(e->unitKind).worker || e->hero.has_value();
                switch (msg.kind) {
                case OrderMsg::Kind::Point: {
                    // Formation spreads a plain MOVE only; an attack-move keeps
                    // the single-target funnel.
                    const Vec2 moveTo = msg.attack ? msg.target : msg.target + formationOffset(slot);
                    const auto [goal, target] = snap(moveTo);
                    if (!msg.queued) {
                        e->orders.clear();
                        if (!msg.attack) e->combat.target.reset();
                    }
                    e->orders.push_back(Order::point(msg.attack, goal, target));
                    break;
                }
                case OrderMsg::Kind::Attack:
                    if (msg.id == id) continue;
                    if (!msg.queued) {
                        e->orders.clear();
                        e->combat.target.reset();
                    }
                    e->orders.push_back(Order::attackUnit(msg.id));
                    break;
                case OrderMsg::Kind::Patrol: {
                    const auto [goal, target] = snap(msg.target);
                    e->orders.clear();
                    e->orders.push_back(Order::patrol(goal, target, e->pos.cur));
                    break;
                }
                case OrderMsg::Kind::Hold:
                    e->orders.clear();
                    e->orders.push_back(Order::hold());
                    break;
                case OrderMsg::Kind::Stop:
                    e->orders.clear();
                    e->combat.target.reset();
                    break;
                case OrderMsg::Kind::Extract:
                    if (!channelGoal || !worker) continue;
                    if (!msg.queued) e->orders.clear();
                    e->orders.push_back(Order::extract(msg.id, *channelGoal));
                    break;
                case OrderMsg::Kind::Repair:
                    if (!channelGoal || !worker || msg.id == id) continue;
                    if (!msg.queued) e->orders.clear();
                    e->orders.push_back(Order::repair(msg.id, *channelGoal));
                    break;
                case OrderMsg::Kind::Build:
                    if (!channelGoal || !worker) continue;
                    if (!msg.queued) e->orders.clear();
                    e->orders.push_back(Order::build(msg.id, *channelGoal));
                    break;
                default: break;
                }
                e->arrived.reset();
            }
            break;
        }
        }
    }
}

} // namespace husk
