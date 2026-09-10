"""Write the scenario files that mirror HUSK's own Rust tests.

Each .scn here reproduces the SETUP of one or more tests in Test-Game/tests/
(ai.rs, balance.rs, depth_stack.rs, economy.rs, hero.rs, loot.rs, m5.rs,
mission.rs, research.rs, terrain.rs, tower.rs) in the scenario language
games/husk/sim/Scenario.hpp documents. The oracle then records the Rust sim's
hash after every tick, and test_husk_scenarios holds the port to it - so each
test's situation becomes a bit-exact comparison.

The .scn files are the source of truth once written; this script is how they
were made, kept so the set can be regenerated or extended in one place.
Positions are written as decimal text, parsed identically by both sides, so
they only need to reproduce each test's situation, not its exact floats.

    python make_scenarios.py      (from games/husk/golden)
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))


def fx(v):
    s = f"{v:.4f}".rstrip("0")
    return s + "0" if s.endswith(".") else s


def unit(kind, team, x, y):
    return f"unit {kind} {team} {fx(x)} {fx(y)}"


def write(name, comment, lines):
    with open(os.path.join(HERE, name + ".scn"), "w", newline="\n") as f:
        for c in comment.strip().splitlines():
            f.write("# " + c.strip() + "\n")
        for line in lines:
            f.write(line + "\n")


def mission(name, text):
    os.makedirs(os.path.join(HERE, "missions"), exist_ok=True)
    with open(os.path.join(HERE, "missions", name + ".ron"), "w", newline="\n") as f:
        f.write(text.strip() + "\n")


# ---- ai.rs -------------------------------------------------------------------

for name, flee_y, ticks in [("ai_leash", 58.0, 601), ("ai_chase", 18.0, 400)]:
    write(name, f"""
        ai.rs: an idle golem acquires a stalker, which flees to y={flee_y:g} -
        {'past the leash, so the guard gives up and walks home' if flee_y > 30 else 'inside the leash, so the chase goes on'}.
        """, [
        "seed 3", f"ticks {ticks}", "map 60",
        unit("golem", 1, 0, 0), unit("stalker", 0, 0, 8),
        f"1 point units=1 attack=0 x=0.0 y={fx(flee_y)} queued=0",
    ])

write("ai_attack_fx", "ai.rs attack_fx_names_its_victim: a golem closes on a stalker.", [
    "seed 3", "ticks 300", "map 60", unit("golem", 1, 0, 0), unit("stalker", 0, 0, 1),
])

for name, golem_y in [("ai_taunt", 5.0), ("ai_taunt_radius", 10.0)]:
    write(name, f"ai.rs: a rifleman, a nearer stalker, and a golem at y={golem_y:g} - "
                f"{'inside' if golem_y < 8 else 'outside'} its taunt radius.", [
        "seed 4", "ticks 300", "map 60",
        unit("soldier", 1, 0, 0), unit("stalker", 0, 0, 3), unit("golem", 0, 0, golem_y),
    ])

write("ai_high_ground", """
    ai.rs: low ground cannot acquire an enemy up on a plateau; a second enemy on
    the same tier, spawned a tick later, is acquired at once.
    """, [
    "seed 3", "ticks 300", "map 30", "plateau 2.0 -10.0 12.0 10.0 1",
    unit("golem", 1, -2, 0), unit("stalker", 0, 5, 0),
    "1 unit stalker 0 0.0 0.0",
])

write("ai_formation", "ai.rs: six stalkers plain-moved to one point spread into a formation.", [
    "seed 5", "ticks 250", "map 60",
    *[unit("stalker", 0, -20.0 + i * 0.4, 0) for i in range(6)],
    "0 point units=0-5 attack=0 x=10.0 y=0.0 queued=0",
])

write("ai_chain_arc", """
    ai.rs: one wisp bolt strikes a siphon and bounces down a conducting line;
    the fourth siphon is outside the chain's reach.
    """, [
    "seed 5", "ticks 200", "map 60", unit("wisp", 0, 0, 0),
    unit("siphon", 1, 0, 5), unit("siphon", 1, 2, 6), unit("siphon", 1, 4, 7), unit("siphon", 1, 0, -30),
])

# ---- balance.rs --------------------------------------------------------------

BALANCE = [
    ("bal_golem_stalker", "golem:8", 8, "stalker:8", 8, "golems beat equal-count stalkers"),
    ("bal_wisp_golem", "wisp:9", 9, "golem:6", 6, "wisps beat golems at equal Will"),
    ("bal_stalker_wisp", "stalker:8", 8, "wisp:4", 4, "stalkers beat wisps at equal Will"),
    ("bal_stalker_monolith", "stalker:8", 8, "monolith:4", 4, "monoliths lose to field units"),
    ("bal_focus", "stalker:4", 4, "golem:1", 1, "four stalkers focus one golem"),
    ("bal_golem_swat", "golem:4", 4, "swat_trooper:6", 6, "armor blunts small arms"),
    ("bal_officer_wisp", "officer:8", 8, "wisp:4", 4, "pierce shreds unarmored casters"),
    ("bal_wisp_tank", "wisp:9", 9, "tank:3", 3, "arcane cracks military vehicles"),
]
for name, a, na, b, nb, what in BALANCE:
    write(name, f"balance.rs: {what} (the harness's squads and a-move, seed 7).", [
        "seed 7", "ticks 1500",
        f"squad team=0 x=-12.0 y=32.0 comp={a}", f"squad team=1 x=12.0 y=32.0 comp={b}",
        f"0 point units=0-{na - 1} attack=1 x=12.0 y=32.0 queued=0",
        f"0 point units={na}-{na + nb - 1} attack=1 x=-12.0 y=32.0 queued=0",
    ])

# ---- depth_stack.rs ----------------------------------------------------------

write("depth_stack", """
    depth_stack.rs: loot, the affinity payoff and research exercised together -
    a funded Forge researching Munitions while tier-2 Flora stalkers overrun
    enemy soldiers that roll the loot table.
    """, [
    "seed 9", "ticks 680", "map 60", "affinity Flora 300.0",
    "building forge 0 0.0 20.0", "essence 100000.0",
    *[unit("stalker", 0, (i % 6) * 1.2 - 3.0, (i // 6) * 1.2 - 4.0) for i in range(12)],
    *[unit("soldier", 1, i * 1.0 - 2.5, -1.0) for i in range(6)],
    "0 research building=0 upgrade=munitions",
])

write("depth_affinity_duel", "depth_stack.rs: a tier-2 Flora stalker duels an identical enemy.", [
    "seed 9", "ticks 600", "map 60", "affinity Flora 300.0",
    unit("stalker", 0, 0, -0.8), unit("stalker", 1, 0, 0.8),
])

write("depth_research_duel", "depth_stack.rs: a Munitions-2 stalker duels an identical enemy.", [
    "seed 9", "ticks 800", "map 60", "researched munitions 2",
    unit("stalker", 0, 0, -0.8), unit("stalker", 1, 0, 0.8),
])

# ---- economy.rs --------------------------------------------------------------

write("eco_extraction", "economy.rs: a siphon drains a tree to a husk and auto-continues to the next.", [
    "seed 3", "ticks 4000", unit("siphon", 0, -24, -6),
    "source tree -22.0 -6.0", "source tree -20.0 -8.0",
    "0 extract units=0 source=1 queued=0",
])

write("eco_anima", "economy.rs: anima drains proportionally from a living ancient tree.", [
    "seed 4", "ticks 4000", unit("siphon", 0, 10, -20), "source ancient_tree 13.0 -20.0",
    "0 extract units=0 source=1 queued=0",
])

write("eco_production", """
    economy.rs: production gated by affinity, the produces list, Will and cost -
    a stalker refused, then unlocked and built; siphons refused at the Forge and
    queued at the Sanctum until Will binds.
    """, [
    "seed 5", "ticks 700", "building sanctum 0 0.0 -20.0", "building forge 0 8.0 -20.0", "essence 1000.0",
    "0 produce building=1 kind=stalker",
    "2 affinity Flora 10.0", "2 produce building=1 kind=stalker",
    *["304 produce building=1 kind=siphon"] * 12,
    *["305 produce building=0 kind=siphon"] * 12,
])

write("eco_flora_regen", "economy.rs: wounded stalkers and a golem regenerate, near and far from living flora.", [
    "seed 7", "ticks 200", unit("stalker", 0, -19, -6), unit("stalker", 0, 20, -20), unit("golem", 0, -25, -6),
    "source tree -22.0 -6.0", "hp 0 50.0", "hp 1 50.0", "hp 2 50.0",
])

write("eco_anchor", "economy.rs: a siphon places and builds an Anchor, raising the Will cap.", [
    "seed 6", "ticks 600", "building sanctum 0 0.0 -20.0", unit("siphon", 0, 4, -16), "essence 500.0",
    "2 place builder=1 kind=anchor x=8.0 y=-14.0",
])

write("eco_macro_loop", """
    economy.rs full_macro_loop_ends_with_an_army: four siphons extract flora, one
    places and builds a Forge (id 49), which rallies and produces three stalkers.
    """, [
    "seed 7", "ticks 2100", "setup macro",
    "0 extract units=1-4 source=11 queued=0",
    "160 place builder=1 kind=forge x=-26.0 y=-30.0",
    "1060 rally building=49 x=-20.0 y=-28.0",
    *["1060 produce building=49 kind=stalker"] * 3,
])

write("eco_cancel", """
    economy.rs's cancel tests on one timeline: a full refund, popping the back
    while the front keeps its progress, a sole entry resetting, a cancel and a
    produce in one drain reusing the freed Will, and refusals for an empty
    queue, an unknown id and an enemy building.
    """, [
    "seed 7", "ticks 400", "building sanctum 0 0.0 -20.0", "building forge 0 8.0 -20.0",
    "essence 2000.0", "anima 50.0", "affinity Stone 301.0",
    "0 produce building=1 kind=monolith",
    "2 cancel building=1",
    "4 affinity Flora 10.0", "4 produce building=1 kind=stalker", "4 produce building=1 kind=monolith",
    "46 cancel building=1",
    "60 cancel building=1",
    *["62 produce building=1 kind=monolith"] * 3,
    "64 cancel building=1", "64 produce building=1 kind=monolith",
    "100 building forge 1 40.0 40.0", "100 queue 2 monolith", "100 cancel building=2",
    "102 cancel building=4294967295",
])

# ---- hero.rs -----------------------------------------------------------------

HERO = "hero 8.0 -30.0"

write("hero_surge_enemy", "hero.rs: a wounded hero surges an enemy and lifesteals.", [
    "seed 51", "ticks 200", HERO, unit("stalker", 1, 12, -30),
    "0 learn hero=0 ability=0", "0 hp 0 100.0", "0 cast hero=0 ability=0 target=id:1 queued=0",
])

write("hero_xp", "hero.rs: kills of fourteen defenceless siphons feed XP to level 2.", [
    "seed 21", "ticks 2600", HERO,
    *[unit("siphon", 1, 6.0 + (i % 4) * 1.5, -28.0 + (i // 4) * 1.5) for i in range(14)],
])

write("hero_surge_source", "hero.rs: surge drains a tree and heals; then surges an enemy for the refund.", [
    "seed 22", "ticks 400", HERO, "source tree 12.0 -30.0",
    "0 learn hero=0 ability=0", "1 hp 0 300.0", "1 cast hero=0 ability=0 target=id:1 queued=0",
    "81 unit stalker 1 10.0 -26.0", "81 cast hero=0 ability=0 target=id:2 queued=0",
])

write("hero_overcharge", "hero.rs: overcharge buffs a friendly stalker, burns it, and expires.", [
    "seed 23", "ticks 300", HERO, unit("stalker", 0, 10, -30),
    "0 learn hero=0 ability=1", "0 cast hero=0 ability=1 target=id:1 queued=0",
])

write("hero_pulse", "hero.rs: pulse damages and slows inside its radius, not beyond.", [
    "seed 24", "ticks 200", HERO, unit("siphon", 1, 12.5, -30), unit("siphon", 1, 20, -30),
    "0 learn hero=0 ability=2", "0 cast hero=0 ability=2 target=none queued=0",
])

write("hero_avatar", "hero.rs: avatar at level 6 raises thralls from two husks; all of it expires.", [
    "seed 25", "ticks 500", HERO, "source tree 10.0 -30.0 husk", "source tree 6.0 -32.0 husk",
    "herolevel 0 6 0.0 1", "0 learn hero=0 ability=3", "2 cast hero=0 ability=3 target=none queued=0",
])

write("hero_revive", """
    hero.rs: three golems kill a level-3 hero; a broke revive is refused, a
    funded one revives it at the Sanctum with its progress kept.
    """, [
    "seed 26", "ticks 2100", "building sanctum 0 8.0 -24.0", HERO, "herolevel 1 3 600.0 1",
    unit("golem", 1, 9.2, -30), unit("golem", 1, 6.8, -29.6), unit("golem", 1, 8, -28.7),
    "800 essence 10.0", "800 revive", "802 essence 200.0", "802 revive",
])

write("hero_items", "hero.rs: the hero picks up knuckles, then drops them.", [
    "seed 27", "ticks 200", HERO, "item rebar_knuckles 11.0 -30.0",
    "0 pickup hero=0 item=1 queued=0", "80 drop hero=0 slot=0",
])

write("hero_consumables", """
    hero.rs: a poultice heals and is spent, a passive charm cannot be used, and a
    stim grants haste and is spent.
    """, [
    "seed 19", "ticks 100", HERO, "inventory 0 0 field_poultice", "inventory 0 1 rebar_knuckles", "hp 0 100.0",
    "0 use hero=0 slot=0", "1 use hero=0 slot=1",
    "2 inventory 0 2 adrenal_stim", "2 use hero=0 slot=2",
])

write("hero_full_heal_kept", "hero.rs: a heal used at full health is refused, not wasted.", [
    "seed 19", "ticks 20", HERO, "inventory 0 0 field_poultice", "1 use hero=0 slot=0",
])

write("hero_pulse_squad", "hero.rs hero_scenario_is_deterministic: pulse into a jittered squad, then fight.", [
    "seed 31", "ticks 2000", HERO, "squad team=1 x=12.0 y=-29.0 comp=siphon:6",
    "0 learn hero=0 ability=2", "0 cast hero=0 ability=2 target=none queued=0",
])

write("hero_malformed", "hero.rs: an out-of-range ability and kinds are refused, and spend nothing.", [
    "seed 7", "ticks 30", HERO, "building forge 0 6.0 -28.0",
    "0 cast hero=0 ability=99 target=none queued=0",
    "0 produce building=1 kind=@65535",
    "0 place builder=0 kind=@65535 x=6.0 y=-30.0",
])

# ---- m5.rs -------------------------------------------------------------------

write("m5_first_light_descent", "m5.rs: the hero walks down the reshaped First Light mountain to the bench.", [
    "seed 91", "ticks 1200", "mission first_light", "0 point units=15 attack=0 x=0.0 y=-18.0 queued=0",
])

for difficulty in ["story", "hard"]:
    write(f"m5_difficulty_{difficulty}", f"m5.rs: {difficulty} scales enemy hp and fire, never the player's.", [
        "seed 62", "ticks 60", f"difficulty {difficulty}",
        unit("golem", 1, 0, -30), unit("golem", 0, 10, -30),
        unit("siphon", 0, 0, -34), unit("officer", 1, 2, -34),
    ])

write("m5_hero_does_everything", """
    m5.rs: in Heartwood the hero extracts, places the Sanctum (id 48), builds it,
    and produces from it.
    """, [
    "seed 82", "ticks 2100", "mission heartwood",
    "0 extract units=35 source=0 queued=0",
    "320 essence 450.0",
    "322 place builder=35 kind=sanctum x=-40.0 y=-44.0",
    "324 build units=35 site=48 queued=0",
    "1724 essence 200.0", "1724 produce building=48 kind=siphon",
])

write("m5_snapshot", "m5.rs: a Heartwood run saved and restored mid-run continues bit-identically.", [
    "seed 71", "ticks 800", "mission heartwood", "0 extract units=35 source=0 queued=0", "400 snapshot",
])

# ---- mission.rs --------------------------------------------------------------

mission("raid_counts", """
(
    name: "test",
    triggers: [
        (name: "raid", when: [TimeAtLeast(0.1)], actions: [
            SpawnSquad(unit: "golem", count: 4, x: 0.0, y: -20.0, aggressive: false),
            SpawnPlayerSquad(unit: "stalker", count: 4, x: 12.0, y: -20.0),
        ]),
    ],
)
""")
for difficulty in ["normal", "hard"]:
    write(f"mis_raid_counts_{difficulty}", f"mission.rs: {difficulty} fields its enemy squad; player squads never scale.", [
        "seed 70", "ticks 40", f"difficulty {difficulty}", "install missions/raid_counts.ron",
    ])

for count in [1, 2]:
    mission(f"raid_{count}", f"""
