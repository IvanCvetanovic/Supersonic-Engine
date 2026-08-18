# Pure-logic regression tests.
#
# Each test compiles the engine sources it needs directly rather than linking a
# library target, because the engine builds as an executable. None of the code
# under test issues Vulkan calls, so only the headers are required.

set(SUPERSONIC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# tinygltf and json.hpp are vendored; they must not be held to the engine's
# warning level. Both branches matter - the MSVC-only guard left them compiling
# under -Wall -Wextra -Wpedantic on the Linux CI job.
if (MSVC)
    set(_vendorWarnFlag "/W0")
else()
    set(_vendorWarnFlag "-w")
endif()

function(add_engine_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${SUPERSONIC_SRC}/src
        ${SUPERSONIC_SRC}/tests
        ${Vulkan_INCLUDE_DIRS}
        ${SUPERSONIC_SRC}/third_party/glm
        ${SUPERSONIC_SRC}/third_party/entt-3.13.2/src
    )
    target_compile_definitions(${name} PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
    )
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    # Several suites compile JobSystem.cpp, which starts std::threads. Linking
    # this unconditionally is simpler than tracking which ones, and costs the
    # others nothing.
    target_link_libraries(${name} PRIVATE Threads::Threads)

    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_engine_test(test_transform)
add_engine_test(test_raycast   ${SUPERSONIC_SRC}/src/core/Raycast.cpp
                               ${SUPERSONIC_SRC}/src/core/TransformSystem.cpp)
# MeshData::computeTangents dispatches its per-vertex pass through JobSystem, so
# every target that builds a mesh needs it. With no pool running it falls back to
# an inline loop, which is what keeps these tests single-threaded and repeatable.
add_engine_test(test_meshgen   ${SUPERSONIC_SRC}/src/core/ModelLoader.cpp
                               ${SUPERSONIC_SRC}/src/core/TerrainGenerator.cpp
                               ${SUPERSONIC_SRC}/src/core/JobSystem.cpp)
add_engine_test(test_serialize ${SUPERSONIC_SRC}/src/core/SceneSerializer.cpp
                               ${SUPERSONIC_SRC}/src/core/ComponentCodec.cpp
                               ${SUPERSONIC_SRC}/src/core/PrefabSerializer.cpp)
add_engine_test(test_frustum)
add_engine_test(test_input     ${SUPERSONIC_SRC}/src/core/Input.cpp)
add_engine_test(test_spotlight  ${SUPERSONIC_SRC}/src/renderer/SpotLight.cpp)
add_engine_test(test_pointshadow ${SUPERSONIC_SRC}/src/renderer/PointShadow.cpp)
add_engine_test(test_cascades  ${SUPERSONIC_SRC}/src/renderer/ShadowCascades.cpp)
add_engine_test(test_jobs      ${SUPERSONIC_SRC}/src/core/JobSystem.cpp)
add_engine_test(test_uiinput   ${SUPERSONIC_SRC}/src/core/UIInput.cpp
                               ${SUPERSONIC_SRC}/src/core/UICanvas.cpp)
add_engine_test(test_uicanvas  ${SUPERSONIC_SRC}/src/core/UICanvas.cpp)
add_engine_test(test_gameruntime ${SUPERSONIC_SRC}/src/core/GameRuntime.cpp
                                 ${SUPERSONIC_SRC}/src/platform/ExecutablePath.cpp)
add_engine_test(test_mixer     ${SUPERSONIC_SRC}/src/core/AudioMixer.cpp
                               ${SUPERSONIC_SRC}/src/core/AudioClip.cpp)
add_engine_test(test_undo      ${SUPERSONIC_SRC}/src/editor/EditHistory.cpp
                               ${SUPERSONIC_SRC}/src/core/SceneSerializer.cpp
                               ${SUPERSONIC_SRC}/src/core/ComponentCodec.cpp)
add_engine_test(test_materials ${SUPERSONIC_SRC}/src/core/MaterialLibrary.cpp
                               ${SUPERSONIC_SRC}/src/core/MaterialSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/SceneSerializer.cpp
                               ${SUPERSONIC_SRC}/src/core/ComponentCodec.cpp)
add_engine_test(test_physics   ${SUPERSONIC_SRC}/src/core/PhysicsSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/TransformSystem.cpp)
add_engine_test(test_audio     ${SUPERSONIC_SRC}/src/core/AudioClip.cpp)
add_engine_test(test_scripts   ${SUPERSONIC_SRC}/src/core/ScriptRegistry.cpp)
add_engine_test(test_hierarchy ${SUPERSONIC_SRC}/src/core/TransformSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/PlayMode.cpp
                               ${SUPERSONIC_SRC}/src/core/SceneSerializer.cpp
                               ${SUPERSONIC_SRC}/src/core/ComponentCodec.cpp
                               ${SUPERSONIC_SRC}/src/core/TimeTravelDebugger.cpp)

# TimeTravelDebugger's panel uses ImGui, which PlayMode::Play touches via Clear().
target_include_directories(test_hierarchy PRIVATE ${SUPERSONIC_SRC}/third_party/imgui)
target_sources(test_hierarchy PRIVATE
    ${SUPERSONIC_SRC}/third_party/imgui/imgui.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_draw.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_tables.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_widgets.cpp)
set_source_files_properties(
    ${SUPERSONIC_SRC}/third_party/imgui/imgui.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_draw.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_tables.cpp
    ${SUPERSONIC_SRC}/third_party/imgui/imgui_widgets.cpp
    TARGET_DIRECTORY test_hierarchy PROPERTIES COMPILE_OPTIONS "${_vendorWarnFlag}")
add_engine_test(test_gltf      ${SUPERSONIC_SRC}/src/core/GltfLoader.cpp
                               ${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp
                               ${SUPERSONIC_SRC}/src/core/JobSystem.cpp)

# Deliberately NOT RenderSystem.cpp: it calls vkCmd*, and these targets link no
# Vulkan library. That is exactly why the palette gather lives in
# AnimationSystem.cpp rather than next to the draw loop that consumes it.
add_engine_test(test_blending  ${SUPERSONIC_SRC}/src/core/AnimationSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/AnimationLibrary.cpp
                               ${SUPERSONIC_SRC}/src/core/GltfLoader.cpp
                               ${SUPERSONIC_SRC}/src/core/JobSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp)
add_engine_test(test_skeletal  ${SUPERSONIC_SRC}/src/core/AnimationSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/AnimationLibrary.cpp
                               ${SUPERSONIC_SRC}/src/core/GltfLoader.cpp
                               ${SUPERSONIC_SRC}/src/core/JobSystem.cpp
                               ${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp)
target_include_directories(test_blending PRIVATE ${SUPERSONIC_SRC}/third_party/tinygltf)
set_source_files_properties(${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp
    TARGET_DIRECTORY test_blending PROPERTIES COMPILE_OPTIONS "${_vendorWarnFlag}")

set_tests_properties(test_blending PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")

target_include_directories(test_skeletal PRIVATE ${SUPERSONIC_SRC}/third_party/tinygltf)
set_tests_properties(test_skeletal PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")
set_source_files_properties(${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp
    TARGET_DIRECTORY test_skeletal PROPERTIES COMPILE_OPTIONS "${_vendorWarnFlag}")

target_include_directories(test_gltf PRIVATE ${SUPERSONIC_SRC}/third_party/tinygltf)
# Runs against assets/models/monument.gltf, so it needs the project root.
set_tests_properties(test_gltf PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")

set_source_files_properties(${SUPERSONIC_SRC}/src/core/TinyGltfImplementation.cpp
    TARGET_DIRECTORY test_gltf PROPERTIES COMPILE_OPTIONS "${_vendorWarnFlag}")

# test_audio also opportunistically checks the shipped ambient clip, so run the
# suite from the project root where assets/ resolves.
set_tests_properties(test_audio PROPERTIES WORKING_DIRECTORY "${SUPERSONIC_SRC}")
