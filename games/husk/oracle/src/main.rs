//! The oracle the C++ port of HUSK's simulation is held to.
//!
//! Every number printed here comes from the game's own code, linked by path at
//! the pinned commit. Nothing is re-implemented except where the game keeps a
//! thing private, and then it is re-derived from public components. The game's
//! repository is never edited.
//!
//!   husk-oracle catalogs                 every parsed data value, floats as bits
//!   husk-oracle primitives               glam and f32 results on fixed inputs
//!   husk-oracle run <file.scn>           the SimHash after every tick
//!         [--domains]                    plus one sub-hash per domain
//!         [--entities N]                 plus a full entity dump every N ticks
//!
//! The C++ side prints the same text from the same inputs, and the suites
//! compare the two line by line. Floats are always their IEEE bits, so equal
//! text means bit-identical values, not values that happen to print alike.

use std::collections::HashMap;

use bevy::prelude::*;
use project_husk::sim::data::{
    self, AbilityKind, ActiveEffect, ArmorType, AttackDef, AttackType, UnitPassive, UpgradeEffect,
};
use project_husk::sim::difficulty::{self, Difficulty};
use project_husk::sim::economy::{
    AffinityState, EssenceSource, PlayerEconomy, ResearchState, WillState,
};
use project_husk::sim::hash::{Fnv1a, SimHash};
use project_husk::sim::hero::{
    AvatarBuff, Hero, HeroState, ItemPickup, OverchargeBuff, SlowDebuff, TimedLife,
};
use project_husk::sim::mission::{
    self, MissionOutcome, MissionState, ObjectiveStatus, SceneStep, TriggerAction, TriggerCond,
};
use project_husk::sim::orders::{CastTarget, Order, OrderMsg, OrderQueue, Orders};
use project_husk::sim::rng::{Pcg32, SimRng};
use project_husk::sim::unit::{
    Arrived, CombatState, Health, SimId, SimIdAlloc, SimPos, Team, UnitKind, spawn_squad,
};
use project_husk::sim::{SIM_DT, SimPlugin, SimStep, SimTick};

fn b(f: f32) -> String {
    format!("{:08x}", f.to_bits())
}

/// A string as its length and FNV-1a, so prose never has to survive two
/// languages' escaping rules to be compared.
fn th(s: &str) -> String {
    let mut h = Fnv1a::new();
    h.write_bytes(s.as_bytes());
    format!("{}:{:016x}", s.len(), h.finish())
}

fn list_f(v: &[f32]) -> String {
    v.iter().map(|f| b(*f)).collect::<Vec<_>>().join(",")
}

fn attack(a: &Option<AttackDef>) -> String {
    match a {
        None => "none".into(),
        Some(a) => format!(
            "{},{},{},{},{},{}",
            a.kind as u8,
            b(a.damage),
            b(a.cooldown),
            b(a.range),
            b(a.acquire),
            a.ranged as u8
        ),
    }
}

// ---------------------------------------------------------------------------
// catalogs
// ---------------------------------------------------------------------------

