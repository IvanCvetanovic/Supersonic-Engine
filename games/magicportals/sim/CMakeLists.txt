# Magic Portals' simulation side, ported from the Godot remake.
#
# A library rather than part of an executable, and unconditional, for the reason
# Wolf Brigade's and HUSK's are: the tests link it. Deliberately free of the
# renderer. See docs/planning/2026-09-10-magic-portals-spike.md.

add_library(MagicPortalsSim STATIC
    Game.cpp
    Goals.cpp
    LevelBuilder.cpp
    Mover.cpp
    Player.cpp
    Portal.cpp
    Portals.cpp
    Prism.cpp
    Puzzle.cpp
    Roles.cpp
    Trigger.cpp
    Tscn.cpp
)

# Where the converted levels live - which is NOT in this repository.
#
# They are the original game's level data, decoded by the remake's converter
# into its gitignored out/ directory, and they stay on the machine that
# converted them. The default assumes the remake is cloned beside the engine;
# anywhere else is one -D away. Baked in as an absolute path for the reason
# WOLFBRIGADE_DATA_DIR is: ctest runs from the build tree, so a relative path
# would be right for nobody. The suites that read levels skip, and say where
# they looked, when it is not there - see tests/CMakeLists.txt.
set(SUPERSONIC_MAGICPORTALS_LEVELS "${CMAKE_SOURCE_DIR}/../Magic-Portals-Remake/out/levels"
    CACHE PATH "The remake's converted levels (Magic-Portals-Remake/out/levels)")

# And the remake's own data - its entity_roles.json, portals.json and
# player.json, which are the remake's authored files rather than the original's.
# The tests read the role table and pin what it says of a level (test_mp_play);
# the numbers in portals.json and player.json are marked _guess there, so they
# are carried as data and nothing pins them.
set(SUPERSONIC_MAGICPORTALS_DATA "${CMAKE_SOURCE_DIR}/../Magic-Portals-Remake/game/data"
    CACHE PATH "The remake's data directory (Magic-Portals-Remake/game/data)")

target_compile_definitions(MagicPortalsSim PUBLIC
    MAGICPORTALS_LEVELS_DIR="${SUPERSONIC_MAGICPORTALS_LEVELS}"
    MAGICPORTALS_DATA_DIR="${SUPERSONIC_MAGICPORTALS_DATA}")

target_include_directories(MagicPortalsSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(MagicPortalsSim PUBLIC SupersonicCore)

if (MSVC)
    target_compile_options(MagicPortalsSim PRIVATE /W4)
else()
    target_compile_options(MagicPortalsSim PRIVATE -Wall -Wextra)
endif()
