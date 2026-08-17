// Single translation unit that compiles the Vulkan Memory Allocator.
//
// VMA is header-only and its implementation emits a large number of warnings
// under /W4. Keeping it in its own TU means those can be silenced in
// CMakeLists.txt without also silencing warnings in first-party code, which is
// what happened while VMA_IMPLEMENTATION lived in VulkanContext.cpp.

#include <vulkan/vulkan.hpp>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
