# Wolf Brigade - Phase 0 spike.
#
# A game that LINKS SupersonicCore rather than one compiled into it. That is
# the point: the layer seam was argued for in EngineLayer.hpp and had never
# been used by anything, and "a game can live outside the engine" is a claim
# this target either proves or disproves.
#
# Off by default. It is a spike against a game that lives in another repository
# entirely, and nobody building the engine should have to build it.
#
# See docs/planning/2026-08-25-wolf-brigade-port.md.

# The layer, as a LIBRARY, built unconditionally - the same split and the same
# argument as WolfBrigadeSim next door.
#
# It is the seam between a verified simulation and a picture: it owns the match,
# steps it on the tick, and builds the HUD that reads it. Every one of those is
# a claim a test can check, and none of them could be checked while this file
# compiled straight into an executable that is off by default.
#
# The screenshot tool does not cover it either, and cannot: --screenshot writes
# the offscreen 3D target, while the UI is drawn over that image in ImGui's draw
# list. So a HUD is invisible to the one instrument that looks at pixels, which
# leaves a registry test as the instrument that reaches it.
add_library(WolfBrigadeGame STATIC
    WolfBrigadeLayer.cpp
)

target_link_libraries(WolfBrigadeGame PUBLIC SupersonicCore WolfBrigadeSim)
target_include_directories(WolfBrigadeGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(WolfBrigadeGame PRIVATE /W4)
else()
    target_compile_options(WolfBrigadeGame PRIVATE -Wall -Wextra)
endif()

# The executable stays gated: it is a spike against a game in another repository
# and nobody building the engine should have to build it.
if (NOT SUPERSONIC_BUILD_WOLFBRIGADE)
    return()
endif()

add_executable(WolfBrigade
    main.cpp
)

# Everything it needs - the src/ include root, the vendored headers, and the
# GLM configuration that MUST match the engine's - arrives through
# SupersonicCore's PUBLIC interface, exactly as the test suites get it.
#
# AND THE SIMULATION, which until now only the test suites linked. Four thousand
# lines of ported game, verified against twenty-two of the original's harnesses,
# and none of it was in the binary that runs: this target drew coloured quads
# from numbers it made up while the real match sat in a library beside it. The
# sim's include root and WOLFBRIGADE_DATA_DIR are PUBLIC on that target, so the
# data path arrives with it and nothing here has to guess where the JSON lives.
target_link_libraries(WolfBrigade PRIVATE WolfBrigadeGame)

if (MSVC)
    target_compile_options(WolfBrigade PRIVATE /W4)
else()
    target_compile_options(WolfBrigade PRIVATE -Wall -Wextra)
endif()
