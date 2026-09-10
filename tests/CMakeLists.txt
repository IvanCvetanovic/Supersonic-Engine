# Pure-logic regression tests.
#
# Each suite is one .cpp linked against SupersonicCore. It used to be one .cpp
# plus a hand-maintained list of the engine sources it happened to need, which
# cost 53 duplicate translation units across 23 targets and made the renderer
# untestable - there was no library to link, only an executable.
#
# Linking a static library does not drag the whole engine into every suite: the
# linker pulls only the objects that resolve a symbol the test actually
# references. test_transform still produces a binary that touches no Vulkan
# entry point, which is what keeps these runnable on a machine with no GPU.

set(SUPERSONIC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(add_engine_test name)
    # ${ARGN} is accepted and ignored so a stale extra-sources argument is a
    # no-op rather than a configure error. Nothing should pass one any more.
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE ${SUPERSONIC_SRC}/tests)

    # Everything else - the src/ include root, the vendored header paths, the
    # GLM configuration that MUST match the engine's, and Threads - arrives
    # through SupersonicCore's PUBLIC interface. That is the point: the ODR
    # constraint on GLM_FORCE_DEPTH_ZERO_TO_ONE is now enforced by the build
    # graph instead of by a duplicated block kept in step by hand.
    target_link_libraries(${name} PRIVATE SupersonicCore)

    if (MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_engine_test(test_transform)
add_engine_test(test_launchoptions)
add_engine_test(test_json)
add_engine_test(test_scenemanager)
add_engine_test(test_assetwatcher)
add_engine_test(test_contacts)
add_engine_test(test_sat)
add_engine_test(test_raycast)
add_engine_test(test_meshgen)
add_engine_test(test_heightfield)
add_engine_test(test_joints)
add_engine_test(test_convexhull)
add_engine_test(test_decomposition)
add_engine_test(test_draworder)
add_engine_test(test_nav)
add_engine_test(test_renderplan)
add_engine_test(test_sprite)
add_engine_test(test_tilemap)
add_engine_test(test_uilayer)

# The ported game, verified against the original.
#
# Links WolfBrigadeSim on top of SupersonicCore. Each of these reproduces the
# numbers one of `tools/verify_*.gd` prints when run against the Godot project
# at D:/The-Wolf-Brigade - the command is recorded in the suite itself, and the
# numbers are constants here rather than something ctest goes and asks Godot
# for. That binary is Steam-installed on another volume and will not exist on a
# build machine.
function(add_port_test name)
    add_engine_test(${name})
    target_link_libraries(${name} PRIVATE WolfBrigadeSim)
endfunction()

add_port_test(test_wb_gestures)
add_port_test(test_wb_data)
add_port_test(test_wb_economy)
add_port_test(test_wb_waves)
add_port_test(test_wb_worker)
add_port_test(test_wb_combat)
add_port_test(test_wb_buildings)
add_port_test(test_wb_progression)
add_port_test(test_wb_snapshot)
add_port_test(test_wb_match)
add_port_test(test_wb_audio)
add_port_test(test_wb_placement)
add_port_test(test_wb_selection)
add_port_test(test_wb_squads)
add_port_test(test_wb_hero)
add_port_test(test_wb_controls)

# The LAYER, not the simulation - so this one also links WolfBrigadeGame. It is
# the only suite that touches the seam between the ported game and the picture,
# and the reason it can exist at all is that the layer became a library.
add_port_test(test_wb_hud)
target_link_libraries(test_wb_hud PRIVATE WolfBrigadeGame)

# Also the layer, and the ONE suite allowed to give it a save directory.
#
# Every other layer suite depends on a bare layer touching no disk - two suites
# writing the same filenames in the same working directory fail in an
# order-dependent way that reads as a flake. So that contract is asserted here,
# and this is the only file that exercises the paths.
add_port_test(test_wb_persistence)
target_link_libraries(test_wb_persistence PRIVATE WolfBrigadeGame)

# And test_wb_audio, which grew a second half. The first is arithmetic against
# the original's harness and needs nothing; the second is the layer registering
# those samples with a real engine and playing them, which is the seam the
# synthesis tests could not see.
target_link_libraries(test_wb_audio PRIVATE WolfBrigadeGame)

# HUSK's simulation, ported from Rust. Stricter than the Wolf Brigade suites:
# the goldens are the Rust sim's own SimHash after every tick of each scripted
# scenario, printed by games/husk/oracle, and the port must match them bit for
# bit. The oracle is not run by ctest - it needs the game's repository and a
# Rust toolchain - so its output is checked in beside the scenarios.
function(add_husk_test name)
    add_engine_test(${name})
    target_link_libraries(${name} PRIVATE HuskSim)
endfunction()

add_husk_test(test_husk_foundation)
add_husk_test(test_husk_scenarios)

# The layer that draws it, attached to a bare registry: no window, no Vulkan.
# Its first job is to prove the view leaves the sim alone - the layer's world
# must hash exactly as a bare World stepped beside it.
add_husk_test(test_husk_layer)
target_link_libraries(test_husk_layer PRIVATE HuskGame)

# Magic Portals, from the Godot remake. The weakest oracle of the three: the
# remake's harnesses assert behaviour over Godot's 2D solver, which is not being
# reimplemented, so only what the converter and the role table decide is pinned
# exactly. The acceptance rule is in docs/planning/2026-09-10-magic-portals-spike.md.
function(add_mp_test name)
    add_engine_test(${name})
    target_link_libraries(${name} PRIVATE MagicPortalsSim)
endfunction()

add_mp_test(test_mp_tscn)
add_mp_test(test_mp_levels)

# test_mp_levels reads the converted levels from outside this repository, so on
# a build machine they can simply be absent. It exits 77 then, which ctest
# reports as SKIPPED with the path it looked in - never as a pass.
set_tests_properties(test_mp_levels PROPERTIES SKIP_RETURN_CODE 77)

# The simulation's own sin, cos, asin, atan2 and pow, pinned bit for bit so the
# determinism claim holds across C runtimes (DetMath.hpp).
add_engine_test(test_detmath)
add_engine_test(test_environmentmap)
add_engine_test(test_assetdatabase)
add_engine_test(test_serialize)
add_engine_test(test_frustum)
add_engine_test(test_input)
add_engine_test(test_spotlight)
add_engine_test(test_lightselection)
add_engine_test(test_clustergrid)
add_engine_test(test_shadowcache)
add_engine_test(test_resourcesync)
add_engine_test(test_layerstack)
add_engine_test(test_determinism)
add_engine_test(test_replay)
add_engine_test(test_camera)
add_engine_test(test_codecextension)
add_engine_test(test_pointshadow)
add_engine_test(test_cascades)
add_engine_test(test_jobs)
add_engine_test(test_uiinput)
add_engine_test(test_uicanvas)
add_engine_test(test_worldshapes)
add_engine_test(test_gameruntime)
add_engine_test(test_packaging)
add_engine_test(test_userdata)
add_engine_test(test_mixer)
add_engine_test(test_undo)
add_engine_test(test_materials)
add_engine_test(test_physics)
add_engine_test(test_audio)
add_engine_test(test_scripts)
add_engine_test(test_hierarchy)
add_engine_test(test_gltf)
add_engine_test(test_blending)
add_engine_test(test_skeletal)

# Suites that read real assets need the project root, because asset paths
# resolve against the working directory.
set_tests_properties(
    test_gltf       # assets/models/monument.gltf
    test_blending   # assets/models/bender.gltf
    test_skeletal   # assets/models/bender.gltf
    test_audio      # the shipped ambient clip
    test_convexhull # assets/models/monument.gltf, for the hull cache
    test_physics    # assets/scenes/MainScene.scene, to prove it has a floor
    test_packaging  # copies assets/, and now checks the .meta files come too
    test_materials  # reads assets/shaders/shader.frag, to check the UV slot
                    # packing agrees with the shader that unpacks it
    PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")
