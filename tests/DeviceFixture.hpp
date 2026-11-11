#pragma once

// A real Vulkan device for the suites that need one.
//
// Most of this engine's suites are pure logic and run anywhere. The code that
// talks to a driver - uploads, registries, the pipeline cache - had none, and the
// hardening pass says so in as many words: fixes to it "were made from reading the
// code". This is the way in: a hidden window, an instance, a device and a command
// pool, built the way the app builds them.
//
// WHEN THERE IS NO DEVICE it does not fail. Create() returns null and says why, and
// a suite built on it prints "SKIPPED: ..." and exits with code 77, which ctest
// lists as Skipped. A machine with no GPU and no display is the ordinary case (a
// CI runner, a container), and a suite that went red there would be switched off.
// It runs where there is something to run it on: a desktop, or a Linux machine
// with Mesa's lavapipe (a Vulkan device in software) under Xvfb - which is what
// CI's validation job already installs.
//
// Not for pixels. Two different drivers disagree about the last bit of a filtered
// sample, so what a suite checks here is the engine's own behaviour - which id a
// refusal answers with, how many slots a replace leaves, whether an upload reads
// back - not how a particular GPU rounds.

#include "platform/Window.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/VulkanDevice.hpp"

#include <exception>
#include <memory>
#include <string>

namespace Supersonic::test {

class DeviceFixture {
public:
    std::unique_ptr<Window> window;
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<VulkanDevice> device;
    vk::CommandPool pool{nullptr};

    // Null with `whyNot` filled when there is no usable device.
    static std::unique_ptr<DeviceFixture> Create(std::string& whyNot) {
        std::unique_ptr<DeviceFixture> fixture(new DeviceFixture);
        try {
            // Hidden, and small: it exists only to give the device a surface to
            // pick a queue family against.
            fixture->window = std::make_unique<Window>(64, 64, "supersonic-device-test", false);
            fixture->context = std::make_unique<VulkanContext>(fixture->window->GetRequiredExtensions());
            fixture->device = std::make_unique<VulkanDevice>(fixture->context->GetInstance(),
                                                             *fixture->window);

            vk::CommandPoolCreateInfo info{};
            info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            info.queueFamilyIndex = fixture->device->FindQueueFamilies().graphicsFamily.value();
            fixture->pool = fixture->device->GetDevice().createCommandPool(info);
        } catch (const std::exception& error) {
            whyNot = error.what();
            return nullptr;
        } catch (...) {
            whyNot = "an unknown error while creating the device";
            return nullptr;
        }
        return fixture;
    }

    ~DeviceFixture() {
        // The pool before the device that made it. The members then go in reverse
        // order of declaration: device, instance, window.
        if (pool && device) {
            device->GetDevice().waitIdle();
            device->FlushDeferredDestroys();
            device->GetDevice().destroyCommandPool(pool);
        }
    }

private:
    DeviceFixture() = default;
};

} // namespace Supersonic::test
