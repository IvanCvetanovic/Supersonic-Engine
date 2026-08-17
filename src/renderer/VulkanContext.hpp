#pragma once

#include <vector>
#include <iostream>
#include <string>

#include <vulkan/vulkan.hpp>

// VMA placeholder include (VMA implementation is in VulkanContext.cpp)
#include <vk_mem_alloc.h>

namespace Engine {

class VulkanContext {
public:
    explicit VulkanContext(const std::vector<const char*>& windowExtensions);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    vk::Instance GetInstance() const { return m_instance; }

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

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif
};

} // namespace Engine
