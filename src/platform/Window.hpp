#pragma once

#include "platform/WindowBackend.hpp"

#if SUPERSONIC_WINDOW_GLFW

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>

namespace Supersonic {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void PollEvents() const;
    GLFWwindow* GetNativeWindow() const { return m_window; }
    
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsResized() const { return m_framebufferResized; }
    void ResetResizedFlag() { m_framebufferResized = false; }

    std::vector<const char*> GetRequiredExtensions() const;

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* m_window{nullptr};
    int m_width{0};
    int m_height{0};
    std::string m_title;
    bool m_framebufferResized{false};
};

} // namespace Supersonic

#else // !SUPERSONIC_WINDOW_GLFW

#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

struct ANativeWindow;

namespace Supersonic {

// The window on a platform where the engine does not own one (WindowBackend.hpp).
//
// The same public face as the GLFW Window - SupersonicApp, the renderer and the
// polling layer call the same functions - plus the few things a BORROWED window
// needs that GLFW used to answer: a surface made from the native handle, the
// drawable size, a clock, and a way to be told the window is going away.
//
// Android: the ANativeWindow the activity has now (src/platform/android/).
class Window {
public:
    // The size is what a desktop window would open at, and is ignored: a
    // phone's window is its screen, and it exists before the game is entered
    // (android_main waits for it). Throws when there is none.
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // The platform has asked the app to finish.
    bool ShouldClose() const;

    // Everything the platform delivered since the last call - lifecycle, input,
    // size changes. BLOCKS while the app is in the background or has no window,
    // which is what stops the simulation ticking there, and returns once there
    // is a window to draw on again or the app has been asked to close.
    void PollEvents();

    ANativeWindow* GetNativeWindow() const;

    int GetWidth() const;
    int GetHeight() const;
    bool IsResized() const { return m_framebufferResized; }
    void ResetResizedFlag() { m_framebufferResized = false; }

    std::vector<const char*> GetRequiredExtensions() const;

    // ---- What only a borrowed window needs ----

    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* surface) const;

    // The drawable size in pixels, in the orientation the app is shown in.
    // 0 x 0 while there is no window.
    void GetFramebufferSize(int& width, int& height) const;

    // Monotonic seconds, for the frame timer (glfwGetTime on desktop).
    double GetTime() const;

    // Called from inside PollEvents when the platform is about to take the
    // native window away. Everything made from it - the swapchain, the
    // surface - must be destroyed before this returns: Android releases the
    // window the moment it does.
    void SetSurfaceLostCallback(std::function<void()> callback);

    // True, once, after PollEvents came back from waiting in the background, so
    // the frame timer restarts instead of handing the simulation the minutes
    // the app spent off screen.
    bool TakeResumedFromSuspend();

private:
    bool m_framebufferResized{false};
};

} // namespace Supersonic

#endif // SUPERSONIC_WINDOW_GLFW