(
    name: "test",
    triggers: [
        (name: "raid", when: [TimeAtLeast(0.1)], actions: [
            SpawnSquad(unit: "golem", count: {count}, x: -8.0, y: -20.0, aggressive: true),
        ]),
    ],
)
""")

write("mis_raider_retarget", "mission.rs: raiders crack a fragile Sanctum, then retarget the live stalker.", [
    "seed 71", "ticks 300", "building sanctum 0 6.0 -20.0", "hp 0 5.0", unit("stalker", 0, 20, -20),
    "install missions/raid_1.ron",
])

write("mis_raider_save", "mission.rs: a raid saved mid-march continues bit-identically, Raider and all.", [
    "seed 71", "ticks 280", "building sanctum 0 6.0 -20.0", "hp 0 12.0", unit("stalker", 0, 20, -20),
    "install missions/raid_2.ron", "40 snapshot",
])

mission("triggers_once", """
(
    name: "test",
    triggers: [
        (name: "drop", when: [TimeAtLeast(0.5)], actions: [Message("contact"), GrantEssence(100.0), GrantAnima(5.0)]),
        (name: "drip", once: false, when: [TimeAtLeast(0.5)], actions: [Message("tick")]),
    ],
)
""")
write("mis_triggers_once", "mission.rs: a once-trigger pays once; a repeating one re-fires.", [
    "seed 44", "ticks 40", "install missions/triggers_once.ron",
])

mission("ambush", """
(
    name: "test",
    triggers: [
        (name: "ambush", when: [TimeAtLeast(0.25)], actions: [
            SpawnSquad(unit: "officer", count: 4, x: 30.0, y: -30.0, aggressive: false),
            GrantHeroXp(250.0),
        ]),
    ],
)
""")
write("mis_spawns_xp", "mission.rs: a scripted encounter and objective XP that levels the hero.", [
    "seed 45", "ticks 40", HERO, "install missions/ambush.ron",
])

mission("chain", """
(
    name: "test",
    objectives: [
        (name: "a", text: "first", optional: false),
        (name: "opt", text: "side", optional: true),
    ],
    triggers: [
        (name: "twist", when: [TimeAtLeast(0.25)], actions: [
            CompleteObjective("a"),
            AddObjective(name: "survive", text: "hold", optional: false),
        ]),
        (name: "held", when: [ObjectiveActiveForAtLeast("survive", 1.0)], actions: [CompleteObjective("survive")]),
        (name: "win", when: [AllRequiredObjectivesComplete], actions: [Victory]),
        (name: "too late", when: [TimeAtLeast(2.0)], actions: [Defeat]),
    ],
)
""")
write("mis_objectives", "mission.rs: an objective chain reaches Victory, and the first outcome is final.", [
    "seed 46", "ticks 90", unit("stalker", 0, 0, -30), "install missions/chain.ron",
])

mission("wipe", """
(
    name: "test",
    triggers: [(name: "wiped", when: [PlayerWiped], actions: [Defeat])],
)
""")
write("mis_player_wipe", "mission.rs: a lone stalker is overrun, and the wipe is Defeat.", [
    "seed 47", "ticks 600", unit("stalker", 0, 8, -30),
    unit("golem", 1, 9.5, -30), unit("golem", 1, 6.5, -29.7), unit("golem", 1, 8, -28.4),
    "install missions/wipe.ron",
])

write("mis_hooks", "mission.rs: a mission hook runs every tick, mission or not.", [
    "seed 48", "ticks 10", "hook essence_drip",
])

# ---- research.rs -------------------------------------------------------------

write("res_complete", "research.rs: Munitions completes on schedule and buffs only the player's units.", [
    "seed 7", "ticks 640", "building forge 0 0.0 0.0", "essence 1000.0",
    unit("stalker", 0, 20, 0), unit("stalker", 1, -20, 0),
    "0 research building=0 upgrade=munitions",
])

write("res_queue_and_cap", "research.rs: one research at a time, and Munitions caps at its max level.", [
    "seed 7", "ticks 3400", "building forge 0 0.0 0.0", "essence 1000000.0",
    "0 research building=0 upgrade=munitions", "1 research building=0 upgrade=extraction",
    *[f"{t} research building=0 upgrade=munitions" for t in (700, 1400, 2100, 2800)],
])

write("res_save", "research.rs: research in progress, and research completed, both survive a save.", [
    "seed 7", "ticks 900", "building forge 0 0.0 0.0", "essence 100000.0",
    "0 research building=0 upgrade=munitions", "5 snapshot", "700 snapshot",
])

write("res_poor", "research.rs: research without the essence is refused and spends nothing.", [
    "seed 7", "ticks 20", "building forge 0 0.0 0.0", "essence 10.0",
    "0 research building=0 upgrade=munitions",
])

# ---- tower.rs ----------------------------------------------------------------

write("tower", "tower.rs: a Tower shoots and kills a nearby enemy, and survives.", [
    "seed 7", "ticks 420", "building tower 0 0.0 -30.0", unit("stalker", 1, 6, -30),
])
write("tower_pair", "tower.rs: a Tower against two enemies.", [
    "seed 13", "ticks 200", "building tower 0 0.0 -30.0", unit("stalker", 1, 6, -30), unit("stalker", 1, -5, -28),
])

# ---- loot.rs -----------------------------------------------------------------


def loot_battle(n, enemy_team, killer_team):
    return [
        *[unit("soldier", enemy_team, (i % 4) * 1.0 - 1.5, (i // 4) * 1.0 - 1.5) for i in range(n)],
        *[unit("stalker", killer_team, (i % 10) * 1.2 - 6.0, (i // 10) * 1.2 - 6.0) for i in range(n * 4)],
    ]


write("loot_enemies", "loot.rs: a stalker swarm wipes 24 enemy soldiers, which roll the loot table.", [
    "seed 9", "ticks 500", "map 60", *loot_battle(24, 1, 0),
])
write("loot_player", "loot.rs: the same kind on the PLAYER's team never drops.", [
    "seed 9", "ticks 500", "map 60", *loot_battle(12, 0, 1),
])
write("loot_structure", "loot.rs: a razed enemy structure never drops.", [
    "seed 9", "ticks 800", "map 60", "building anchor 1 0.0 0.0",
    *[unit("stalker", 0, (i % 8) * 1.2 - 4.0, (i // 8) * 1.2 + 3.0) for i in range(24)],
])

# ---- terrain.rs --------------------------------------------------------------

mission("plateau", """
(
    name: "terrain test",
    map: Some((
        half: 30.0,
        obstacles: [],
        plateaus: [(0.0, -30.0, 30.0, 30.0, 1)],
        ramps: [(-4.0, -6.0, 4.0, 6.0)],
    )),
)
""")
write("terrain_climb", "terrain.rs: a unit climbs a plateau only by its ramp.", [
    "seed 5", "ticks 400", "install missions/plateau.ron", unit("stalker", 0, -18, 18),
    "0 point units=0 attack=0 x=18.0 y=18.0 queued=0",
])

write("terrain_poly", "terrain.rs: a diamond plateau raised only inside; a unit walks around it.", [
    "seed 5", "ticks 300", "map 30", "poly 1 0.0,-10.0 10.0,0.0 0.0,10.0 -10.0,0.0", unit("stalker", 0, -18, 0),
    "0 point units=0 attack=0 x=18.0 y=0.0 queued=0",
])

print("written:", len([f for f in os.listdir(HERE) if f.endswith(".scn")]), "scenarios")
