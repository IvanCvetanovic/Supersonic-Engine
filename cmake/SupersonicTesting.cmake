# How a suite is built against the engine: the engine's own suites, and a
# game's when the game builds the engine as a subproject.
#
# Included by the top-level CMakeLists.txt whether or not the engine's own
# suites are built. A game that adds the engine with add_subdirectory() gets
# SUPERSONIC_BUILD_TESTS off by default and builds ITS suites by this same rule,
# so neither side keeps a copy that could drift from the other.
#
#   Supersonic::TestHarness    INTERFACE target: tests/TestHarness.hpp on the
#                              include path. Header-only, standard library only.
#   supersonic_add_test(name)  one suite: <name>.cpp in the calling directory,
#                              linked against the harness and SupersonicCore,
#                              at /W4 (-Wall -Wextra elsewhere), and registered
#                              with add_test under its own name.
#
# A game names neither the harness's directory nor the engine's include roots:
# the first is this target's business and the second arrives through
# SupersonicCore's PUBLIC interface.

get_filename_component(_supersonicTestingEngineDir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(SupersonicTestHarness INTERFACE)
add_library(Supersonic::TestHarness ALIAS SupersonicTestHarness)
target_include_directories(SupersonicTestHarness INTERFACE "${_supersonicTestingEngineDir}/tests")

function(supersonic_add_test name)
    # ${ARGN} is accepted and ignored so a stale extra-sources argument is a
    # no-op rather than a configure error. Nothing should pass one any more.
    add_executable(${name} ${name}.cpp)

    # The harness, and then everything else - the src/ include root, the
    # vendored header paths, the GLM configuration that MUST match the engine's,
    # the C++ standard, and Threads - through SupersonicCore's PUBLIC interface.
    # That is the point: the ODR constraint on GLM_FORCE_DEPTH_ZERO_TO_ONE is
    # enforced by the build graph instead of by a duplicated block kept in step
    # by hand, and a suite in a game's repository is held to it exactly as the
    # engine's own suites are.
    target_link_libraries(${name} PRIVATE Supersonic::TestHarness SupersonicCore)

    if (MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()

    # SUPERSONIC_WERROR is read when a suite is registered, so a game that sets it
    # holds its own suites to it; off by default (CMakeLists.txt says why).
    if (SUPERSONIC_WERROR)
        if (MSVC)
            target_compile_options(${name} PRIVATE /WX)
        else()
            target_compile_options(${name} PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()
