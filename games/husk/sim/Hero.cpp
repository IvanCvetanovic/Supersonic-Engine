// The hero (src/sim/hero.rs) and loot (src/sim/loot.rs): XP and levels, the
// 3+1 ability kit, inventory, buffs, death and revival, and the drops that
// feed the inventory.

#include "World.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>
#include <vector>

namespace husk {

namespace {

void popFront(World& w, uint32_t id) {
    if (Entity* e = w.get(id); e && !e->orders.empty()) e->orders.pop_front();
}

void pushCast(World& w, CastFx::Kind kind, Vec2 pos, Vec2 to = {}, float radius = 0.0f) {
    CastFx fx;
    fx.kind = kind;
    fx.pos = pos;
    fx.to = to;
    fx.radius = radius;
    w.events.casts.push_back(fx);
}

// Death handling for ability kills, mirroring tick_attacks - but immediate,
// since hero_actions is exclusive.
void killTarget(World& w, uint32_t victimId) {
    Entity* v = w.get(victimId);
    if (!v) return;
    const Vec2 pos = v->pos.cur;
    const float radius = v->mover.radius;
    const uint8_t team = v->hasTeam() ? v->team : 255;
    const float xp = v->isUnit() ? w.cat().units.def(v->unitKind).xpValue : 0.0f;
    // structures never drop, so they take the sentinel
    const uint16_t victimKind = v->isUnit() ? v->unitKind : UINT16_MAX;
    w.events.deaths.push_back({pos, radius, team});
    w.events.kills.push_back({pos, team, xp, victimKind});
    if (v->hero) w.heroState = HeroState::dead(*v->hero, w.tick);
    w.unindex(victimId);
    w.statics.erase(victimId);
    if (v->isBuilding()) {
        w.grid.setBlocked(v->building.cells, false);
        w.flowFields.clear();
    }
    w.despawnNow(victimId);
}

void executePickup(World& w, uint32_t heroId, uint32_t itemId) {
    Entity* item = w.indexed(itemId);
    if (!item || !item->isItem()) {
        popFront(w, heroId);
        return;
    }
    Entity* h = w.get(heroId);
    if (h->pos.cur.distance(item->pos.cur) > 1.6f) return; // still walking in
    Hero& hero = *h->hero;
    auto slot = std::find_if(hero.inventory.begin(), hero.inventory.end(),
                             [](const std::optional<uint16_t>& s) { return !s.has_value(); });
    if (slot == hero.inventory.end()) {
        popFront(w, heroId); // bags full
        return;
    }
    *slot = item->itemKind;
    w.unindex(itemId);
    w.statics.erase(itemId);
    w.despawnNow(itemId);
    popFront(w, heroId);
}

void castPulse(World& w, uint32_t caster, Vec2 center, uint8_t team, uint64_t until, const AbilityRank& params) {
    // enemies in radius, in SimId order
    std::vector<uint32_t> victims;
    for (const auto& [id, e] : w.entities) {
        if (id == caster || !e.hasTeam()) continue;
        if (e.team != team && e.health.cur > 0.0f && e.pos.cur.distance(center) <= params.radius) victims.push_back(id);
    }
    for (uint32_t v : victims) {
        Entity* e = w.get(v);
        e->health.cur -= params.damage;
        if (e->health.cur <= 0.0f) {
            killTarget(w, v);
        } else if (params.slow > 0.0f) {
            e->slow = SlowDebuff{until, params.slow};
        }
    }
    pushCast(w, CastFx::Kind::Pulse, center, {}, params.radius);
}

void castAvatar(World& w, uint32_t caster, Vec2 center, uint64_t until, const AbilityRank& params) {
    Entity* self = w.get(caster);
    self->avatar = AvatarBuff{until, params.hpBonus, params.dmgBonus};
    // the new max hp arrives filled
    self->health.cur += params.hpBonus;
    const uint8_t heroTeam = self->team;

    // animate nearby husks, lowest SimId first
    std::vector<uint32_t> husks;
    for (const auto& [id, e] : w.entities) {
        if (e.isSource() && e.source.husked() && !e.source.animated && e.pos.cur.distance(center) <= params.radius) {
            husks.push_back(id);
        }
    }
    if (husks.size() > params.thralls) husks.resize(params.thralls);

    const uint16_t thrallKind = w.cat().units.id("thrall");
    for (uint32_t huskId : husks) {
        Entity* husk = w.get(huskId);
        husk->source.animated = true;
        const Vec2 spawnPos = w.grid.cellCenter(w.grid.nearestOpen(w.grid.cellAt(husk->pos.cur)));
        const uint32_t thrall = spawnUnit(w, thrallKind, heroTeam, spawnPos);
        w.get(thrall)->timedLife = TimedLife{until};
    }
    pushCast(w, CastFx::Kind::Avatar, center);
}

void executeCast(World& w, uint32_t heroId, uint8_t slot, CastTarget target) {
    const Catalogs& c = w.cat();
    const Entity* h = w.get(heroId);
    const Vec2 heroPos = h->pos.cur;
    const uint8_t heroTeam = h->team;
    const Hero hero = *h->hero;
    const uint8_t rank = hero.ranks[slot];
    if (rank == 0) {
        popFront(w, heroId);
        return;
    }
    const AbilityDef& def = c.abilities.def(c.abilities.id(c.hero.abilities[slot]));
    if (hero.cooldowns[slot] > 0.0f) return; // wait in place for the cooldown
    const AbilityRank params = def.rank(rank);

    // range gate for targeted casts; movement is walking us in
    std::optional<std::pair<uint32_t, Vec2>> targetPos;
    if (target.kind == CastTarget::Kind::Id) {
        const Entity* t = w.indexed(target.id);
        if (!t) {
            popFront(w, heroId);
            return;
        }
        targetPos = {{target.id, t->pos.cur}};
    }
    if (targetPos && heroPos.distance(targetPos->second) > def.castRange + 0.5f) return; // not in range yet

    const uint64_t until = w.tick + asU64(params.duration * kSimHzF);

    switch (def.kind) {
    case AbilityKind::TargetEnemyOrSource: {
        if (!targetPos) {
            popFront(w, heroId);
            return;
        }
        Entity* t = w.get(targetPos->first);
        float total = 0.0f;
        if (t->isSource()) {
            // drain an object: essence to the bank, vitality to the hero
            const float take = fminr(params.drain, t->source.essence);
            t->source.essence -= take;
            w.economy.essence += take;
            // surge drains are extraction: they unlock affinities like Siphon work
            w.affinity.cumulative[static_cast<size_t>(t->source.affinity)] += take;
            total = take;
        } else if (t->hasTeam() && t->team != heroTeam) {
            t->health.cur -= params.damage;
            const bool died = t->health.cur <= 0.0f;
            w.economy.essence += params.refund;
            if (died) killTarget(w, targetPos->first);
            total = params.damage;
        } else {
            popFront(w, heroId);
            return;
        }
        // lifesteal whether the target was a source or an enemy
        const float heal = total * params.healFrac;
        if (Entity* self = w.get(heroId)) self->health.cur = fminr(self->health.cur + heal, self->health.max);
        pushCast(w, CastFx::Kind::SurgeBeam, heroPos, targetPos->second);
        if (heal > 0.0f) pushCast(w, CastFx::Kind::Heal, heroPos);
        break;
    }
    case AbilityKind::TargetFriendlyUnit: {
        if (!targetPos) {
            popFront(w, heroId);
            return;
        }
        Entity* t = w.get(targetPos->first);
        const bool valid = t->isUnit() && t->team == heroTeam && !t->hero;
        if (!valid) {
            popFront(w, heroId);
            return;
        }
        t->overcharge = OverchargeBuff{until, params.atkSpeed, params.moveSpeed, params.burn};
        pushCast(w, CastFx::Kind::Overcharge, targetPos->second);
        break;
    }
    case AbilityKind::NoTarget:
        if (params.thralls > 0) {
            castAvatar(w, heroId, heroPos, until, params);
        } else {
            castPulse(w, heroId, heroPos, heroTeam, until, params);
        }
        break;
    }

    // pay the cooldown and finish the order
    if (Entity* self = w.get(heroId); self && self->hero) self->hero->cooldowns[slot] = params.cooldown;
    popFront(w, heroId);
}

} // namespace

void heroStats(World& w) {
    const Catalogs& c = w.cat();
    const HeroConfig& config = c.hero;
    for (auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        StatMods next;
        float maxHp = c.units.def(e.unitKind).hp;

        if (e.hero) {
            Hero& hero = *e.hero;
            for (float& cd : hero.cooldowns) cd = fmaxr(cd - kSimDt, 0.0f);
            const float levelUps = static_cast<float>(hero.level - 1);
            next.damageAdd += config.damagePerLevel * levelUps;
            next.regen += config.regenBase + config.regenPerLevel * levelUps;
            maxHp += config.hpPerLevel * levelUps;
            for (const auto& slot : hero.inventory) {
                if (!slot) continue;
                const ItemDef& item = c.items.def(*slot);
                next.damageAdd += item.damageAdd;
                next.regen += item.regenAdd;
                next.moveMult *= 1.0f + item.moveMultAdd;
                maxHp += item.hpAdd;
            }
        }
        if (e.overcharge) {
            next.atkSpeedMult *= 1.0f + e.overcharge->atkSpeed;
            next.moveMult *= 1.0f + e.overcharge->moveSpeed;
        }
        if (e.slow) next.moveMult *= 1.0f - e.slow->slow;
        if (e.avatar) {
            next.damageAdd += e.avatar->dmgBonus;
            maxHp += e.avatar->hpBonus;
        }

        // Affinity-tier payoff for the player's own constructs, and the
        // army-wide research - inside the team gate, because ResearchState is
        // global and an escaped multiply would buff the enemy.
        if (e.team == 0) {
            next.atkSpeedMult *= w.research.atkSpeedMult(c.upgrades);
            const UnitDef& def = c.units.def(e.unitKind);
            if (def.requirement) {
                const float tier = static_cast<float>(w.affinity.tier(def.requirement->affinity));
                if (tier > 0.0f) {
                    maxHp += def.hp * c.payoff.hpFracPerTier * tier;
                    if (def.attack) next.damageAdd += def.attack->damage * c.payoff.damageFracPerTier * tier;
                }
            }
        }

        if (std::fabs(e.health.max - maxHp) > FLT_EPSILON) e.health.max = maxHp;
        if (next.regen > 0.0f && e.health.cur > 0.0f) {
            e.health.cur = fminr(e.health.cur + next.regen * kSimDt, e.health.max);
        }
        e.health.cur = fminr(e.health.cur, e.health.max);
        e.mods = next;
    }
}

void applyXp(const HeroConfig& config, float xp, Vec2 pos, Hero& hero, Health& hp, SimEvents& events) {
    hero.xp += xp;
    while (hero.level < HeroConfig::kMaxLevel && hero.xp >= config.xpForLevel(static_cast<uint8_t>(hero.level + 1))) {
        hero.level += 1;
        hero.points += 1;
        // a level-up partially heals; the new hp arrives filled
        hp.cur = fminr(hp.cur + config.hpPerLevel, hp.max + config.hpPerLevel);
        CastFx fx;
        fx.kind = CastFx::Kind::LevelUp;
        fx.pos = pos;
        events.casts.push_back(fx);
    }
}

void xpAwards(World& w) {
    if (w.events.kills.empty()) return;
    std::vector<KillFx> kills;
    kills.swap(w.events.kills);
    const HeroConfig& config = w.cat().hero;
    for (const KillFx& kill : kills) {
        for (auto& [id, e] : w.entities) {
            if (!e.isUnit() || !e.hero) continue;
            if (e.team == kill.victimTeam || kill.xp <= 0.0f) continue;
            if (e.pos.cur.distance(kill.pos) > config.xpRadius) continue;
            applyXp(config, kill.xp, e.pos.cur, *e.hero, e.health, w.events);
        }
    }
}

void grantHeroXp(World& w, float xp) {
    if (xp <= 0.0f) return;
    if (w.heroState.kind != HeroState::Kind::Alive) return;
    Entity* e = w.indexed(w.heroState.id);
    if (!e || !e->hero) return;
    applyXp(w.cat().hero, xp, e->pos.cur, *e->hero, e->health, w.events);
}

void tickBuffs(World& w) {
    const uint64_t now = w.tick;
    // Through Commands in the game: the removals and despawns land after this
    // system, so a unit the first loop kills is still seen by the last one.
    for (auto& [id, e] : w.entities) {
        if (!e.overcharge) continue;
        if (now >= e.overcharge->untilTick) {
            const uint32_t entity = id;
            w.defer([entity](World& world) {
                if (Entity* x = world.get(entity)) x->overcharge.reset();
            });
            continue;
        }
        e.health.cur -= e.overcharge->burn * kSimDt;
        if (e.health.cur <= 0.0f) {
            w.events.deaths.push_back({e.pos.cur, e.mover.radius, 0}); // overcharge only lands on friendlies
            w.unindex(id);
            w.despawnDeferred(id);
        }
    }
    for (auto& [id, e] : w.entities) {
        if (e.slow && now >= e.slow->untilTick) {
            const uint32_t entity = id;
            w.defer([entity](World& world) {
                if (Entity* x = world.get(entity)) x->slow.reset();
            });
        }
    }
    for (auto& [id, e] : w.entities) {
        if (e.avatar && now >= e.avatar->untilTick) {
            const uint32_t entity = id;
            w.defer([entity](World& world) {
                if (Entity* x = world.get(entity)) x->avatar.reset();
            });
        }
    }
    for (auto& [id, e] : w.entities) {
        if (e.timedLife && now >= e.timedLife->untilTick) {
            w.events.deaths.push_back({e.pos.cur, e.mover.radius, 0}); // thralls are the player's
            w.unindex(id);
            w.despawnDeferred(id);
        }
    }
}

void heroActions(World& w) {
    // pending item drops
    std::vector<std::pair<uint16_t, Vec2>> drops;
    drops.swap(w.pendingDrops);
    for (const auto& [kind, pos] : drops) spawnItem(w, kind, pos);

    // heroes with an executable front order, in SimId order
    std::vector<uint32_t> heroes;
    for (const auto& [id, e] : w.entities) {
        if (e.hero) heroes.push_back(id);
    }
    for (uint32_t id : heroes) {
        const Entity* e = w.get(id);
        if (!e || e->orders.empty()) continue;
        const Order front = e->orders.front();
        if (front.kind == OrderKind::Cast) {
            executeCast(w, id, front.ability, front.castTarget);
        } else if (front.kind == OrderKind::Pickup) {
            executePickup(w, id, front.id);
        }
    }
}

void heroRevive(World& w) {
    if (w.heroState.kind != HeroState::Kind::Reviving || w.tick < w.heroState.tick) return;
    Hero restored = w.heroState.saved;
    w.heroState = HeroState{};

    // Back at the hero's Sanctum, the oldest by SimId; failing that the oldest
    // complete friendly building; failing that the origin.
    const uint16_t sanctum = w.cat().buildings.id("sanctum");
    std::optional<Vec2> first;
    std::optional<Vec2> firstSanctum;
    for (const auto& [id, e] : w.entities) {
        if (!e.isBuilding() || e.team != 0 || !e.building.complete()) continue;
        if (!first) first = e.pos.cur;
        if (!firstSanctum && e.building.kind == sanctum) firstSanctum = e.pos.cur;
    }
    const Vec2 spawnAt = firstSanctum ? *firstSanctum : (first ? *first : kZero);
    const Vec2 exit = w.grid.cellCenter(w.grid.nearestOpen(w.grid.cellAt(spawnAt + Vec2(0.0f, 3.0f))));
    const uint32_t id = spawnHero(w, exit);
    restored.cooldowns = {};
    w.get(id)->hero = restored;
    w.heroState = HeroState::alive(id);
    pushCast(w, CastFx::Kind::LevelUp, exit);
}

void rollLootDrops(World& w) {
    if (w.events.kills.empty()) return;
    // Read, not consumed: xp_awards drains the kills after this.
    struct Kill {
        uint16_t kind;
        uint8_t team;
        Vec2 pos;
    };
    std::vector<Kill> kills;
    for (const KillFx& k : w.events.kills) kills.push_back({k.victimKind, k.victimTeam, k.pos});

    for (const Kill& k : kills) {
        // only enemy units drop; structures carry the sentinel
        if (k.team == 0 || k.kind == UINT16_MAX) continue;
        const std::vector<std::pair<uint16_t, float>> entries = w.cat().loot.dropsFor(k.kind);
        for (const auto& [item, chance] : entries) {
            // one draw per entry, always, so the draw count is a pure function
            // of the tick's kills
            const float roll = w.rng.nextF32();
            if (roll < chance) spawnItem(w, item, k.pos);
        }
    }
}

} // namespace husk
