# HUSK - the Rust RTS, on the engine.
#
# Three pieces, split the way Wolf Brigade's are:
#
#   sim/      the ported simulation. Pure logic, held to the Rust game's
#             per-tick hashes, and linked by the tests, so it is unconditional.
#   HuskGame  the layer that owns a sim World, steps it on the engine's tick and
#             draws it. A library, so a test can attach it to a bare registry.
#   Husk      the executable. Off by default: it links the engine the way any
#             game would, and building the engine should not mean building it.
#
# See docs/planning/2026-09-10-husk-port.md.

add_subdirectory(sim)

add_library(HuskGame STATIC
    HuskLayer.cpp
)

target_link_libraries(HuskGame PUBLIC SupersonicCore HuskSim)
target_include_directories(HuskGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(HuskGame PRIVATE /W4)
else()
    target_compile_options(HuskGame PRIVATE -Wall -Wextra)
endif()

if (NOT SUPERSONIC_BUILD_HUSK)
    return()
endif()

add_executable(Husk
    main.cpp
)

target_link_libraries(Husk PRIVATE HuskGame)

if (MSVC)
    target_compile_options(Husk PRIVATE /W4)
else()
    target_compile_options(Husk PRIVATE -Wall -Wextra)
endif()
