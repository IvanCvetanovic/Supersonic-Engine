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
    GestureMachine.cpp
)

target_include_directories(WolfBrigadeSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(WolfBrigadeSim PUBLIC SupersonicCore)

if (MSVC)
    target_compile_options(WolfBrigadeSim PRIVATE /W4)
else()
    target_compile_options(WolfBrigadeSim PRIVATE -Wall -Wextra)
endif()
