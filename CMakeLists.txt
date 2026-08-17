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
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imguizmo/ImGuizmo.cpp
)

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

add_executable(SupersonicEngine ${ENGINE_SOURCES} ${IMGUI_SOURCES})

# Keep the IDE tree mirroring the source tree instead of one flat blob.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${ENGINE_SOURCES})
source_group("third_party/imgui" FILES ${IMGUI_SOURCES})

target_include_directories(SupersonicEngine PRIVATE
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
target_compile_definitions(SupersonicEngine PRIVATE
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
    # PRIVATE on the target rather than in a header, because the vendored
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
)

if (MSVC)
    target_compile_options(SupersonicEngine PRIVATE /W4 /permissive-)
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(SupersonicEngine PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_link_libraries(SupersonicEngine PRIVATE
    Vulkan::Vulkan
    glfw
    EnTT::EnTT
    VulkanMemoryAllocator
    Threads::Threads
)

# Audio output. XAudio2 ships with Windows, so this adds no third-party
# dependency; other platforms compile AudioEngine's documented no-op path.
if (WIN32)
    target_link_libraries(SupersonicEngine PRIVATE xaudio2 ole32)
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
    "grid.vert=grid_vert.spv"
    "grid.frag=grid_frag.spv"
    "shadow.vert=shadow_vert.spv"
    "shadow.frag=shadow_frag.spv"
)

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
            DEPENDS "${_srcPath}"
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
option(SUPERSONIC_BUILD_TESTS "Build engine unit tests" ON)
if (SUPERSONIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
