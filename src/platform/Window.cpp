#include "platform/Window.hpp"

#include <iostream>
#include <stdexcept>

namespace Engine {

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height), m_title(title) {

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
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

    std::cout << "[Window] GLFW Window created: " << m_width << "x" << m_height << " (\"" << m_title << "\")" << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    std::cout << "[Window] GLFW Window destroyed." << std::endl;
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

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    return extensions;
}

} // namespace Engine
