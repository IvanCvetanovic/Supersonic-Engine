#pragma once

// A real Vulkan device for the suites that need one.
//
// Most of this engine's suites are pure logic and run anywhere. The code that
// talks to a driver - uploads, registries, the pipeline cache - had none, and the
// hardening pass says so in as many words: fixes to it "were made from reading the
// code". This is the way in: a hidden window, an instance, a device and a command
// pool, built the way the app builds them.
//
// WHEN THE MACHINE HAS NO DEVICE it does not fail. Create() returns null, says why,
// and says it is the machine's doing; a suite built on it prints "SKIPPED: ..." and
// exits with code 77, which ctest lists as Skipped. A machine with no GPU and no
// display is the ordinary case (a CI runner, a container), and a suite that went red
// there would be switched off. It runs where there is something to run it on: a
// desktop, or a Linux machine with Mesa's lavapipe (a Vulkan device in software)
// under Xvfb - which is what CI's validation job already installs.
//
// WHEN THERE IS A DEVICE AND THE ENGINE FAILS TO USE IT, that is a failure, not a
// skip. Create() tells the two apart by stage: no display, no Vulkan loader or
// driver, no GPU the engine accepts are the machine; a failed vkCreateDevice or
// allocator, a missing queue family, a surface that will not make are the engine.
// Telling them apart matters because ctest reports Skipped with exit 0: were every
// exception a skip, a change that made device creation throw everywhere would turn
// this suite Skipped everywhere and nothing would go red. An exception it does not
// recognise is a failure. SUPERSONIC_REQUIRE_DEVICE (see test_device.cpp) makes the
// machine's own absence a failure too, for a machine that is meant to have a device.
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

    // Null when the fixture could not be made, with `why` filled. `machine` is true
    // when it is the MACHINE that has nothing to run on (a skip) and false when a
    // device is there and the engine failed to use it (a failure); see above.
    static std::unique_ptr<DeviceFixture> Create(std::string& why, bool& machine) {
        std::unique_ptr<DeviceFixture> fixture(new DeviceFixture);
        machine = false;

        // Stage 1: a window. Hidden, and small: it exists only to give the device a
        // surface to pick a queue family against. No display, or a platform that
        // cannot make one, is the machine.
        try {
            fixture->window = std::make_unique<Window>(64, 64, "supersonic-device-test", false);
        } catch (const std::exception& error) {
            why = std::string("no window: ") + error.what();
            machine = true;
            return nullptr;
        }

        // Stage 2: an instance. "No driver" is the machine; any other result (a missing
        // extension the engine asks for, a layer it cannot enable) is the engine.
        try {
            fixture->context = std::make_unique<VulkanContext>(fixture->window->GetRequiredExtensions());
        } catch (const std::exception& error) {
            why = error.what();
            const std::string text = why;
            machine = text.find("ErrorIncompatibleDriver") != std::string::npos ||
                      text.find("ErrorInitializationFailed") != std::string::npos ||
                      // GLFW's own words when the loader finds no ICD at all.
                      text.find("no Vulkan loader or ICD is installed") != std::string::npos;
            return nullptr;
        }

        // Stage 3: the device. The two refusals the constructor makes when there is no
        // GPU, or none it can use, are the machine; everything else it throws (the
        // surface, vkCreateDevice, the allocator) is not.
        try {
            fixture->device = std::make_unique<VulkanDevice>(fixture->context->GetInstance(),
                                                             *fixture->window);
        } catch (const std::exception& error) {
            why = error.what();
            const std::string text = why;
            machine = text.find("Failed to find GPUs with Vulkan support") != std::string::npos ||
                      text.find("Failed to find a suitable Vulkan physical GPU") != std::string::npos;
            return nullptr;
        }

        // Stage 4: a command pool. A device that cannot make one is the engine's.
        try {
            vk::CommandPoolCreateInfo info{};
            info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            info.queueFamilyIndex = fixture->device->FindQueueFamilies().graphicsFamily.value();
            fixture->pool = fixture->device->GetDevice().createCommandPool(info);
        } catch (const std::exception& error) {
            why = std::string("the device was created but a command pool was not: ") + error.what();
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
