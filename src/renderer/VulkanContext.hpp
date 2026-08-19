#pragma once

#include <vector>
#include <iostream>
#include <string>

#include <vulkan/vulkan.hpp>

// VMA placeholder include (VMA implementation is in VulkanContext.cpp)
#include <vk_mem_alloc.h>

// Whether to request VK_LAYER_KHRONOS_validation. CMake defines this; the
// fallback reproduces the old behaviour exactly for a build that does not.
//
// It is decoupled from NDEBUG on purpose. Validation used to be available only
// in Debug builds, and the Linux CI job builds Release deliberately - the
// runner's memory does not survive Debug's -O0 -g across forty translation
// units. Tying the layers to the build type therefore meant the one job that
// could have run them structurally could not.
#ifndef SUPERSONIC_ENABLE_VALIDATION
#  ifdef NDEBUG
#    define SUPERSONIC_ENABLE_VALIDATION 0
#  else
#    define SUPERSONIC_ENABLE_VALIDATION 1
#  endif
#endif

namespace Supersonic {

class VulkanContext {
public:
    explicit VulkanContext(const std::vector<const char*>& windowExtensions);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    vk::Instance GetInstance() const { return m_instance; }

    // How many ERROR-severity validation messages have been reported, process
    // wide. The messenger callback is invoked by the driver on whichever thread
    // made the offending call, so this is an atomic and not a plain counter.
    //
    // The point is the exit status. debugCallback printed to cerr and returned
    // VK_FALSE, so a run that emitted a hundred validation errors still exited
    // 0 and any CI step wrapping it passed. A message nobody's build fails on
    // is a message nobody reads.
    static unsigned ValidationErrorCount();
    static bool ValidationLayersActive();

    // Note: there is deliberately no GetAllocator() here. The VMA allocator
    // needs a physical and logical device, so it belongs to - and is owned by -
    // VulkanDevice. This class previously exposed a member that nothing ever
    // assigned, so every caller would have received VK_NULL_HANDLE.

private:
    void createInstance(const std::vector<const char*>& windowExtensions);
    void setupDebugMessenger();
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions(const std::vector<const char*>& windowExtensions);

    vk::Instance m_instance{nullptr};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const bool m_enableValidationLayers = SUPERSONIC_ENABLE_VALIDATION != 0;
};

} // namespace Supersonic
