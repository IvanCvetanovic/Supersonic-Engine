cmake_minimum_required(VERSION 3.20)
project(SupersonicEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug for single-config generators (Ninja/Makefiles). Multi-config
# generators (Visual Studio, Xcode) ignore CMAKE_BUILD_TYPE and use --config.
get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT _isMultiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set; defaulting to Debug")
endif()
if (_isMultiConfig)
    message(STATUS "Multi-config generator: CMAKE_BUILD_TYPE is ignored, use --config <cfg>")
endif()

# Target Platform Detection
if (ANDROID)
    message(STATUS "Targeting Android NDK platform (Vulkan NativeActivity)")
    add_compile_definitions(SUPERSONIC_PLATFORM_ANDROID=1)
elseif (APPLE)
    if (IOS)
        message(STATUS "Targeting iOS platform (MoltenVK Metal)")
        add_compile_definitions(SUPERSONIC_PLATFORM_IOS=1)
    else()
        message(STATUS "Targeting macOS platform (MoltenVK Metal)")
        add_compile_definitions(SUPERSONIC_PLATFORM_MACOS=1)
    endif()
elseif (WIN32)
    message(STATUS "Targeting Windows platform (Win32 Vulkan)")
    add_compile_definitions(SUPERSONIC_PLATFORM_WINDOWS=1)
else()
    message(STATUS "Targeting Linux platform (X11/Wayland Vulkan)")
    add_compile_definitions(SUPERSONIC_PLATFORM_LINUX=1)
endif()

# ---------------------------------------------------------------------------
# Vulkan
#
# Prefer a real SDK. Without one we can still build against the vendored
# headers plus the loader that ships with the graphics driver, but a linker
# needs an *import library*, not the DLL itself. On Windows with MSVC we
# synthesise one from the loader's export table at configure time.
# ---------------------------------------------------------------------------
# JobSystem uses std::thread. MSVC needs nothing for that; GCC and Clang need
# the platform's thread library, and omitting it is either a link error or a
# std::system_error the first time a worker starts.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Vulkan QUIET)

if (Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_LIBRARY}")
else()
    message(STATUS "No Vulkan SDK found - falling back to vendored headers + system loader")
    set(Vulkan_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers-1.3.290/include")

    if (WIN32 AND MSVC)
        set(_vkLoaderDll "$ENV{SystemRoot}/System32/vulkan-1.dll")
        if (NOT EXISTS "${_vkLoaderDll}")
            message(FATAL_ERROR
                "Vulkan loader not found at ${_vkLoaderDll}.\n"
                "Install a Vulkan-capable graphics driver, or install the Vulkan SDK "
                "and re-run CMake.")
        endif()

        # Locate dumpbin/lib from the active MSVC toolchain.
        get_filename_component(_msvcBinDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(MSVC_DUMPBIN dumpbin HINTS "${_msvcBinDir}")
        find_program(MSVC_LIB     lib     HINTS "${_msvcBinDir}")
        if (NOT MSVC_DUMPBIN OR NOT MSVC_LIB)
            message(FATAL_ERROR
                "Could not locate dumpbin/lib in ${_msvcBinDir}.\n"
                "Install the Vulkan SDK instead, or build from a Developer Command Prompt.")
        endif()

        set(_vkGenDir "${CMAKE_CURRENT_BINARY_DIR}/vulkan-import")
        set(_vkImportLib "${_vkGenDir}/vulkan-1.lib")

        if (NOT EXISTS "${_vkImportLib}")
            message(STATUS "Generating Vulkan import library from ${_vkLoaderDll}")
            file(MAKE_DIRECTORY "${_vkGenDir}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}"
                        -DDUMPBIN=${MSVC_DUMPBIN}
                        -DLIBTOOL=${MSVC_LIB}
                        -DDLL=${_vkLoaderDll}
                        -DOUTDIR=${_vkGenDir}
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVulkanImportLib.cmake"
                RESULT_VARIABLE _vkGenResult
                OUTPUT_VARIABLE _vkGenOut
                ERROR_VARIABLE  _vkGenOut)
            if (NOT _vkGenResult EQUAL 0 OR NOT EXISTS "${_vkImportLib}")
                message(FATAL_ERROR "Failed to generate Vulkan import library:\n${_vkGenOut}")
            endif()
        endif()

        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${_vkImportLib}")

    elseif (WIN32)
        # MinGW's ld can link a DLL directly.
        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
            IMPORTED_LOCATION "$ENV{SystemRoot}/System32/vulkan-1.dll")
    else()
        # Linux / macOS: link the loader shared object by name.
        if (APPLE)
            find_library(_vkLoader NAMES vulkan MoltenVK)
        else()
            find_library(_vkLoader NAMES vulkan)
        endif()
        if (NOT _vkLoader)
            message(FATAL_ERROR
                "No Vulkan loader found. Install the Vulkan SDK or your distribution's "
                "vulkan-loader package (libvulkan.so.1 / libMoltenVK.dylib).")
        endif()
        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${_vkLoader}")
    endif()
endif()

# GLFW setup
#
# DESKTOP ONLY, and this guard is the first thing standing between this tree and
# an Android build. GLFW 3.4 has no Android backend at all, and worse than being
# useless there it is actively fatal: the NDK toolchain sets UNIX=1, so GLFW's
# own CMakeLists turns Wayland and X11 support on for "any UNIX that is not
# Apple" and then fails the configure outright looking for wayland-scanner. The
# engine never gets as far as reporting its own problems.
#
# What replaces it on Android is not written yet - a surface comes from
# ANativeWindow through VK_KHR_android_surface rather than from
# glfwCreateWindowSurface - so an Android configure that gets past this point
# still fails to COMPILE, at Window.hpp. That is the honest next blocker and it
# is a real one; this only moves the failure from "cannot configure the tree" to
# "has not written the windowing backend", which is progress of the kind that
# can be worked on.
if (NOT ANDROID)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4)
endif()

# EnTT setup
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt-3.13.2)