fn dump_catalogs() {
    let units = data::load_unit_catalog();
    for (i, d) in units.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(units.id(&lower) as usize, i);
        let requires = match d.requires {
            None => "none".to_string(),
            Some((aff, tier)) => format!("{},{}", aff as u8, tier),
        };
        let passive = match d.passive {
            None => "none".to_string(),
            Some(UnitPassive::FloraRegen { rate, radius }) => format!("flora,{},{}", b(rate), b(radius)),
            Some(UnitPassive::Taunt { radius }) => format!("taunt,{}", b(radius)),
            Some(UnitPassive::ChainArc { jumps, radius, falloff }) => {
                format!("chain,{},{},{}", jumps, b(radius), b(falloff))
            }
        };
        println!(
            "unit {i} \"{lower}\" worker={} will={} hp={} speed={} radius={} armor={} attack={} color={},{},{} size={},{} cost={},{} build={} requires={} xp={} passive={}",
            d.worker as u8,
            d.will,
            b(d.hp),
            b(d.speed),
            b(d.radius),
            d.armor as u8,
            attack(&d.attack),
            b(d.color.0),
            b(d.color.1),
            b(d.color.2),
            b(d.size.0),
            b(d.size.1),
            b(d.cost_essence),
            b(d.cost_anima),
            b(d.build_time),
            requires,
            b(d.xp_value),
            passive
        );
    }

    let buildings = data::load_building_catalog();
    for (i, d) in buildings.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(buildings.id(&lower) as usize, i);
        println!(
            "building {i} \"{lower}\" hp={} armor={} cost={} build={} will={} footprint={},{} size={},{} color={},{},{} produces={} researches={} attack={}",
            b(d.hp),
            d.armor as u8,
            b(d.cost_essence),
            b(d.build_time),
            d.will_provided,
            d.footprint.0,
            d.footprint.1,
            b(d.size.0),
            b(d.size.1),
            b(d.color.0),
            b(d.color.1),
            b(d.color.2),
            d.produces.join("|"),
            d.researches.join("|"),
            attack(&d.attack)
        );
    }

    let sources = data::load_source_catalog();
    for (i, d) in sources.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(sources.id(&lower) as usize, i);
        println!(
            "source {i} \"{lower}\" affinity={} essence={} anima={} footprint={},{} size={},{}",
            d.affinity as u8,
            b(d.essence),
            b(d.anima),
            d.footprint.0,
            d.footprint.1,
            b(d.size.0),
            b(d.size.1)
        );
    }

    let hero = data::load_hero_config();
    println!(
        "hero unit=\"{}\" hp_per_level={} damage_per_level={} regen_base={} regen_per_level={} xp_curve={} xp_radius={} abilities={} revive={},{},{}",
        hero.unit,
        b(hero.hp_per_level),
        b(hero.damage_per_level),
        b(hero.regen_base),
        b(hero.regen_per_level),
        list_f(&hero.xp_curve),
        b(hero.xp_radius),
        hero.abilities.join("|"),
        b(hero.revive_cost_base),
        b(hero.revive_cost_per_level),
        b(hero.revive_time)
    );

    let abilities = data::load_ability_catalog();
    for (i, d) in abilities.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(abilities.id(&lower) as usize, i);
        let kind = match d.kind {
            AbilityKind::TargetEnemyOrSource => 0,
            AbilityKind::TargetFriendlyUnit => 1,
            AbilityKind::NoTarget => 2,
        };
        let gates = d.max_level_gate.iter().map(|g| g.to_string()).collect::<Vec<_>>().join(",");
        println!(
            "ability {i} \"{lower}\" description={} kind={kind} cast_range={} gates={gates}",
            th(&d.description),
            b(d.cast_range)
        );
        for (r, k) in d.ranks.iter().enumerate() {
            println!(
                "  rank {r} cooldown={} damage={} refund={} drain={} heal_frac={} atk_speed={} move_speed={} duration={} burn={} radius={} slow={} hp_bonus={} dmg_bonus={} thralls={}",
                b(k.cooldown),
                b(k.damage),
                b(k.refund),
                b(k.drain),
                b(k.heal_frac),
                b(k.atk_speed),
                b(k.move_speed),
                b(k.duration),
                b(k.burn),
                b(k.radius),
                b(k.slow),
                b(k.hp_bonus),
                b(k.dmg_bonus),
                k.thralls
            );
        }
    }

    let items = data::load_item_catalog();
    for (i, d) in items.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(items.id(&lower) as usize, i);
        let active = match d.active {
            None => "none".to_string(),
            Some(ActiveEffect::Heal { amount }) => format!("heal,{}", b(amount)),
            Some(ActiveEffect::Haste { atk_speed, move_speed, secs }) => {
                format!("haste,{},{},{}", b(atk_speed), b(move_speed), b(secs))
            }
        };
        println!(
            "item {i} \"{lower}\" hp_add={} damage_add={} regen_add={} move_mult_add={} active={active}",
            b(d.hp_add),
            b(d.damage_add),
            b(d.regen_add),
            b(d.move_mult_add)
        );
    }

    let upgrades = data::load_upgrade_catalog();
    for (i, d) in upgrades.defs.iter().enumerate() {
        let lower = d.name.to_lowercase();
        assert_eq!(upgrades.id(&lower) as usize, i);
        let effect = match d.effect {
            UpgradeEffect::AttackSpeed { pct_per_level } => format!("attack_speed,{}", b(pct_per_level)),
            UpgradeEffect::GatherRate { pct_per_level } => format!("gather_rate,{}", b(pct_per_level)),
        };
        println!(
            "upgrade {i} \"{lower}\" max_level={} cost={},{} time={} effect={effect}",
            d.max_level,
            b(d.cost_essence),
            b(d.cost_anima),
            b(d.time)
        );
    }

    let matrix = data::load_damage_matrix();
    let attacks = [AttackType::Normal, AttackType::Pierce, AttackType::Siege, AttackType::Arcane];
    let armors = [ArmorType::Unarmored, ArmorType::Light, ArmorType::Heavy, ArmorType::Fortified];
    for (a, at) in attacks.iter().enumerate() {
        let row = armors.iter().map(|ar| b(matrix.multiplier(*at, *ar))).collect::<Vec<_>>().join(",");
        println!("matrix {a} {row}");
    }

    let loot = data::load_loot_table(&units, &items);
    for kind in 0..units.defs.len() {
        let row = loot
            .drops_for(kind as u16)
            .iter()
            .map(|(item, chance)| format!("{item}:{}", b(*chance)))
            .collect::<Vec<_>>()
            .join(",");
        println!("loot {kind} {row}");
    }

    let payoff = data::load_affinity_payoff();
    println!(
        "affinity hp_frac_per_tier={} damage_frac_per_tier={}",
        b(payoff.hp_frac_per_tier),
        b(payoff.damage_frac_per_tier)
    );

    let table = difficulty::load_difficulty_table();
    for (name, m) in [("story", table.story), ("normal", table.normal), ("hard", table.hard)] {
        println!(
            "difficulty {name} enemy_hp={} enemy_damage={} enemy_count={}",
            b(m.enemy_hp),
            b(m.enemy_damage),
            b(m.enemy_count)
        );
    }

    for name in ["first_light", "heartwood"] {
        dump_mission(name);
    }
}

