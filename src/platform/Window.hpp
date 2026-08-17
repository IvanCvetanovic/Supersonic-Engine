#pragma once

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
