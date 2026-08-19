#include "platform/Window.hpp"
#include "core/Log.hpp"

#include <iostream>
#include <stdexcept>

namespace Supersonic {

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height), m_title(title) {

    // Installed before glfwInit so initialisation failures explain themselves.
    // Without it GLFW errors were silently discarded and surfaced much later.
    glfwSetErrorCallback([](int code, const char* description) {
        SUPERSONIC_LOG_ERROR("GLFW") << "error " << code << ": " << (description ? description : "(no detail)") << std::endl;
    });

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }

    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw std::runtime_error(
            "No Vulkan loader or ICD was found. Install up-to-date graphics drivers "
            "(or the Vulkan SDK) and try again.");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW Window!");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);

    SUPERSONIC_LOG_INFO("Window") << "GLFW Window created: " << m_width << "x" << m_height << " (\"" << m_title << "\")" << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    SUPERSONIC_LOG_INFO("Window") << "GLFW Window destroyed." << std::endl;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto appWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (appWindow) {
        appWindow->m_width = width;
        appWindow->m_height = height;
        appWindow->m_framebufferResized = true;
    }
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::PollEvents() const {
    glfwPollEvents();
}

std::vector<const char*> Window::GetRequiredExtensions() const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    // A NULL return means GLFW could not find a usable loader. Ignoring it
    // produced an instance with zero surface extensions and a "created
    // successfully" log line, with the real failure surfacing later and
    // misleadingly at glfwCreateWindowSurface.
    if (!glfwExtensions || glfwExtensionCount == 0) {
        throw std::runtime_error(
            "GLFW could not report the required Vulkan instance extensions. "
            "This usually means no Vulkan loader or ICD is installed.");
    }

    return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

} // namespace Supersonic
