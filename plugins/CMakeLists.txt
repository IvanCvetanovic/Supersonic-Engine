# Hot-reloadable script plugins.
#
# Built as a separate shared library so it can be relinked while the engine is
# running. HotReloadEngine shadow-copies the output before loading it, which is
# what keeps the file unlocked for the next build on Windows.

add_library(GameScripts SHARED sample_scripts/SampleScripts.cpp)

# Only the C ABI header is needed - deliberately not the engine's include tree,
# so a plugin cannot accidentally depend on engine internals whose layout is not
# part of the contract.
target_include_directories(GameScripts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set_target_properties(GameScripts PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    PREFIX ""
)

# Land next to the executable, where the engine looks for it first.
#
# The editor executable has no output directory of its own, so it lands in
# ${CMAKE_BINARY_DIR} with a single-config generator (Ninja, Makefiles) and in
# ${CMAKE_BINARY_DIR}/<Config> with a multi-config one (Visual Studio, Xcode). The
# plugin was always put in the second place, which for Visual Studio is beside the
# executable and for everything else is one directory too deep - found only by the
# engine's fallback, a search of build/Release and build/Debug under the working
# directory, so hot reload worked only when the build directory was named exactly
# "build" and the engine was launched from the project root.
if (CMAKE_CONFIGURATION_TYPES)
    set_target_properties(GameScripts PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY             "${CMAKE_BINARY_DIR}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG       "${CMAKE_BINARY_DIR}/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE     "${CMAKE_BINARY_DIR}/Release"
        LIBRARY_OUTPUT_DIRECTORY             "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
else()
    set_target_properties(GameScripts PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY             "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY             "${CMAKE_BINARY_DIR}"
    )
endif()

if (MSVC)
    target_compile_options(GameScripts PRIVATE /W4)
else()
    target_compile_options(GameScripts PRIVATE -Wall -Wextra)
endif()
