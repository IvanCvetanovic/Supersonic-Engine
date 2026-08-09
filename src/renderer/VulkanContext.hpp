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
    VmaAllocator GetAllocator() const { return m_allocator; }

private:
    void createInstance(const std::vector<const char*>& windowExtensions);
    void setupDebugMessenger();
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions(const std::vector<const char*>& windowExtensions);

    vk::Instance m_instance{nullptr};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    
    // Placeholder VMA Allocator handle (initialized when physical/logical device is ready)
    VmaAllocator m_allocator{VK_NULL_HANDLE};

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
