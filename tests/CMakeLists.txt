# Pure-logic regression tests.
#
# Each test compiles the engine sources it needs directly rather than linking a
# library target, because the engine builds as an executable. None of the code
# under test issues Vulkan calls, so only the headers are required.

set(ENGINE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(add_engine_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${ENGINE_SRC}/src
        ${ENGINE_SRC}/tests
        ${Vulkan_INCLUDE_DIRS}
        ${ENGINE_SRC}/third_party/glm
        ${ENGINE_SRC}/third_party/entt-3.13.2/src
    )
    target_compile_definitions(${name} PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
    )
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_engine_test(test_transform)
