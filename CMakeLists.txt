cmake_minimum_required(VERSION 3.20)
project(SupersonicEngine LANGUAGES C CXX)

# Objective-C and Objective-C++ on Apple platforms only: the iOS backend
# (src/platform/ios/) talks to UIKit, which has no C interface, and GLFW's
# Cocoa backend is Objective-C. Both, not only the one the engine writes: with
# OBJCXX enabled and OBJC not, CMake compiled GLFW's .m files as Objective-C++,
# where C's implicit conversions from void* are errors. Enabled here, where the
# engine is built for itself; a game that builds the engine as a subproject
# enables them in its own top directory as well (enable_language wants the
# highest directory common to everything that links the language).
if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Whether the engine is being built for itself, or as part of a game's build
# that added this directory with add_subdirectory(). The editor, the script
# plugin and the engine's own suites are the engine's business and default ON
# only in the first case; a game gets SupersonicCore, the test harness
# (cmake/SupersonicTesting.cmake) and nothing it did not ask for.
#
# PROJECT_IS_TOP_LEVEL is set by project() on CMake 3.21 and later. The minimum
# stays 3.20 rather than rising to claim it: raising it would also switch on
# 3.21's policies across the whole tree - CMP0126 changes what set(CACHE) does
# to a variable of the same name - in a change meant to alter nothing. An older
# CMake gets the comparison the variable stands for.
if (DEFINED PROJECT_IS_TOP_LEVEL)
    set(SUPERSONIC_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})
else()
    string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" SUPERSONIC_IS_TOP_LEVEL)
endif()

# The engine checkout's root, for a project building the engine as a
# subproject that has to name something in it - the working directory of a
# suite that reads the engine's assets, say. A cache entry because a
# directory's variables do not reach its parent.
set(SUPERSONIC_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL
    "Root of the Supersonic Engine checkout being built")

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
    set(SUPERSONIC_PLATFORM_DEFINITION SUPERSONIC_PLATFORM_ANDROID=1)
elseif (APPLE)
    if (IOS)
        message(STATUS "Targeting iOS platform (MoltenVK Metal)")
        set(SUPERSONIC_PLATFORM_DEFINITION SUPERSONIC_PLATFORM_IOS=1)
    else()
        message(STATUS "Targeting macOS platform (MoltenVK Metal)")
        set(SUPERSONIC_PLATFORM_DEFINITION SUPERSONIC_PLATFORM_MACOS=1)
    endif()
elseif (WIN32)
    message(STATUS "Targeting Windows platform (Win32 Vulkan)")
    set(SUPERSONIC_PLATFORM_DEFINITION SUPERSONIC_PLATFORM_WINDOWS=1)
else()
    message(STATUS "Targeting Linux platform (X11/Wayland Vulkan)")
    set(SUPERSONIC_PLATFORM_DEFINITION SUPERSONIC_PLATFORM_LINUX=1)
endif()
# Directory-scoped, so it reaches every target in this tree, the vendored ones
# included. SupersonicCore also carries it PUBLIC (below), which is what takes
# it to a game that builds the engine as a subproject.
add_compile_definitions(${SUPERSONIC_PLATFORM_DEFINITION})

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

# APPLE ONLY, AND ONLY WHEN NAMED: MoltenVK linked into the program itself,
# with no Vulkan loader in front of it. The path to a libMoltenVK.a (a slice of
# the MoltenVK.xcframework from a KhronosGroup/MoltenVK release) or a
# libMoltenVK.dylib.
#
# iOS has no loader to link: an app carries its Vulkan implementation inside
# itself, and the xcframework is how MoltenVK ships for that. A macOS app
# bundle is simplest the same way - one library linked in, no ICD manifest for
# a loader to find at run time, no dylib whose install name has to be
# rewritten into the bundle. What is given up is the loader's layers, the
# validation layer among them, which no Apple build of this engine has had.
#
# Empty (the default), nothing changes: the SDK, then the loader found by
# name, exactly as below.
set(SUPERSONIC_MOLTENVK_LIBRARY "" CACHE FILEPATH
    "Apple only: link this MoltenVK (libMoltenVK.a or .dylib) directly instead of a Vulkan loader")
set(_supersonicMoltenVK FALSE)
if (APPLE AND SUPERSONIC_MOLTENVK_LIBRARY)
    set(_supersonicMoltenVK TRUE)
endif()

# Not on Android: the NDK sysroot ships vulkan.h and libvulkan.so, so
# FindVulkan succeeds there - and then vulkan.hpp, which only the SDK and the
# vendored headers carry, is missing from every file that includes it. Skipped,
# Android takes the fallback below: the vendored headers, and the loader found
# by name, which under the NDK toolchain is the sysroot's libvulkan.so for the
# API level being built.
if (NOT ANDROID AND NOT _supersonicMoltenVK)
    find_package(Vulkan QUIET)
