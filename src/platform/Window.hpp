#pragma once

#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Engine {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void PollEvents();

    GLFWwindow* GetNativeWindow() const { return m_window; }
    std::vector<const char*> GetRequiredExtensions() const;

private:
    GLFWwindow* m_window{nullptr};
    int m_width{0};
    int m_height{0};
    std::string m_title;
};

} // namespace Engine