fn rects(v: &[(f32, f32, f32, f32)]) -> String {
    v.iter()
        .map(|r| format!("{},{},{},{}", b(r.0), b(r.1), b(r.2), b(r.3)))
        .collect::<Vec<_>>()
        .join(";")
}

fn dump_scene(tag: &str, steps: &[SceneStep]) {
    for (i, s) in steps.iter().enumerate() {
        let camera = match s.camera {
            None => "none".to_string(),
            Some((x, y, z)) => format!("{},{},{}", b(x), b(y), b(z)),
        };
        let actors = s
            .actors
            .iter()
            .map(|a| format!("{},{}", b(a.0), b(a.1)))
            .collect::<Vec<_>>()
            .join(";");
        println!(
            "  {tag} {i} at={} subtitle={} speaker={} camera={camera} actors={actors} storm={} flash={}",
            b(s.at),
            th(&s.subtitle),
            th(&s.speaker),
            b(s.storm),
            b(s.flash)
        );
    }
}

fn dump_mission(name: &str) {
    let def = mission::try_load_mission_def(name).unwrap_or_else(|e| panic!("{e}"));
    println!(
        "mission {name} name={} briefing={} night={} rotate180={} day_cycle={} rainy={} biome={}",
        th(&def.name),
        th(&def.briefing),
        def.night as u8,
        def.rotate180 as u8,
        def.day_cycle as u8,
        def.rainy as u8,
        th(&def.biome)
    );
    match &def.map {
        None => println!("  map none"),
        Some(m) => {
            println!("  map half={}", b(m.half));
            println!("  obstacles {}", rects(&m.obstacles));
            println!("  roads {}", rects(&m.roads));
            let plateaus = m
                .plateaus
                .iter()
                .map(|p| format!("{},{},{},{},{}", b(p.0), b(p.1), b(p.2), b(p.3), p.4))
                .collect::<Vec<_>>()
                .join(";");
            println!("  plateaus {plateaus}");
            for (verts, level) in &m.plateau_polys {
                let v = verts.iter().map(|v| format!("{},{}", b(v.0), b(v.1))).collect::<Vec<_>>().join(";");
                println!("  poly {level} {v}");
            }
            println!("  ramps {}", rects(&m.ramps));
            println!("  bumps {}", rects(&m.bumps));
        }
    }
    match &def.spawns {
        None => println!("  spawns none"),
        Some(s) => {
            let hero = match s.hero {
                None => "none".to_string(),
                Some((x, y)) => format!("{},{}", b(x), b(y)),
            };
            println!("  spawns hero={hero} start_essence={}", b(s.start_essence));
            for (n, x, y) in &s.buildings {
                println!("  building \"{n}\" {},{}", b(*x), b(*y));
            }
            for (n, c, x, y) in &s.units {
                println!("  unit \"{n}\" {c} {},{}", b(*x), b(*y));
            }
            for (n, c, x, y) in &s.enemies {
                println!("  enemy \"{n}\" {c} {},{}", b(*x), b(*y));
            }
            for (n, x, y) in &s.sources {
                println!("  source \"{n}\" {},{}", b(*x), b(*y));
            }
            for (n, x, y) in &s.items {
                println!("  item \"{n}\" {},{}", b(*x), b(*y));
            }
        }
    }
    for o in &def.objectives {
        println!("  objective \"{}\" text={} optional={}", o.name, th(&o.text), o.optional as u8);
    }
    for t in &def.triggers {
        println!("  trigger \"{}\" once={}", t.name, t.once as u8);
        for c in &t.when {
            println!("    when {}", cond(c));
        }
        for a in &t.actions {
            println!("    do {}", action(a));
        }
    }
    dump_scene("intro", &def.intro);
    dump_scene("outro", &def.outro);
}

fn cond(c: &TriggerCond) -> String {
    match c {
        TriggerCond::TimeAtLeast(v) => format!("time_at_least {}", b(*v)),
        TriggerCond::TotalExtractedAtLeast(v) => format!("total_extracted_at_least {}", b(*v)),
        TriggerCond::EssenceAtLeast(v) => format!("essence_at_least {}", b(*v)),
        TriggerCond::AnimaAtLeast(v) => format!("anima_at_least {}", b(*v)),
        TriggerCond::HeroLevelAtLeast(v) => format!("hero_level_at_least {v}"),
        TriggerCond::HeroRankAtLeast { slot, rank } => format!("hero_rank_at_least {slot} {rank}"),
        TriggerCond::HeroInArea { x, y, radius } => {
            format!("hero_in_area {},{},{}", b(*x), b(*y), b(*radius))
        }
        TriggerCond::PlayerUnitsOfAtLeast { unit, count } => {
            format!("player_units_of_at_least \"{unit}\" {count}")
        }
        TriggerCond::PlayerBuildingsOfAtLeast { building, count } => {
            format!("player_buildings_of_at_least \"{building}\" {count}")
        }
        TriggerCond::ArmyWillAtLeast(v) => format!("army_will_at_least {v}"),
        TriggerCond::EnemyUnitsAtMost(v) => format!("enemy_units_at_most {v}"),
        TriggerCond::PlayerWiped => "player_wiped".into(),
        TriggerCond::ItemsOnGroundAtMost(v) => format!("items_on_ground_at_most {v}"),
        TriggerCond::ObjectiveComplete(n) => format!("objective_complete \"{n}\""),
        TriggerCond::ObjectiveActive(n) => format!("objective_active \"{n}\""),
        TriggerCond::ObjectiveActiveForAtLeast(n, s) => {
            format!("objective_active_for_at_least \"{n}\" {}", b(*s))
        }
        TriggerCond::AllRequiredObjectivesComplete => "all_required_objectives_complete".into(),
    }
}

