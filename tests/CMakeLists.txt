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

# The rule itself - the harness, SupersonicCore and the GLM configuration its
# interface carries, /W4, add_test - is supersonic_add_test in
# cmake/SupersonicTesting.cmake, where a game building the engine as a
# subproject reaches it too. The engine's own suites keep this name for it.
function(add_engine_test name)
    supersonic_add_test(${name} ${ARGN})
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
# The BMFont reader: a .fnt into glyph quads, so a game can draw text IN THE
# WORLD. ImGui's atlas is the engine's only other text and it draws in screen
# space, so nothing could put a glyph at a level's own coordinates before this.
add_engine_test(test_bitmapfont)
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
# 2D point lights: the gather that packs Light2DComponents for scene binding 12,
# and shader.frag's light loop through its CPU twin, Light2D::Contribution. Pure
# arithmetic, so it needs no working directory; test_materials holds the
# shader's declarations to the same constants.
add_engine_test(test_light2d)
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
add_engine_test(test_screenoverlay)
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
                    # packing agrees with the shader that unpacks it, and
                    # that its 2D sprite switches, light mask shift and
                    # overlay binding are the ones VulkanPipeline.hpp states,
                    # and its 2D light buffer the one core/Light2D.hpp packs
    test_screenoverlay # reads assets/shaders/screen_overlay.vert, to check its
                       # vertex table is the one ScreenOverlay::CornerOf states,
                       # and bloom_composite.frag, to check its branches are
                       # numbered as RenderSettings::SceneEncoding is
    PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")
