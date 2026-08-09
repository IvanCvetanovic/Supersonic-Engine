# Engine Architecture & Architectural Manifesto

## Manifesto
> **This engine strictly forbids traditional Object-Oriented inheritance for game logic. All game state must be stored in flat EnTT components, and all logic must be executed by pure stateless Systems. Vulkan memory is managed exclusively by VMA.**

---

## Core Principles

### 1. Data-Oriented Design (DOD) & Entity Component System (ECS)
- **Entities**: Lightweight 32-bit identifiers (`entt::entity`). Entities possess zero behavior and zero state beyond their ID.
- **Components**: Plain-Old-Data (POD) structs containing flat, cache-friendly data layout. No virtual functions, no business logic, no inheritance hierarchies.
- **Systems**: Free functions or stateless logic blocks operating over EnTT registry views (`registry.view<ComponentA, ComponentB>()`). Systems process contiguous arrays of components for maximum CPU cache utilization.

### 2. Vulkan Rendering Subsystem
- **API**: Vulkan (`vulkan/vulkan.hpp` C++ bindings).
- **Validation Layers**: `VK_LAYER_KHRONOS_validation` enabled on all Debug builds via `VkDebugUtilsMessengerEXT` callback.
- **Windowing**: GLFW configured specifically for Vulkan (`GLFW_NO_API`). Legacy OpenGL context creation is strictly prohibited.
- **Memory Management**: Vulkan GPU memory allocations are managed exclusively via AMD Vulkan Memory Allocator (`VmaAllocator` / VMA). Manual raw `vkAllocateMemory` calls are prohibited.