fn action(a: &TriggerAction) -> String {
    match a {
        TriggerAction::Message(t) => format!("message {}", th(t)),
        TriggerAction::Say { speaker, text } => format!("say \"{speaker}\" {}", th(text)),
        TriggerAction::SpawnSquad { unit, count, x, y, aggressive } => format!(
            "spawn_squad \"{unit}\" {count} {},{} {}",
            b(*x),
            b(*y),
            *aggressive as u8
        ),
        TriggerAction::SpawnPlayerSquad { unit, count, x, y } => {
            format!("spawn_player_squad \"{unit}\" {count} {},{}", b(*x), b(*y))
        }
        TriggerAction::SpawnPlayerBuilding { building, x, y } => {
            format!("spawn_player_building \"{building}\" {},{}", b(*x), b(*y))
        }
        TriggerAction::SpawnItem { item, x, y } => format!("spawn_item \"{item}\" {},{}", b(*x), b(*y)),
        TriggerAction::AddObjective { name, text, optional } => {
            format!("add_objective \"{name}\" {} {}", th(text), *optional as u8)
        }
        TriggerAction::CompleteObjective(n) => format!("complete_objective \"{n}\""),
        TriggerAction::FailObjective(n) => format!("fail_objective \"{n}\""),
        TriggerAction::GrantEssence(v) => format!("grant_essence {}", b(*v)),
        TriggerAction::GrantAnima(v) => format!("grant_anima {}", b(*v)),
        TriggerAction::GrantHeroXp(v) => format!("grant_hero_xp {}", b(*v)),
        TriggerAction::Victory => "victory".into(),
        TriggerAction::Defeat => "defeat".into(),
    }
}

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

/// The glam and f32 operations the sim actually calls, on inputs drawn from the
/// sim's own PCG32 - so this also pins `range_f32`. The shapes mirror real call
/// sites: the chase velocity `(t - p) / d * speed`, the separation clamp, the
/// map-edge clamp, the spatial-hash floor, the grid-cell truncation.
fn dump_primitives() {
    for seed in [0u64, 7, 11, 0x4855_534B, u64::MAX] {
        let mut r = SimRng::new(seed).0;
        let raw = (0..8).map(|_| format!("{:08x}", r.next_u32())).collect::<Vec<_>>().join(",");
        let (s, i) = r.save_state();
        println!("rng {seed} {raw} state={s:016x},{i:016x}");
    }

    let mut r = Pcg32::new(0x5eed, 0xda3e_39cb_94b9_5bdb);
    for i in 0..4000 {
        let a = Vec2::new(r.range_f32(-60.0, 60.0), r.range_f32(-60.0, 60.0));
        // every eighth pair is close, so the near-zero branches get exercised
        let b2 = if i % 8 == 0 {
            a + Vec2::new(r.range_f32(-0.001, 0.001), r.range_f32(-0.001, 0.001))
        } else {
            Vec2::new(r.range_f32(-60.0, 60.0), r.range_f32(-60.0, 60.0))
        };
        let k = r.range_f32(0.0, 10.0);
        let len = a.length();
        let d = a.distance(b2);
        let n = if len > 1e-4 { a.normalize() } else { Vec2::ZERO };
        let c = a.clamp_length_max(k);
        let cl = a.clamp(Vec2::splat(-50.0 + k), Vec2::splat(50.0 - k));
        let chase = if d > 1e-4 { (b2 - a) / d * k } else { Vec2::ZERO };
        let step = a + chase * SIM_DT;
        println!(
            "p {i} len={} lsq={} dist={} dsq={} dot={} norm={},{} clampmax={},{} clamp={},{} chase={},{} step={},{} floor={} trunc={} round={} ceil={} u32={} sqrt={}",
            b(len),
            b(a.length_squared()),
            b(d),
            b(a.distance_squared(b2)),
            b(a.dot(b2)),
            b(n.x),
            b(n.y),
            b(c.x),
            b(c.y),
            b(cl.x),
            b(cl.y),
            b(chase.x),
            b(chase.y),
            b(step.x),
            b(step.y),
            (a.x / 2.0).floor() as i32,
            a.y as i32,
            a.x.round() as i32,
            (a.y * 0.5).ceil() as i32,
            a.x as u32,
            b((k * 7.0).sqrt())
        );
    }
}