endif()

if (_supersonicMoltenVK)
    message(STATUS "Vulkan: MoltenVK linked directly, no loader: ${SUPERSONIC_MOLTENVK_LIBRARY}")
    set(Vulkan_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers-1.3.290/include")
    # What MoltenVK's own README lists for linking it statically; a dylib
    # brings its own and the extra names cost nothing.
    set(_mvkFrameworks
        "-framework Metal" "-framework Foundation" "-framework QuartzCore"
        "-framework IOSurface" "-framework CoreGraphics")
    if (IOS)
        list(APPEND _mvkFrameworks "-framework UIKit")
    else()
        list(APPEND _mvkFrameworks "-framework IOKit" "-framework AppKit")
    endif()
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${SUPERSONIC_MOLTENVK_LIBRARY};${_mvkFrameworks}")
elseif (Vulkan_FOUND)
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
#
# Nor on iOS, which GLFW does not support either: its Apple backend is Cocoa,
# and src/platform/ios/ stands where it does, as src/platform/android/ does.
if (NOT ANDROID AND NOT IOS)
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
if (NOT ANDROID AND NOT IOS)
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
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/platform/android/")
endif()

# And the GLFW backends must not enter an Android one. src/platform/android/
# implements the same classes - Window, InputPolling, NativeWindowControl,
# Gamepads - over the NDK instead, so everything above them compiles unchanged.
if (ANDROID)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX
         "/src/platform/(Window|InputPolling|NativeWindowControl|Gamepads)\\.cpp$")
endif()

