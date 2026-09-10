# Wolf Brigade's simulation, ported from GDScript.
#
# A LIBRARY rather than part of the executable, and built unconditionally rather
# than behind SUPERSONIC_BUILD_WOLFBRIGADE, for one reason: the tests link it.
# The port's whole verification story is that a C++ test reproduces the numbers
# the original's headless harness prints, and a simulation that only exists
# inside an executable nobody builds by default cannot be tested at all.
#
# It is also the line between the port and the SPIKE next door. WolfBrigadeLayer
# says of itself that everything in it is deliberately disposable; nothing here
# is, and nothing here may depend on it.
#
# Deliberately free of the renderer. This links SupersonicCore for glm, entt and
# the input types - not for a device - and the test suites that link it produce
# binaries that touch no Vulkan entry point, exactly as the engine's own do.

add_library(WolfBrigadeSim STATIC
    AudioTones.cpp
    BuildPlacement.cpp
    Building.cpp
    CapturePoint.cpp
    Commands.cpp
    DataValidator.cpp
    GameData.cpp
    GameState.cpp
    GestureMachine.cpp
    HeroControl.cpp
    HeroInput.cpp
    Lane.cpp
    Match.cpp
    Projectiles.cpp
    Selection.cpp
    Progression.cpp
    ResourceNode.cpp
    SaveIds.cpp
    Snapshot.cpp
    Unit.cpp
    UnitStats.cpp
    WaveDirector.cpp
)

# Where the ported data lives, as a compile definition rather than a path the
# test works out for itself.
#
# ctest runs from the build tree and the game's own harnesses run from the
# project root, so any relative path is right for exactly one of them. An
# absolute path baked in at configure time is right for both, and a test that
# cannot find its data fails loudly instead of validating an empty document and
# reporting zero issues.
target_compile_definitions(WolfBrigadeSim PUBLIC
    WOLFBRIGADE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")

target_include_directories(WolfBrigadeSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(WolfBrigadeSim PUBLIC SupersonicCore)

if (MSVC)
    target_compile_options(WolfBrigadeSim PRIVATE /W4)
else()
    target_compile_options(WolfBrigadeSim PRIVATE -Wall -Wextra)
endif()

# GCC 13's -Wdangling-reference guesses that a function returning a reference
# may be handing back one into a temporary argument. In DataValidator.cpp it is
# wrong six times over: AsObject, AsArray and field return a reference into the
# JSON document or to a function-local static, and the temporaries it sees are
# only a key or the context string an issue is reported under. GCC 14 can be
# told that per declaration with [[gnu::no_dangling]]; Ubuntu 24.04 - and so
# CI - has 13, so the one file is exempted rather than the whole library.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    set_source_files_properties(DataValidator.cpp PROPERTIES COMPILE_OPTIONS -Wno-dangling-reference)
endif()