// ---------------------------------------------------------------------------
// scenarios
// ---------------------------------------------------------------------------

/// A scenario file: a handful of setup lines, then `<tick> <order> key=value...`
/// lines that are pushed onto the OrderQueue before that tick runs - the same
/// shape as `tests/determinism.rs`'s `script()`. The C++ suites read the same
/// files, so the orders have one source of truth.
struct Scenario {
    seed: u64,
    ticks: u64,
    difficulty: Difficulty,
    setup: Vec<Vec<String>>,
    script: Vec<(u64, Vec<String>)>,
}

fn parse_scenario(path: &str) -> Scenario {
    let text = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("{path}: {e}"));
    let mut s = Scenario {
        seed: project_husk::sim::DEFAULT_SEED,
        ticks: 1000,
        difficulty: Difficulty::Normal,
        setup: Vec::new(),
        script: Vec::new(),
    };
    for line in text.lines() {
        let line = line.split('#').next().unwrap().trim();
        if line.is_empty() {
            continue;
        }
        let words: Vec<String> = line.split_whitespace().map(|w| w.to_string()).collect();
        match words[0].as_str() {
            "seed" => s.seed = words[1].parse().unwrap(),
            "ticks" => s.ticks = words[1].parse().unwrap(),
            "difficulty" => {
                s.difficulty = match words[1].as_str() {
                    "story" => Difficulty::Story,
                    "normal" => Difficulty::Normal,
                    "hard" => Difficulty::Hard,
                    other => panic!("unknown difficulty {other}"),
                }
            }
            w if w.chars().all(|c| c.is_ascii_digit()) => {
                s.script.push((w.parse().unwrap(), words[1..].to_vec()));
            }
            _ => s.setup.push(words),
        }
    }
    s
}

fn kv(words: &[String]) -> HashMap<String, String> {
    words
        .iter()
        .filter_map(|w| w.split_once('='))
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect()
}

fn name(v: &str) -> String {
    v.replace('_', " ")
}

fn ids(v: &str) -> Vec<u32> {
    let mut out = Vec::new();
    for part in v.split(',') {
        if let Some((lo, hi)) = part.split_once('-') {
            let (lo, hi): (u32, u32) = (lo.parse().unwrap(), hi.parse().unwrap());
            out.extend(lo..=hi);
        } else {
            out.push(part.parse().unwrap());
        }
    }
    out
}

fn f(m: &HashMap<String, String>, k: &str) -> f32 {
    m[k].parse().unwrap_or_else(|_| panic!("bad float {k}={}", m[k]))
}

fn u(m: &HashMap<String, String>, k: &str) -> u32 {
    m[k].parse().unwrap_or_else(|_| panic!("bad int {k}={}", m[k]))
}

fn flag(m: &HashMap<String, String>, k: &str) -> bool {
    m.get(k).is_some_and(|v| v == "1")
}

fn order_msg(world: &World, words: &[String]) -> OrderMsg {
    let m = kv(&words[1..]);
    let units = || ids(&m["units"]);
    match words[0].as_str() {
        "point" => OrderMsg::Point {
            units: units(),
            attack: flag(&m, "attack"),
            target: Vec2::new(f(&m, "x"), f(&m, "y")),
            queued: flag(&m, "queued"),
        },
        "attack" => OrderMsg::Attack { units: units(), target: u(&m, "target"), queued: flag(&m, "queued") },
        "patrol" => OrderMsg::Patrol { units: units(), target: Vec2::new(f(&m, "x"), f(&m, "y")) },
        "hold" => OrderMsg::Hold { units: units() },
        "stop" => OrderMsg::Stop { units: units() },
        "extract" => OrderMsg::Extract { units: units(), source: u(&m, "source"), queued: flag(&m, "queued") },
        "repair" => OrderMsg::Repair { units: units(), target: u(&m, "target"), queued: flag(&m, "queued") },
        "build" => OrderMsg::Build { units: units(), site: u(&m, "site"), queued: flag(&m, "queued") },
        "place" => OrderMsg::PlaceBuilding {
            builder: u(&m, "builder"),
            kind: world.resource::<data::BuildingCatalog>().id(&name(&m["kind"])),
            center: Vec2::new(f(&m, "x"), f(&m, "y")),
        },
        "produce" => OrderMsg::Produce {
            building: u(&m, "building"),
            kind: world.resource::<data::UnitCatalog>().id(&name(&m["kind"])),
        },
        "cancel" => OrderMsg::CancelProduce { building: u(&m, "building") },
        "rally" => OrderMsg::SetRally { building: u(&m, "building"), target: Vec2::new(f(&m, "x"), f(&m, "y")) },
        "cast" => {
            let t = &m["target"];
            let target = if t == "none" {
                CastTarget::None_
            } else if let Some(id) = t.strip_prefix("id:") {
                CastTarget::Id(id.parse().unwrap())
            } else if let Some(p) = t.strip_prefix("point:") {
                let (x, y) = p.split_once(',').unwrap();
                CastTarget::Point(Vec2::new(x.parse().unwrap(), y.parse().unwrap()))
            } else {
                panic!("bad cast target {t}")
            };
            OrderMsg::Cast { hero: u(&m, "hero"), ability: u(&m, "ability") as u8, target, queued: flag(&m, "queued") }
        }
        "learn" => OrderMsg::Learn { hero: u(&m, "hero"), ability: u(&m, "ability") as u8 },
        "revive" => OrderMsg::Revive,
        "pickup" => OrderMsg::Pickup { hero: u(&m, "hero"), item: u(&m, "item"), queued: flag(&m, "queued") },
        "drop" => OrderMsg::DropItem { hero: u(&m, "hero"), slot: u(&m, "slot") as u8 },
        "use" => OrderMsg::UseItem { hero: u(&m, "hero"), slot: u(&m, "slot") as u8 },
        "research" => OrderMsg::Research {
            building: u(&m, "building"),
            upgrade: world.resource::<data::UpgradeCatalog>().id(&name(&m["upgrade"])),
        },
        other => panic!("unknown order {other}"),
    }
}