# VMA setup
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator-3.1.0)

# Collect ImGui & ImGuizmo sources
set(IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp
)

# The GLFW backend follows GLFW. ImGui itself is platform-agnostic and the
# Vulkan backend is fine anywhere; only the window-system half has to go.
if (NOT ANDROID)
    list(APPEND IMGUI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp)
endif()

# Collect engine sources. CONFIGURE_DEPENDS re-globs on build so adding or
# removing a file does not silently produce a stale project.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Android-only sources must not enter a desktop build.
if (NOT ANDROID)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/platform/AndroidNativeApp\\.(cpp|hpp)$")
endif()

# main.cpp belongs to the executable, not the library: linking a static library
# that contains main() into a test would either collide with the test's own
# main or, worse, silently win.
list(REMOVE_ITEM ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# The engine builds as a LIBRARY plus a thin executable, rather than one
# executable, for one reason: a test cannot link an executable. Every suite
# under tests/ used to recompile the engine sources it needed - 53 duplicate
# translation units across 23 targets - and no suite could reach the renderer
# at all, because there was nothing to link against.
#
# STATIC rather than OBJECT is safe only while script registration stays
# explicit. ScriptEngine.cpp registers its built-ins by calling Register(), so
# the linker has no unreferenced-but-required objects to discard. The moment
# anything self-registers from a file-scope initializer, that object gets
# dropped from a target that never names it and the script silently vanishes -
# switch to OBJECT if that day comes.
add_library(SupersonicCore STATIC ${ENGINE_SOURCES} ${IMGUI_SOURCES})
add_executable(SupersonicEngine ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(SupersonicEngine PRIVATE SupersonicCore)

# Keep the IDE tree mirroring the source tree instead of one flat blob.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${ENGINE_SOURCES})
source_group("third_party/imgui" FILES ${IMGUI_SOURCES})

# PUBLIC throughout: every consumer - the executable and every test target -
# must compile against exactly the same include paths and GLM configuration.
# Expressing that through the link interface is what makes the ODR constraint
# below a fact of the build graph instead of a comment someone has to obey.
target_include_directories(SupersonicCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imguizmo
)

# GLM's configuration MUST be identical in every translation unit. Defining
# these in headers is order-dependent and silently produces an ODR violation
# where getProjectionMatrix() compiles differently per TU.
target_compile_definitions(SupersonicCore PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL

    # glfw3.h includes <GL/gl.h> unless told not to. This is a Vulkan engine and
    # never touches OpenGL, but the header does not know that - so on a machine
    # with no OpenGL development headers installed, every file that reaches
    # Window.hpp fails to compile. Windows and macOS ship GL headers with their
    # SDKs, which is exactly why this went unnoticed until a Linux CI runner
    # without libgl1-mesa-dev tried it.
    #
    # On the target rather than in a header, because the vendored
    # imgui_impl_glfw.cpp is compiled into this target and includes glfw3.h
    # itself.
    GLFW_INCLUDE_NONE
)

# Vendored code we do not maintain: ImGui/ImGuizmo, and the VMA implementation,
# which is header-only and lives in its own TU precisely so it can be silenced
# without silencing first-party code.
set(VENDORED_SOURCES
    ${IMGUI_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/VmaImplementation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/TinyGltfImplementation.cpp
    # Hosts STB_IMAGE_WRITE_IMPLEMENTATION, so it carries stb's warnings the way
    # the two above carry VMA's and tinygltf's. The engine's own code in this
    # file is small; the alternative is /W4 on somebody else's sprintf.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/ScreenCapture.cpp
)

# PRIVATE, not PUBLIC: the engine's own warning level is the engine's business.
# Pushing /W4 down the link interface would hold every test target - and anyone
# who ever links this library - to it as well.
if (MSVC)
    target_compile_options(SupersonicCore PRIVATE /W4 /permissive-)
    target_compile_options(SupersonicEngine PRIVATE /W4 /permissive-)
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(SupersonicCore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(SupersonicEngine PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Validation layers, decoupled from the build type.
#
# AUTO reproduces the old behaviour: on in Debug, off in Release. The reason it
# is not simply left that way is the Linux CI job, which builds Release on
# purpose because the runner's memory does not survive Debug across forty
# translation units - so the one job that could run the layers structurally
# could not. ON makes a Release build that validates; OFF makes a Debug build
# that does not, which is the fast iteration case.
set(SUPERSONIC_ENABLE_VALIDATION "AUTO" CACHE STRING
    "Vulkan validation layers: ON, OFF, or AUTO (Debug only)")
set_property(CACHE SUPERSONIC_ENABLE_VALIDATION PROPERTY STRINGS AUTO ON OFF)

if (SUPERSONIC_ENABLE_VALIDATION STREQUAL "ON")
    message(STATUS "Vulkan validation: ON (all configurations)")
    target_compile_definitions(SupersonicCore PUBLIC SUPERSONIC_ENABLE_VALIDATION=1)
elseif (SUPERSONIC_ENABLE_VALIDATION STREQUAL "OFF")
    message(STATUS "Vulkan validation: OFF (all configurations)")
    target_compile_definitions(SupersonicCore PUBLIC SUPERSONIC_ENABLE_VALIDATION=0)
elseif (SUPERSONIC_ENABLE_VALIDATION STREQUAL "AUTO")
    message(STATUS "Vulkan validation: AUTO (Debug only)")
    # Define nothing; the header falls back to NDEBUG.
else()
    message(FATAL_ERROR
        "SUPERSONIC_ENABLE_VALIDATION must be ON, OFF or AUTO, "
        "not '${SUPERSONIC_ENABLE_VALIDATION}'")
endif()

# PUBLIC because these appear in the engine's own headers: vulkan.hpp in
# Components.hpp, GLFW in Window.hpp, EnTT in every system signature, VMA in
# the renderer headers and <thread> in JobSystem.hpp. A consumer that includes
# any of those needs the same include paths and the same link libraries.
target_link_libraries(SupersonicCore PUBLIC
    Vulkan::Vulkan
    EnTT::EnTT
    VulkanMemoryAllocator
    Threads::Threads
)

# glfw is PUBLIC on desktop for the reason above - Window.hpp includes it - and
# simply absent on Android, where the target does not exist.
if (NOT ANDROID)
    target_link_libraries(SupersonicCore PUBLIC glfw)
endif()

# Audio output.
#
# XAudio2 ships with Windows, so that path adds no third-party dependency.
# Linux has no equivalent in the base system, so ALSA is used when its
# development headers are present and the engine falls back to the documented
# no-op when they are not - a missing package must not fail the build for
# someone who does not care about sound.
# PRIVATE: the backend is an implementation detail of AudioEngine.cpp, which
# lives inside the library. A static library still forwards these to whatever
# links it, so the executable gets them without the link interface advertising
# an XAudio2 dependency to every test target.
if (WIN32)
    target_link_libraries(SupersonicCore PRIVATE xaudio2 ole32)
elseif (UNIX AND NOT APPLE AND NOT ANDROID)
    find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
    find_library(ALSA_LIBRARY NAMES asound)
    if (ALSA_INCLUDE_DIR AND ALSA_LIBRARY)
        message(STATUS "Audio backend: ALSA (${ALSA_LIBRARY})")
        target_compile_definitions(SupersonicCore PRIVATE SUPERSONIC_AUDIO_ALSA=1)
        target_include_directories(SupersonicCore PRIVATE ${ALSA_INCLUDE_DIR})
        target_link_libraries(SupersonicCore PRIVATE ${ALSA_LIBRARY})
    else()
        message(STATUS "Audio backend: none (install libasound2-dev for sound on Linux)")
    endif()
endif()

# Assets are loaded with paths relative to the working directory, so make F5
# in Visual Studio launch from the project root rather than the build folder.
set_target_properties(SupersonicEngine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

if (_isMultiConfig)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT SupersonicEngine)
endif()

# ---------------------------------------------------------------------------
# Shaders: compile GLSL -> SPIR-V as part of the build when a compiler exists.
# ---------------------------------------------------------------------------
find_program(GLSL_COMPILER
    NAMES glslc glslangValidator glslang
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES bin)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(SHADER_JOBS
    "shader.vert=vert.spv"
    "shader.frag=frag.spv"
    "world_shape.vert=world_shape_vert.spv"
    "world_shape.frag=world_shape_frag.spv"
    "grid.vert=grid_vert.spv"
    "grid.frag=grid_frag.spv"
    "sky.vert=sky_vert.spv"
    "sky.frag=sky_frag.spv"
    "shadow.vert=shadow_vert.spv"
    "shadow.frag=shadow_frag.spv"
    "shadow_cutout.frag=shadow_cutout_frag.spv"
    "fullscreen.vert=fullscreen_vert.spv"
    "bloom_bright.frag=bloom_bright.spv"
    "bloom_blur.frag=bloom_blur.spv"
    "bloom_composite.frag=bloom_composite.spv"
)

# Headers shared between shaders. Not compiled themselves; every compiled
# shader depends on all of them, which is coarse and correct - there are two.
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS "${SHADER_DIR}/*.glsl")

if (GLSL_COMPILER)
    message(STATUS "Shader compiler: ${GLSL_COMPILER}")
    get_filename_component(_glslName "${GLSL_COMPILER}" NAME_WE)

    set(SPIRV_OUTPUTS "")
    foreach (job ${SHADER_JOBS})
        string(REPLACE "=" ";" _pair "${job}")
        list(GET _pair 0 _src)
        list(GET _pair 1 _out)
        set(_srcPath "${SHADER_DIR}/${_src}")
        set(_outPath "${SHADER_DIR}/${_out}")

        if (_glslName STREQUAL "glslc")
            set(_cmd "${GLSL_COMPILER}" "${_srcPath}" -o "${_outPath}")
        else()
            set(_cmd "${GLSL_COMPILER}" -V "${_srcPath}" -o "${_outPath}")
        endif()

        add_custom_command(
            OUTPUT "${_outPath}"
            COMMAND ${_cmd}
            # SHADER_INCLUDES as well as the source: the scene UBO block is
            # #included by three shaders, and depending only on the .vert/.frag
            # would leave every one of them stale after an edit to the block
            # they share - which is the failure mode extracting it was meant to
            # remove, reintroduced one level down.
            DEPENDS "${_srcPath}" ${SHADER_INCLUDES}
            COMMENT "Compiling ${_src} -> ${_out}"
            VERBATIM)
        list(APPEND SPIRV_OUTPUTS "${_outPath}")
    endforeach()

    add_custom_target(Shaders DEPENDS ${SPIRV_OUTPUTS})
    add_dependencies(SupersonicEngine Shaders)
else()
    message(WARNING
        "No GLSL compiler found (glslc/glslangValidator). The committed .spv blobs in "
        "assets/shaders will be used as-is and will NOT be regenerated if you edit the "
        "GLSL sources. Install the Vulkan SDK to enable shader compilation.")
endif()

# ---------------------------------------------------------------------------
# Hot-reloadable script plugin
# ---------------------------------------------------------------------------
option(SUPERSONIC_BUILD_SCRIPT_PLUGIN "Build the sample hot-reload script plugin" ON)
if (SUPERSONIC_BUILD_SCRIPT_PLUGIN)
    add_subdirectory(plugins)
    # Not a link dependency - the engine loads it at runtime and tolerates its
    # absence - but building it by default makes the feature work out of the box.
    add_dependencies(SupersonicEngine GameScripts)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Games built on the engine
# ---------------------------------------------------------------------------
#
# Off by default: these link the engine the way anyone else would, and are not
# part of building it. Turn one on with -DSUPERSONIC_BUILD_WOLFBRIGADE=ON.
# The ported SIMULATION is not gated, and the executable is.
#
# The port is verified by C++ tests that reproduce the numbers the original's
# headless harnesses print, so the simulation has to be built whenever the tests
# are. It is pure logic - no renderer, no window - and costs a handful of
# translation units.
add_subdirectory(games/wolfbrigade/sim)

option(SUPERSONIC_BUILD_WOLFBRIGADE "Build the Wolf Brigade port spike" OFF)
if (SUPERSONIC_BUILD_WOLFBRIGADE)
    add_subdirectory(games/wolfbrigade)
endif()

option(SUPERSONIC_BUILD_TESTS "Build engine unit tests" ON)
if (SUPERSONIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
