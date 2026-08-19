#include "renderer/VulkanDevice.hpp"
#include "core/Log.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <cstring>

namespace Supersonic {

VulkanDevice::VulkanDevice(vk::Instance instance, Window& window)
    : m_instance(instance) {
    if (!m_instance) {
        throw std::runtime_error("Cannot create VulkanDevice with null Vulkan Instance!");
    }

    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    initVMA();
}

VulkanDevice::~VulkanDevice() {
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    if (m_device) {
        m_device.destroy();
        m_device = nullptr;
    }

    if (m_surface && m_instance) {
        m_instance.destroySurfaceKHR(m_surface);
        m_surface = nullptr;
    }
}

void VulkanDevice::createSurface(Window& window) {
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    VkResult result = glfwCreateWindowSurface(
        static_cast<VkInstance>(m_instance),
        window.GetNativeWindow(),
        nullptr,
        &rawSurface
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GLFW window surface! VkResult: " + std::to_string(static_cast<int>(result)));
    }

    m_surface = rawSurface;
    SUPERSONIC_LOG_INFO("VulkanDevice") << "Vulkan Window Surface created successfully." << std::endl;
}

void VulkanDevice::pickPhysicalDevice() {
    std::vector<vk::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    vk::PhysicalDevice selectedGPU = nullptr;
    bool foundDiscreteGPU = false;

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            auto properties = device.getProperties();
            if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                selectedGPU = device;
                foundDiscreteGPU = true;
                break;
            } else if (!selectedGPU) {
                selectedGPU = device;
            }
        }
    }

    if (!selectedGPU) {
        throw std::runtime_error("Failed to find a suitable Vulkan physical GPU!");
    }

    m_physicalDevice = selectedGPU;
    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    auto properties = m_physicalDevice.getProperties();
    SUPERSONIC_LOG_INFO("VulkanDevice") << "Selected Physical GPU: " << properties.deviceName
              << " (Type: " << (foundDiscreteGPU ? "Discrete GPU" : "Integrated/Other GPU") << ")"
              << std::endl;
}

bool VulkanDevice::isDeviceSuitable(vk::PhysicalDevice device) {
    // VMA is configured for Vulkan 1.2 and statically binds the 1.1 core entry
    // points on that basis, asserting they are non-null. A 1.2 loader in front
    // of a 1.0/1.1-only physical device (real on older iGPUs, and on the Android
    // target this project advertises) would satisfy vkCreateInstance and then
    // fail inside VMA, so the device's own apiVersion has to be checked.
    const vk::PhysicalDeviceProperties properties = device.getProperties();
    if (properties.apiVersion < kRequiredApiVersion) {
        SUPERSONIC_LOG_INFO("VulkanDevice") << "Skipping " << properties.deviceName
                  << ": reports Vulkan "
                  << VK_API_VERSION_MAJOR(properties.apiVersion) << "."
                  << VK_API_VERSION_MINOR(properties.apiVersion)
                  << ", engine requires "
                  << VK_API_VERSION_MAJOR(kRequiredApiVersion) << "."
                  << VK_API_VERSION_MINOR(kRequiredApiVersion) << "." << std::endl;
        return false;
    }

    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool VulkanDevice::checkDeviceExtensionSupport(vk::PhysicalDevice device) {
    std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(vk::PhysicalDevice device) const {
    QueueFamilyIndices indices;

    std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        vk::Bool32 presentSupport = device.getSurfaceSupportKHR(i, m_surface);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

SwapChainSupportDetails VulkanDevice::querySwapChainSupport(vk::PhysicalDevice device) const {
    SwapChainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(m_surface);
    details.formats = device.getSurfaceFormatsKHR(m_surface);
    details.presentModes = device.getSurfacePresentModesKHR(m_surface);
    return details;
}

vk::Format VulkanDevice::FindSupportedFormat(
    const std::vector<vk::Format>& candidates,
    vk::ImageTiling tiling,
    vk::FormatFeatureFlags features) const {

    for (vk::Format format : candidates) {
        vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}

vk::Format VulkanDevice::FindDepthFormat() const {
    return FindSupportedFormat(
        { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

void VulkanDevice::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    vk::PhysicalDeviceFeatures deviceFeatures{};

    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    m_device = m_physicalDevice.createDevice(createInfo);
    SUPERSONIC_LOG_INFO("VulkanDevice") << "Vulkan Logical Device created successfully." << std::endl;

    m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
    m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
}

void VulkanDevice::initVMA() {
    VmaAllocatorCreateInfo allocatorCreateInfo{};
    // Guaranteed safe: isDeviceSuitable rejected anything below this version.
    allocatorCreateInfo.vulkanApiVersion = kRequiredApiVersion;
    allocatorCreateInfo.instance = static_cast<VkInstance>(m_instance);
    allocatorCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
    allocatorCreateInfo.device = static_cast<VkDevice>(m_device);

    VkResult result = vmaCreateAllocator(&allocatorCreateInfo, &m_allocator);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Vulkan Memory Allocator (VMA)! VkResult: " + std::to_string(static_cast<int>(result)));
    }

    SUPERSONIC_LOG_INFO("VulkanDevice") << "VmaAllocator initialized successfully." << std::endl;
}


vk::SampleCountFlagBits VulkanDevice::GetMaxUsableSampleCount(vk::SampleCountFlagBits cap) const {
    if (!m_physicalDevice) return vk::SampleCountFlagBits::e1;

    const vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties();
    const vk::SampleCountFlags counts = properties.limits.framebufferColorSampleCounts &
                                        properties.limits.framebufferDepthSampleCounts;

    // Descending, so the first hit is the best the device and the cap agree on.
    const vk::SampleCountFlagBits ladder[] = {
        vk::SampleCountFlagBits::e64, vk::SampleCountFlagBits::e32,
        vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
        vk::SampleCountFlagBits::e4,  vk::SampleCountFlagBits::e2,
    };
    for (const vk::SampleCountFlagBits bit : ladder) {
        if (static_cast<uint32_t>(bit) > static_cast<uint32_t>(cap)) continue;
        if (counts & bit) return bit;
    }
    return vk::SampleCountFlagBits::e1;
}

} // namespace Supersonic