fn setup(world: &mut World, words: &[String]) {
    match words[0].as_str() {
        "setup" => match words[1].as_str() {
            "m0" => project_husk::sim::unit::spawn_m0_scenario(world),
            "macro" => project_husk::sim::scenario::spawn_m2_macro_scenario(world),
            other => panic!("unknown setup {other}"),
        },
        "mission" => mission::load_mission(world, &words[1]).unwrap_or_else(|e| panic!("{e}")),
        "squad" => {
            let m = kv(&words[1..]);
            let comp: Vec<(u16, u32)> = m["comp"]
                .split(',')
                .map(|c| {
                    let (n, k) = c.split_once(':').unwrap();
                    (world.resource::<data::UnitCatalog>().id(&name(n)), k.parse().unwrap())
                })
                .collect();
            spawn_squad(world, u(&m, "team") as u8, Vec2::new(f(&m, "x"), f(&m, "y")), &comp);
        }
        other => panic!("unknown setup line {other}"),
    }
}

fn write_order(h: &mut Fnv1a, order: &Order) {
    match order {
        Order::Point { attack, goal_cell, .. } => {
            h.write_u32(1 + *attack as u32);
            h.write_u32(*goal_cell);
        }
        Order::Attack { target } => {
            h.write_u32(3);
            h.write_u32(*target);
        }
        Order::Patrol { goal_cell, .. } => {
            h.write_u32(4);
            h.write_u32(*goal_cell);
        }
        Order::Hold => h.write_u32(5),
        Order::Extract { source, .. } => {
            h.write_u32(6);
            h.write_u32(*source);
        }
        Order::Repair { target, .. } => {
            h.write_u32(7);
            h.write_u32(*target);
        }
        Order::Build { site, .. } => {
            h.write_u32(8);
            h.write_u32(*site);
        }
        Order::Cast { ability, target, .. } => {
            h.write_u32(9);
            h.write_u32(*ability as u32);
            match target {
                CastTarget::Id(id) => h.write_u32(*id),
                CastTarget::Point(p) => {
                    h.write_f32(p.x);
                    h.write_f32(p.y);
                }
                CastTarget::None_ => h.write_u32(u32::MAX - 1),
            }
        }
        Order::Pickup { item, .. } => {
            h.write_u32(10);
            h.write_u32(*item);
        }
    }
}