# The iOS backend the same way: src/platform/ios/ in an iOS build only, in
# place of the GLFW files. Its UIKit half is Objective-C++ (IOSApp.mm), which
# the glob above does not collect, so it is named; ARC manages its objects.
if (NOT IOS)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/platform/ios/")
endif()
if (IOS)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX
         "/src/platform/(Window|InputPolling|NativeWindowControl|Gamepads)\\.cpp$")
    set(_iosAppSource ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios/IOSApp.mm)
    list(APPEND ENGINE_SOURCES ${_iosAppSource})
    set_source_files_properties(${_iosAppSource} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
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

# The editor is the engine's own application, not something a game linking the
# library needs - see SUPERSONIC_IS_TOP_LEVEL above.
option(SUPERSONIC_BUILD_EDITOR "Build the editor executable, SupersonicEngine" ${SUPERSONIC_IS_TOP_LEVEL})
if (SUPERSONIC_BUILD_EDITOR)
    add_executable(SupersonicEngine ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    target_link_libraries(SupersonicEngine PRIVATE SupersonicCore)
endif()

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

# What the directory settings at the top give every target in this tree, given
# to the library's consumers as well, because a game that builds the engine as
# a subproject is not in this tree and inherits neither. The engine's headers
# are C++20, so a consumer compiling them as anything older fails on the first
# one; the platform definition keeps its compile lines the ones the games had
# while they were built in this tree.
target_compile_features(SupersonicCore PUBLIC cxx_std_20)
target_compile_definitions(SupersonicCore PUBLIC ${SUPERSONIC_PLATFORM_DEFINITION})

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
    # stb_vorbis, which decodes AudioClip::LoadOgg's files. The loader includes
    # only its declarations, so its warnings are confined to this one TU.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/StbVorbisImplementation.cpp
)

# PRIVATE, not PUBLIC: the engine's own warning level is the engine's business.
# Pushing /W4 down the link interface would hold every test target - and anyone
# who ever links this library - to it as well.
if (MSVC)
    target_compile_options(SupersonicCore PRIVATE /W4 /permissive-)
    if (SUPERSONIC_BUILD_EDITOR)
        target_compile_options(SupersonicEngine PRIVATE /W4 /permissive-)
    endif()
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(SupersonicCore PRIVATE -Wall -Wextra -Wpedantic)
    if (SUPERSONIC_BUILD_EDITOR)
        target_compile_options(SupersonicEngine PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    set_source_files_properties(${VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# THE ARITHMETIC IS SPELLED OUT, not left to the compiler: no contraction.
#
# The engine's determinism claim is cross-platform (decided 10 September 2026,
# docs/planning/2026-09-10-cross-platform-determinism.md), and DetMath.hpp gets
# it by using only operations IEEE 754 rounds the same way everywhere. An FMA
# fusing a*b+c rounds ONCE where the source says twice, and whether one is
# emitted depends on the target: GCC's GNU mode contracts by default whenever
# the target has FMA, and clang contracts within a statement. So it is off,
# PUBLIC so the inline math in the headers obeys it in every target that
# includes them. MSVC's /fp:precise already does not contract; it is written
# down so nobody wonders.
if (MSVC)
    target_compile_options(SupersonicCore PUBLIC /fp:precise)
else()
    target_compile_options(SupersonicCore PUBLIC -ffp-contract=off)
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
# simply absent on Android and iOS, where the target does not exist.
if (NOT ANDROID AND NOT IOS)
    target_link_libraries(SupersonicCore PUBLIC glfw)
endif()

# Clang's -Wpedantic reports every _Nullable and _Nonnull in vk_mem_alloc.h,
# which the renderer headers include - hundreds of lines per file about a
# header this engine does not maintain. Android says the same below; Apple's
# compiler is Clang as well.
if (APPLE)
    target_compile_options(SupersonicCore PUBLIC -Wno-nullability-extension)
endif()

# What stands where GLFW does on iOS (src/platform/ios/): UIKit's app, window
# and touches, a CAMetalLayer for MoltenVK to present into, GameController's
# pads and AVFoundation's audio session. GLFW's header, and only its header, for
# the key codes, as on Android. The engine defines main() here (IOSApp.mm) and
# enters the game through SupersonicMain, as android_main does.
if (IOS)
    target_include_directories(SupersonicCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4/include)
    target_link_libraries(SupersonicCore PUBLIC
        "-framework UIKit" "-framework QuartzCore" "-framework Metal"
        "-framework GameController" "-framework AVFoundation" "-framework Foundation")
endif()

# What stands where GLFW does on Android: the NDK's NativeActivity glue, its
# window and input, and the Vulkan loader.
#
#  - android_native_app_glue.c is the NDK's own source, compiled here rather
#    than by each game, because the engine is what implements android_main
#    (src/platform/android/AndroidApp.cpp) and a game is then an ordinary
#    SHARED library that links SupersonicCore. `-u ANativeActivity_onCreate`
#    keeps the glue's entry point, which nothing in the library names, from
#    being dropped by the linker.
#  - GLFW's HEADER, and only its header: Input.hpp's key codes are GLFW's by
#    value, and code on every platform - the engine's key tables, a game's
#    settings - spells them GLFW_KEY_*. No GLFW function is linked.
#  - VMA fetches its Vulkan entry points at run time. Statically bound it names
#    the 1.1+ functions (vkGetBufferMemoryRequirements2 and the rest), which the
#    NDK's libvulkan.so does not export below API 28, so an API-26 link fails.
#    VulkanDevice::initVMA hands it the two getters it starts from.
if (ANDROID)
    set(_ndkGlueDir "${ANDROID_NDK}/sources/android/native_app_glue")
    add_library(SupersonicAndroidGlue STATIC "${_ndkGlueDir}/android_native_app_glue.c")
    target_include_directories(SupersonicAndroidGlue PUBLIC "${_ndkGlueDir}")
    target_link_libraries(SupersonicAndroidGlue PUBLIC android log)
    set_source_files_properties("${_ndkGlueDir}/android_native_app_glue.c" PROPERTIES COMPILE_OPTIONS "-w")

    target_link_libraries(SupersonicCore PUBLIC SupersonicAndroidGlue android log)
    target_link_options(SupersonicCore INTERFACE "-u" "ANativeActivity_onCreate")
    target_include_directories(SupersonicCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4/include)
    target_compile_definitions(SupersonicCore PUBLIC
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
    # vk_mem_alloc.h spells its pointer annotations with clang's _Nonnull and
    # _Nullable wherever the compiler has them, which under -Wpedantic is
    # thousands of lines per build of "a Clang extension" about a header this
    # engine does not maintain.
    target_compile_options(SupersonicCore PUBLIC -Wno-nullability-extension)
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
    # Media Foundation decodes MP3 (AudioClip::LoadMp3) and, like XAudio2,
    # ships with Windows - so recorded audio in a compressed format costs no
    # third-party dependency either.
    target_link_libraries(SupersonicCore PRIVATE xaudio2 ole32 mfplat mfreadwrite mfuuid)
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
elseif (ANDROID)
    # AAudio is in the NDK from API 26, the minimum this target builds for, so
    # like XAudio2 on Windows it is unconditional. The same AudioMixer the ALSA
    # path uses does the summing, pulled from AAudio's data callback.
    message(STATUS "Audio backend: AAudio")
    target_compile_definitions(SupersonicCore PRIVATE SUPERSONIC_AUDIO_AAUDIO=1)
    target_link_libraries(SupersonicCore PRIVATE aaudio)
elseif (APPLE)
    # CoreAudio's output unit, pulled from the same AudioMixer: the default
    # output device on macOS, RemoteIO on iOS. Part of both systems, so like
    # XAudio2 and AAudio it is unconditional.
    message(STATUS "Audio backend: CoreAudio")
    target_compile_definitions(SupersonicCore PRIVATE SUPERSONIC_AUDIO_COREAUDIO=1)
    target_link_libraries(SupersonicCore PRIVATE "-framework AudioToolbox" "-framework CoreAudio")
endif()

# Assets are loaded with paths relative to the working directory, so make F5
# in Visual Studio launch from the project root rather than the build folder.
if (SUPERSONIC_BUILD_EDITOR)
    set_target_properties(SupersonicEngine PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    if (_isMultiConfig)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT SupersonicEngine)
    endif()
endif()

# Where a GAME finds the engine's runtime files - assets/shaders first of all -
# when it is run from a directory that does not hold them. ChooseAssetRoot in
# src/platform/ExecutablePath.hpp is the whole rule: the executable's own
# folder when it is packaged, then the working directory, then this.
#
# Empty when the engine is built for itself. Its editor and every game in its
# tree ran from this root, and a build that bakes no path behaves exactly as it
# did before the rule. When the engine is built as part of another project it
# defaults to this checkout, which is what lets a game in its own repository
# run from its own root, or from anywhere. A relative value is taken from here.
if (NOT DEFINED SUPERSONIC_ASSET_ROOT)
    if (SUPERSONIC_IS_TOP_LEVEL)
        set(SUPERSONIC_ASSET_ROOT "" CACHE PATH
            "Directory holding the engine's assets/, for a game run from elsewhere (empty: none)")
    else()
        set(SUPERSONIC_ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
            "Directory holding the engine's assets/, for a game run from elsewhere (empty: none)")
    endif()
endif()
if (SUPERSONIC_ASSET_ROOT)
    get_filename_component(_supersonicAssetRoot "${SUPERSONIC_ASSET_ROOT}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    message(STATUS "Engine asset root for games run from elsewhere: ${_supersonicAssetRoot}")
    # One translation unit reads it, so changing it rebuilds that one only.
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ExecutablePath.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS "SUPERSONIC_ASSET_ROOT=\"${_supersonicAssetRoot}\"")
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
    "screen_overlay.vert=screen_overlay_vert.spv"
    "screen_overlay.frag=screen_overlay_frag.spv"
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

    # Only the editor depends on it. A game building the engine as a
    # subproject therefore leaves the committed .spv alone unless it builds
    # this target by name - they sit in the engine's source tree, which is the
    # game's submodule.
    add_custom_target(Shaders DEPENDS ${SPIRV_OUTPUTS})
    if (SUPERSONIC_BUILD_EDITOR)
        add_dependencies(SupersonicEngine Shaders)
    endif()
else()
    message(WARNING
        "No GLSL compiler found (glslc/glslangValidator). The committed .spv blobs in "
        "assets/shaders will be used as-is and will NOT be regenerated if you edit the "
        "GLSL sources. Install the Vulkan SDK to enable shader compilation.")
endif()

# ---------------------------------------------------------------------------
# Hot-reloadable script plugin
# ---------------------------------------------------------------------------
option(SUPERSONIC_BUILD_SCRIPT_PLUGIN "Build the sample hot-reload script plugin" ${SUPERSONIC_IS_TOP_LEVEL})
if (SUPERSONIC_BUILD_SCRIPT_PLUGIN)
    add_subdirectory(plugins)
    # Not a link dependency - the engine loads it at runtime and tolerates its
    # absence - but building it by default makes the feature work out of the box.
    if (SUPERSONIC_BUILD_EDITOR)
        add_dependencies(SupersonicEngine GameScripts)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# The rule every suite is built by, and the harness it includes. Unconditional:
# a game that builds the engine as a subproject turns the engine's suites off
# and builds its own with these.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SupersonicTesting.cmake)

# ---------------------------------------------------------------------------
# Games built on the engine
# ---------------------------------------------------------------------------
# None live here. Each game is its own repository and builds this one as a
# subproject, from a submodule pinned to a commit - Wolf Brigade in
# The-Wolf-Brigade/supersonic, HUSK in Test-Game/supersonic and Magic Portals
# in Magic-Portals-Remake. README.md, "Building a game against the engine",
# is what they build against.


# The engine's own suites. Off by default when the engine is built as part of
# another project, whose ctest would otherwise run them as its own.
option(SUPERSONIC_BUILD_TESTS "Build engine unit tests" ${SUPERSONIC_IS_TOP_LEVEL})
if (SUPERSONIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
