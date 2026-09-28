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
#if SUPERSONIC_WINDOW_GLFW
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
#else
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    const VkResult result = window.CreateSurface(static_cast<VkInstance>(m_instance), &rawSurface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create the native window surface! VkResult: " + std::to_string(static_cast<int>(result)));
    }
#endif

    m_surface = rawSurface;
    SUPERSONIC_LOG_INFO("VulkanDevice") << "Vulkan Window Surface created successfully." << std::endl;
}

#if !SUPERSONIC_WINDOW_GLFW
void VulkanDevice::DestroySurface() {
    if (m_surface && m_instance) {
        m_instance.destroySurfaceKHR(m_surface);
        m_surface = nullptr;
        SUPERSONIC_LOG_INFO("VulkanDevice") << "Window surface released with its window.";
    }
}

void VulkanDevice::RecreateSurface(Window& window) {
    DestroySurface();
    createSurface(window);

    // The queue chosen to present was chosen for the FIRST surface. Every
    // Android device presents from its graphics queue, but a device that
    // stopped presenting to the new one should say so here rather than fail at
    // the first vkQueuePresentKHR with a message about something else.
    const vk::Bool32 presentable = m_physicalDevice.getSurfaceSupportKHR(
        m_queueFamilyIndices.presentFamily.value(), m_surface);
    if (!presentable) {
        SUPERSONIC_LOG_ERROR("VulkanDevice") << "The present queue cannot present to the new window's surface.";
    }
}
#endif

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

    // Nothing was ever enabled here. A zeroed PhysicalDeviceFeatures was passed
    // straight to createDevice, so every optional capability was off - which is
    // why every texture in the engine was sampled at mip 0 with no anisotropy.
    // The engine paid for 4x MSAA and then discarded the filtering that matters
    // more at a glancing angle: MSAA anti-aliases geometry edges, not texture
    // minification, so a textured floor shimmered and no amount of MSAA fixed it.
    //
    // Only what is used is requested. Enabling a feature the engine does not
    // exercise is not free - it can change driver behaviour and it fails device
    // creation outright where unsupported - so depthClamp, fillModeNonSolid,
    // multiDrawIndirect and textureCompressionBC stay off until something needs
    // them. m_features records what the device offered, so a caller can ask.
    m_features = m_physicalDevice.getFeatures();

    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = m_features.samplerAnisotropy;

    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

#if defined(__APPLE__)
    // A device that advertises VK_KHR_portability_subset (MoltenVK does) must
    // have it enabled by whoever creates it: it is how the application says it
    // knows the device is not full Vulkan. MoltenVK works without it; the
    // validation layer reports it on every device creation. The name is spelled
    // out because its macro is in vulkan_beta.h.
    std::vector<const char*> appleExtensions = m_deviceExtensions;
    for (const vk::ExtensionProperties& extension : m_physicalDevice.enumerateDeviceExtensionProperties()) {
        if (std::strcmp(extension.extensionName.data(), "VK_KHR_portability_subset") == 0) {
            appleExtensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }
    createInfo.enabledExtensionCount = static_cast<uint32_t>(appleExtensions.size());
    createInfo.ppEnabledExtensionNames = appleExtensions.data();
#endif

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

#if defined(__ANDROID__)
    // VMA fetches its entry points at run time on Android (CMakeLists.txt says
    // why), starting from these two.
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
    allocatorCreateInfo.pVulkanFunctions = &functions;
#endif

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


void VulkanDevice::DeferDestroy(std::function<void()> deleter) {
    if (!deleter) return;
    m_pendingDestroys.push_back({ m_frameNumber, std::move(deleter) });
}

void VulkanDevice::CollectGarbage(uint64_t completedFrame) {
    // Stable partition rather than erase-in-loop: a deleter can, in principle,
    // queue another destroy, and iterating a vector being appended to is how
    // that turns into a dangling iterator.
    std::vector<PendingDestroy> keep;
    keep.reserve(m_pendingDestroys.size());

    std::vector<PendingDestroy> run;
    for (auto& pending : m_pendingDestroys) {
        if (pending.frame <= completedFrame) {
            run.push_back(std::move(pending));
        } else {
            keep.push_back(std::move(pending));
        }
    }
    m_pendingDestroys = std::move(keep);

    for (auto& pending : run) {
        pending.deleter();
    }
}

void VulkanDevice::FlushDeferredDestroys() {
    std::vector<PendingDestroy> all = std::move(m_pendingDestroys);
    m_pendingDestroys.clear();
    for (auto& pending : all) {
        pending.deleter();
    }
}

} // namespace Supersonic