/// `update_sim_hash`, split at its domain boundaries. Each sub-hash starts from
/// a fresh FNV offset, so a divergence names its domain. The C++ side computes
/// the same five and the full hash separately, so these are a diagnostic and
/// never a substitute for the game's own number.
fn domain_hashes(world: &mut World) -> [u64; 5] {
    let mut h = Fnv1a::new();
    h.write_u64(world.resource::<SimTick>().0);
    let economy = *world.resource::<PlayerEconomy>();
    h.write_f32(economy.essence);
    h.write_f32(economy.anima);
    for c in world.resource::<AffinityState>().cumulative {
        h.write_f32(c);
    }
    let will = *world.resource::<WillState>();
    h.write_u32(will.used);
    h.write_u32(will.cap);
    let research = world.resource::<ResearchState>().clone();
    h.write_u32(research.levels.len() as u32);
    for lvl in &research.levels {
        h.write_u32(*lvl as u32);
    }
    match research.in_progress {
        None => h.write_u32(0),
        Some(r) => {
            h.write_u32(1);
            h.write_u32(r.upgrade as u32);
            h.write_u64(r.ready_tick);
        }
    }
    let (hi, lo) = world.resource::<SimRng>().0.save_state();
    h.write_u64(hi);
    h.write_u64(lo);
    h.write_u32(world.resource::<SimIdAlloc>().0);
    h.write_u32(*world.resource::<Difficulty>() as u32);
    match &world.resource::<MissionState>().0 {
        None => h.write_u32(0),
        Some(rt) => {
            h.write_u32(1);
            h.write_u64(rt.start_tick);
            h.write_u32(match rt.outcome {
                MissionOutcome::Playing => 0,
                MissionOutcome::Victory => 1,
                MissionOutcome::Defeat => 2,
            });
            h.write_u64(rt.fired.len() as u64);
            for &fired in &rt.fired {
                h.write_u32(fired as u32);
            }
            h.write_u64(rt.objectives.len() as u64);
            for o in &rt.objectives {
                h.write_bytes(o.name.as_bytes());
                h.write_u32(match o.status {
                    ObjectiveStatus::Active => 0,
                    ObjectiveStatus::Complete => 1,
                    ObjectiveStatus::Failed => 2,
                });
                h.write_u64(o.since_tick);
            }
        }
    }
    match world.resource::<HeroState>() {
        HeroState::Absent => h.write_u32(0),
        HeroState::Alive(id) => {
            h.write_u32(1);
            h.write_u32(*id);
        }
        HeroState::Dead { saved, died_tick } => {
            h.write_u32(2);
            h.write_u32(saved.level as u32);
            h.write_f32(saved.xp);
            h.write_u64(*died_tick);
        }
        HeroState::Reviving { saved, ready_tick } => {
            h.write_u32(3);
            h.write_u32(saved.level as u32);
            h.write_u64(*ready_tick);
        }
    }
    let state = h.finish();

    let mut h = Fnv1a::new();
    let mut q = world.query::<(
        &SimId,
        &SimPos,
        &Team,
        &UnitKind,
        &Health,
        &CombatState,
        &Orders,
        &Arrived,
        (Option<&Hero>, Option<&OverchargeBuff>, Option<&SlowDebuff>, Option<&AvatarBuff>, Option<&TimedLife>),
    )>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, pos, team, kind, hp, combat, orders, arrived, (hero, overcharge, slow, avatar, timed)) in rows {
        h.write_u32(id.0);
        h.write_f32(pos.cur.x);
        h.write_f32(pos.cur.y);
        h.write_u32(team.0 as u32);
        h.write_u32(kind.0 as u32);
        h.write_f32(hp.cur);
        h.write_f32(combat.cooldown);
        h.write_u32(combat.target.unwrap_or(u32::MAX));
        h.write_f32(combat.home.x);
        h.write_f32(combat.home.y);
        h.write_u64(orders.0.len() as u64);
        for order in &orders.0 {
            write_order(&mut h, order);
        }
        h.write_u32(arrived.0.unwrap_or(u32::MAX));
        if let Some(hero) = hero {
            h.write_u32(0xBEEF);
            h.write_u32(hero.level as u32);
            h.write_f32(hero.xp);
            h.write_u32(hero.points as u32);
            for rank in hero.ranks {
                h.write_u32(rank as u32);
            }
            for cd in hero.cooldowns {
                h.write_f32(cd);
            }
            for slot in hero.inventory {
                h.write_u32(slot.map(|k| k as u32).unwrap_or(u32::MAX));
            }
        }
        if let Some(buff) = overcharge {
            h.write_u32(0xC1);
            h.write_u64(buff.until_tick);
            h.write_f32(buff.burn);
        }
        if let Some(debuff) = slow {
            h.write_u32(0xC2);
            h.write_u64(debuff.until_tick);
            h.write_f32(debuff.slow);
        }
        if let Some(buff) = avatar {
            h.write_u32(0xC3);
            h.write_u64(buff.until_tick);
        }
        if let Some(life) = timed {
            h.write_u32(0xC4);
            h.write_u64(life.until_tick);
        }
    }
    let units = h.finish();

    let mut h = Fnv1a::new();
    let mut q = world.query::<(&SimId, &Team, &Health, &project_husk::sim::building::Building)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, team, hp, building) in rows {
        h.write_u32(id.0);
        h.write_u32(team.0 as u32);
        h.write_u32(building.kind as u32);
        h.write_f32(hp.cur);
        h.write_f32(building.progress);
        h.write_f32(building.queue_progress);
        h.write_f32(building.attack_cooldown);
        h.write_u64(building.queue.len() as u64);
        for &k in &building.queue {
            h.write_u32(k as u32);
        }
        match building.rally {
            Some(r) => {
                h.write_f32(r.x);
                h.write_f32(r.y);
            }
            None => h.write_u32(u32::MAX),
        }
    }
    let buildings = h.finish();

    let mut h = Fnv1a::new();
    let mut q = world.query::<(&SimId, &EssenceSource)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, src) in rows {
        h.write_u32(id.0);
        h.write_f32(src.essence);
        h.write_f32(src.anima);
        h.write_u32(src.animated as u32);
    }
    let sources = h.finish();

    let mut h = Fnv1a::new();
    let mut q = world.query::<(&SimId, &ItemPickup)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, item) in rows {
        h.write_u32(id.0);
        h.write_u32(item.kind as u32);
    }
    let items = h.finish();

    [state, units, buildings, sources, items]
}

