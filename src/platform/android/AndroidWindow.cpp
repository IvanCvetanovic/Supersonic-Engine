// platform/Window.hpp's native-surface Window, over Android's ANativeWindow.

#include "platform/Window.hpp"

#include <stdexcept>

#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include "core/Log.hpp"
#include "platform/android/AndroidApp.hpp"

namespace Supersonic {

Window::Window(int width, int height, const std::string& title) {
    (void)width;
    (void)height;
    if (Android::CurrentWindow() == nullptr) {
        throw std::runtime_error("Android: the activity has no window to draw on.");
    }
    int w = 0;
    int h = 0;
    Android::WindowSize(w, h);
    // Taken now, so the first frame does not read the window's arrival as a
    // resize and rebuild the swapchain it was just given.
    Android::TakeWindowSizeChanged();
    SUPERSONIC_LOG_INFO("Window") << "Android window: " << w << "x" << h << " (\"" << title << "\")";
}

Window::~Window() {
    // The callback reaches into whoever set it, which is going away with this
    // window. android_main keeps reading lifecycle commands after the game
    // returns, and a TERM_WINDOW among them must not call into it.
    Android::SetSurfaceLostCallback(nullptr);
}

bool Window::ShouldClose() const { return Android::DestroyRequested(); }

void Window::PollEvents() {
    Android::WaitUntilDrawable();
    if (Android::TakeWindowSizeChanged()) m_framebufferResized = true;
}

ANativeWindow* Window::GetNativeWindow() const { return Android::CurrentWindow(); }

int Window::GetWidth() const {
    int width = 0;
    int height = 0;
    Android::WindowSize(width, height);
    return width;
}

int Window::GetHeight() const {
    int width = 0;
    int height = 0;
    Android::WindowSize(width, height);
    return height;
}

std::vector<const char*> Window::GetRequiredExtensions() const {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
}

VkResult Window::CreateSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    ANativeWindow* native = Android::CurrentWindow();
    if (native == nullptr) return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = native;
    return vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface);
}

void Window::GetFramebufferSize(int& width, int& height) const { Android::WindowSize(width, height); }

double Window::GetTime() const { return Android::Now(); }

void Window::SetSurfaceLostCallback(std::function<void()> callback) {
    Android::SetSurfaceLostCallback(std::move(callback));
}

bool Window::TakeResumedFromSuspend() { return Android::TakeResumedFromSuspend(); }

} // namespace Supersonic
