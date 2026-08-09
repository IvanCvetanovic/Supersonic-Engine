cmake_minimum_required(VERSION 3.20)
project(GameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Vulkan setup
find_package(Vulkan QUIET)

if (NOT Vulkan_FOUND)
    message(STATUS "Using Vulkan-Headers from third_party and vulkan-1.dll from System32...")
    set(VULKAN_SYS_LIB "$ENV{SystemRoot}/System32/vulkan-1.dll")
    if (NOT EXISTS "${VULKAN_SYS_LIB}")
        message(FATAL_ERROR "Could not locate vulkan-1.dll in System32.")
    endif()

    add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers-1.3.290/include"
        IMPORTED_LOCATION "${VULKAN_SYS_LIB}"
    )
    set(Vulkan_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers-1.3.290/include")
endif()

# GLFW setup
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4)

# EnTT setup
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt-3.13.2)

# VMA setup
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator-3.1.0)

# Collect engine sources
file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(GameEngine ${ENGINE_SOURCES})

target_include_directories(GameEngine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf
)

target_link_libraries(GameEngine PRIVATE 
    Vulkan::Vulkan 
    glfw 
    EnTT::EnTT
    VulkanMemoryAllocator
)