fn order_text(o: &Order) -> String {
    match o {
        Order::Point { attack, goal_cell, target } => {
            format!("point:{}:{goal_cell}:{},{}", *attack as u8, b(target.x), b(target.y))
        }
        Order::Attack { target } => format!("attack:{target}"),
        Order::Patrol { goal_cell, target, home } => {
            format!("patrol:{goal_cell}:{},{}:{},{}", b(target.x), b(target.y), b(home.x), b(home.y))
        }
        Order::Hold => "hold".into(),
        Order::Extract { source, goal_cell } => format!("extract:{source}:{goal_cell}"),
        Order::Repair { target, goal_cell } => format!("repair:{target}:{goal_cell}"),
        Order::Build { site, goal_cell } => format!("build:{site}:{goal_cell}"),
        Order::Cast { ability, target, goal_cell } => {
            let t = match target {
                CastTarget::Id(id) => format!("id{id}"),
                CastTarget::Point(p) => format!("pt{},{}", b(p.x), b(p.y)),
                CastTarget::None_ => "none".into(),
            };
            format!("cast:{ability}:{t}:{}", goal_cell.map(|g| g as i64).unwrap_or(-1))
        }
        Order::Pickup { item, goal_cell } => format!("pickup:{item}:{goal_cell}"),
    }
}

/// Every unit's full hashed state plus the fields the hash leaves out (`prev`,
/// `hp.max`), one line each, in SimId order.
fn dump_entities(world: &mut World) {
    let tick = world.resource::<SimTick>().0;
    let mut q = world.query::<(&SimId, &SimPos, &Team, &UnitKind, &Health, &CombatState, &Orders, &Arrived)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, pos, team, kind, hp, combat, orders, arrived) in rows {
        let orders = orders.0.iter().map(order_text).collect::<Vec<_>>().join("|");
        println!(
            "  @{tick} unit {} team={} kind={} pos={},{} prev={},{} hp={}/{} cd={} target={} home={},{} arrived={} orders={orders}",
            id.0,
            team.0,
            kind.0,
            b(pos.cur.x),
            b(pos.cur.y),
            b(pos.prev.x),
            b(pos.prev.y),
            b(hp.cur),
            b(hp.max),
            b(combat.cooldown),
            combat.target.map(|t| t as i64).unwrap_or(-1),
            b(combat.home.x),
            b(combat.home.y),
            arrived.0.map(|a| a as i64).unwrap_or(-1)
        );
    }
    let mut q = world.query::<(&SimId, &Health, &project_husk::sim::building::Building)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, hp, bld) in rows {
        println!(
            "  @{tick} building {} kind={} hp={}/{} progress={} queue={:?} qprog={} cd={}",
            id.0,
            bld.kind,
            b(hp.cur),
            b(hp.max),
            b(bld.progress),
            bld.queue,
            b(bld.queue_progress),
            b(bld.attack_cooldown)
        );
    }
    let mut q = world.query::<(&SimId, &EssenceSource)>();
    let mut rows: Vec<_> = q.iter(world).collect();
    rows.sort_unstable_by_key(|(id, ..)| id.0);
    for (id, src) in rows {
        println!(
            "  @{tick} source {} essence={} anima={} animated={}",
            id.0,
            b(src.essence),
            b(src.anima),
            src.animated as u8
        );
    }
}

fn run(path: &str, domains: bool, entities_every: u64) {
    let scenario = parse_scenario(path);
    let mut app = App::new();
    app.add_plugins(SimPlugin { seed: scenario.seed });
    let world = app.world_mut();
    world.insert_resource(scenario.difficulty);
    for line in &scenario.setup {
        setup(world, line);
    }
    let script: Vec<(u64, OrderMsg)> = scenario
        .script
        .iter()
        .map(|(at, words)| (*at, order_msg(world, words)))
        .collect();
    for tick in 0..scenario.ticks {
        for (at, msg) in &script {
            if *at == tick {
                world.resource_mut::<OrderQueue>().0.push(msg.clone());
            }
        }
        world.run_schedule(SimStep);
        let t = world.resource::<SimTick>().0;
        let hash = world.resource::<SimHash>().0;
        if domains {
            let d = domain_hashes(world);
            println!("{t} {hash:016x} {:016x} {:016x} {:016x} {:016x} {:016x}", d[0], d[1], d[2], d[3], d[4]);
        } else {
            println!("{t} {hash:016x}");
        }
        if entities_every > 0 && t % entities_every == 0 {
            dump_entities(world);
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("catalogs") => dump_catalogs(),
        Some("primitives") => dump_primitives(),
        Some("run") => {
            let path = args.get(2).expect("run <file.scn>");
            let domains = args.iter().any(|a| a == "--domains");
            let entities = args
                .iter()
                .position(|a| a == "--entities")
                .map(|i| args[i + 1].parse().unwrap())
                .unwrap_or(0);
            run(path, domains, entities);
        }
        _ => {
            eprintln!("usage: husk-oracle catalogs | primitives | run <file.scn> [--domains] [--entities N]");
            std::process::exit(2);
        }
    }
}
