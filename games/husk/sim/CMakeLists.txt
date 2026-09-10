# HUSK's simulation, ported from Rust (Desktop/Test-Game at 721e19e).
#
# A library, built unconditionally, for the reason WolfBrigadeSim is one: the
# tests link it. Here the verification is stricter - the suites hold every tick
# of every scripted scenario to the Rust original's own SimHash, bit for bit,
# using goldens the oracle in ../oracle prints. See
# docs/planning/2026-09-10-husk-port.md.
#
# It links nothing from the engine. The game's sim borrows nothing from Bevy
# but the ECS containers, and the port borrows nothing from Supersonic.

add_library(HuskSim STATIC
    Building.cpp
    Combat.cpp
    Data.cpp
    Economy.cpp
    Flow.cpp
    Hash.cpp
    Hero.cpp
    Map.cpp
    Mission.cpp
    Movement.cpp
    Orders.cpp
    Ron.cpp
    Scenario.cpp
    Sim.cpp
    Snapshot.cpp
    Spawn.cpp
    World.cpp
)

target_compile_features(HuskSim PUBLIC cxx_std_20)
target_include_directories(HuskSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Absolute paths baked in at configure time, right from the build tree and the
# source tree alike: the copied data, and the oracle's goldens and scenarios.
target_compile_definitions(HuskSim PUBLIC
    HUSK_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
    HUSK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden")

# NO FUSED MULTIPLY-ADD. `(x*x) + (y*y)` contracted to fma(x, x, y*y) rounds
# once instead of twice, and the Rust original never contracts. PUBLIC, because
# the Vec2 maths is inline in headers: a test that recomputes a primitive
# compiles float code in its own translation unit and needs the same rule. GCC
# contracts by default in its GNU dialects, clang within expressions; MSVC's
# /fp:precise does not, and test_husk_foundation's primitive check is the proof
# either way.
if (MSVC)
    target_compile_options(HuskSim PRIVATE /W4)
    target_compile_options(HuskSim PUBLIC /fp:precise)
else()
    target_compile_options(HuskSim PRIVATE -Wall -Wextra)
    target_compile_options(HuskSim PUBLIC -ffp-contract=off)
endif()
