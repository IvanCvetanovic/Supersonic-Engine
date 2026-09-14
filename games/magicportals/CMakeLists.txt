# Magic Portals - the Godot remake, on the engine.
#
#   sim/              the port's logic: the level reader and builder, the portal
#                     arithmetic, the player, buttons, doors, crystals and portals,
#                     and the tick they run on. Linked by the tests, so it is
#                     unconditional.
#   MagicPortalsGame  the layer that plays a level on the engine's tick and draws
#                     it. A library, so a test can attach it to a bare registry.
#   MagicPortals      the executable, and
#   spike/            a measurement tool - not a game and not a test. It runs
#                     level30 through the engine's solver and prints the numbers
#                     the spike doc records, each beside a threshold written down
#                     before it ran.
#   Both executables are off by default, like the other games'.
#
# See docs/planning/2026-09-10-magic-portals-port.md.

add_subdirectory(sim)

add_library(MagicPortalsGame STATIC
    MagicPortalsLayer.cpp
)

target_link_libraries(MagicPortalsGame PUBLIC SupersonicCore MagicPortalsSim)
target_include_directories(MagicPortalsGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(MagicPortalsGame PRIVATE /W4)
else()
    target_compile_options(MagicPortalsGame PRIVATE -Wall -Wextra)
endif()

if (NOT SUPERSONIC_BUILD_MAGICPORTALS)
    return()
endif()

add_executable(MagicPortals
    main.cpp
    LevelVisit.cpp   # DEV: --visit-levels, never pushed without it
)

target_link_libraries(MagicPortals PRIVATE MagicPortalsGame)

add_executable(MagicPortalsSpike
    spike/main.cpp
)

target_link_libraries(MagicPortalsSpike PRIVATE MagicPortalsSim)

if (MSVC)
    target_compile_options(MagicPortals PRIVATE /W4)
    target_compile_options(MagicPortalsSpike PRIVATE /W4)
else()
    target_compile_options(MagicPortals PRIVATE -Wall -Wextra)
    target_compile_options(MagicPortalsSpike PRIVATE -Wall -Wextra)
endif()
