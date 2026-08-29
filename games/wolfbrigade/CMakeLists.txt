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

add_executable(WolfBrigade
    main.cpp
    WolfBrigadeLayer.cpp
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
target_link_libraries(WolfBrigade PRIVATE SupersonicCore WolfBrigadeSim)
target_include_directories(WolfBrigade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(WolfBrigade PRIVATE /W4)
else()
    target_compile_options(WolfBrigade PRIVATE -Wall -Wextra)
endif()
