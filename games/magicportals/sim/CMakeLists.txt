# Magic Portals' simulation side, ported from the Godot remake.
#
# A library rather than part of an executable, and unconditional, for the reason
# Wolf Brigade's and HUSK's are: the tests link it. Deliberately free of the
# renderer. See docs/planning/2026-09-10-magic-portals-spike.md.

add_library(MagicPortalsSim STATIC
    Art.cpp
    Boss.cpp
    Camera.cpp
    Chapters.cpp
    Demolish.cpp
    Game.cpp
    Goals.cpp
    Hazards.cpp
    Launchers.cpp
    LevelBuilder.cpp
    Mover.cpp
    Particles.cpp
    Player.cpp
    Portal.cpp
    Portals.cpp
    Prism.cpp
    Puzzle.cpp
    Roles.cpp
    Shot.cpp
    Sounds.cpp
    Sprites.cpp
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

# And the level order, chapters.json: the converter's reading of the original's
# goldenScores.enml, written beside the levels in the same gitignored out/.
set(SUPERSONIC_MAGICPORTALS_CHAPTERS "${SUPERSONIC_MAGICPORTALS_LEVELS}/../data/chapters.json"
    CACHE FILEPATH "The remake's converted level order (Magic-Portals-Remake/out/data/chapters.json)")

# The port's own data, which IS in this repository: numbers authored for the
# port, such as how much of a level the view shows (view.json).
# And the original's own assets, extracted from its APK beside the remake: the
# images of what no level places - the portals a shot opens, the shot, the
# player (Art.hpp). Like the levels they are the original's, and stay outside
# this repository; drawn without them, those are boxes.
# The default follows the levels, which the converter writes to the same
# remake's out/levels.
set(SUPERSONIC_MAGICPORTALS_ORIGINAL "${SUPERSONIC_MAGICPORTALS_LEVELS}/../../reference/extracted/assets"
    CACHE PATH "The original's extracted assets (Magic-Portals-Remake/reference/extracted/assets)")

target_compile_definitions(MagicPortalsSim PUBLIC
    MAGICPORTALS_ORIGINAL_DIR="${SUPERSONIC_MAGICPORTALS_ORIGINAL}"
    MAGICPORTALS_LEVELS_DIR="${SUPERSONIC_MAGICPORTALS_LEVELS}"
    MAGICPORTALS_DATA_DIR="${SUPERSONIC_MAGICPORTALS_DATA}"
    MAGICPORTALS_CHAPTERS_FILE="${SUPERSONIC_MAGICPORTALS_CHAPTERS}"
    MAGICPORTALS_PORT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")

target_include_directories(MagicPortalsSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(MagicPortalsSim PUBLIC SupersonicCore)

if (MSVC)
    target_compile_options(MagicPortalsSim PRIVATE /W4)
else()
    target_compile_options(MagicPortalsSim PRIVATE -Wall -Wextra)
endif()
