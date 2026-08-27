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
add_engine_test(test_codecextension)
add_engine_test(test_pointshadow)
add_engine_test(test_cascades)
add_engine_test(test_jobs)
add_engine_test(test_uiinput)
add_engine_test(test_uicanvas)
add_engine_test(test_gameruntime)
add_engine_test(test_packaging)
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
    PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")
