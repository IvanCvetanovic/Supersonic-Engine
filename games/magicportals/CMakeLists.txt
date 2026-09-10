# Magic Portals - the Godot remake, on the engine.
#
#   sim/    the port's logic: the level reader, the level builder, the portal
#           arithmetic. Linked by the tests, so it is unconditional.
#   spike/  a measurement tool - not a game and not a test. It runs level30
#           through the engine's solver and prints the numbers the spike doc
#           records, each beside a threshold written down before it ran. Off by
#           default, like the other games' executables.
#
# See docs/planning/2026-09-10-magic-portals-spike.md.

add_subdirectory(sim)

if (NOT SUPERSONIC_BUILD_MAGICPORTALS)
    return()
endif()

add_executable(MagicPortalsSpike
    spike/main.cpp
)

target_link_libraries(MagicPortalsSpike PRIVATE MagicPortalsSim)

if (MSVC)
    target_compile_options(MagicPortalsSpike PRIVATE /W4)
else()
    target_compile_options(MagicPortalsSpike PRIVATE -Wall -Wextra)
endif()
