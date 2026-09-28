// platform/Window.hpp's native-surface Window, over the CAMetalLayer of the
// view UIKit shows (IOSApp.mm), through MoltenVK's VK_EXT_metal_surface.

#include "platform/Window.hpp"

#include <stdexcept>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include "core/Log.hpp"
#include "platform/ios/IOSApp.hpp"

namespace Supersonic {

Window::Window(int width, int height, const std::string& title) {
    (void)width;
    (void)height;
    if (IOS::CurrentLayer() == nullptr) {
        throw std::runtime_error("iOS: the app has no view to draw on.");
    }
    int w = 0;
    int h = 0;
    IOS::WindowSize(w, h);
    // Taken now, so the first frame does not read the view's first layout as a
    // resize and rebuild the swapchain it was just given.
    IOS::TakeWindowSizeChanged();
    SUPERSONIC_LOG_INFO("Window") << "iOS view: " << w << "x" << h << " pixels (\"" << title << "\")";
}

Window::~Window() {
    // The callback reaches into whoever set it, which goes with this window.
    IOS::SetSurfaceLostCallback(nullptr);
}

bool Window::ShouldClose() const { return IOS::DestroyRequested(); }

void Window::PollEvents() {
    IOS::WaitUntilDrawable();
    if (IOS::TakeWindowSizeChanged()) m_framebufferResized = true;
}

void* Window::GetNativeWindow() const { return IOS::CurrentLayer(); }

int Window::GetWidth() const {
    int width = 0;
    int height = 0;
    IOS::WindowSize(width, height);
    return width;
}

int Window::GetHeight() const {
    int width = 0;
    int height = 0;
    IOS::WindowSize(width, height);
    return height;
}

std::vector<const char*> Window::GetRequiredExtensions() const {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME};
}

VkResult Window::CreateSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    void* layer = IOS::CurrentLayer();
    if (layer == nullptr) return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    VkMetalSurfaceCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = static_cast<const CAMetalLayer*>(layer);
    return vkCreateMetalSurfaceEXT(instance, &info, nullptr, surface);
}

void Window::GetFramebufferSize(int& width, int& height) const { IOS::WindowSize(width, height); }

double Window::GetTime() const { return IOS::Now(); }

void Window::SetSurfaceLostCallback(std::function<void()> callback) {
    IOS::SetSurfaceLostCallback(std::move(callback));
}

bool Window::TakeResumedFromSuspend() { return IOS::TakeResumedFromSuspend(); }

} // namespace Supersonic
