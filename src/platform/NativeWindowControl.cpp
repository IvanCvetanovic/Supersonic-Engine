#include "platform/NativeWindowControl.hpp"

#include "platform/Window.hpp"
#include "core/GameRuntime.hpp"
#include "core/Log.hpp"

namespace Supersonic {

namespace {

const char* monitorName(GLFWmonitor* monitor) {
    const char* name = monitor ? glfwGetMonitorName(monitor) : nullptr;
    return name ? name : "(unnamed monitor)";
}

} // namespace

NativeWindowControl::NativeWindowControl(Window& window) : m_window(window) {}

bool NativeWindowControl::IsFullscreen() const {
    GLFWwindow* native = m_window.GetNativeWindow();
    return native && glfwGetWindowMonitor(native) != nullptr;
}

glm::uvec2 NativeWindowControl::WindowSize() const {
    GLFWwindow* native = m_window.GetNativeWindow();
    if (!native) return glm::uvec2(0u);
    int width = 0;
    int height = 0;
    glfwGetWindowSize(native, &width, &height);
    return glm::uvec2(static_cast<uint32_t>(width > 0 ? width : 0),
                      static_cast<uint32_t>(height > 0 ? height : 0));
}

std::vector<DisplayMode> NativeWindowControl::DisplayModes() const {
    GLFWmonitor* monitor = currentMonitor();
    if (!monitor) return {};

    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
    if (!modes || count <= 0) return {};

    std::vector<VideoMode> reported;
    reported.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const GLFWvidmode& m = modes[i];
        reported.push_back(VideoMode{m.width, m.height, m.redBits, m.greenBits, m.blueBits,
                                     m.refreshRate});
    }
    return SelectDisplayModes(reported);
}

DisplayMode NativeWindowControl::DesktopMode() const {
    GLFWmonitor* monitor = currentMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) return {};
    return DisplayMode{static_cast<uint32_t>(mode->width), static_cast<uint32_t>(mode->height),
                       static_cast<uint32_t>(mode->refreshRate > 0 ? mode->refreshRate : 0)};
}

GLFWmonitor* NativeWindowControl::currentMonitor() const {
    GLFWwindow* native = m_window.GetNativeWindow();
    if (!native) return nullptr;
    if (GLFWmonitor* own = glfwGetWindowMonitor(native)) return own;

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count <= 0) return nullptr;

    // Each monitor's whole rectangle at its current mode, not its work area: a
    // window over the taskbar is still on that monitor.
    std::vector<ScreenRect> rects;
    rects.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        ScreenRect rect;
        glfwGetMonitorPos(monitors[i], &rect.x, &rect.y);
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitors[i])) {
            rect.width = mode->width;
            rect.height = mode->height;
        }
        rects.push_back(rect);
    }

    ScreenRect window;
    glfwGetWindowPos(native, &window.x, &window.y);
    glfwGetWindowSize(native, &window.width, &window.height);

    const int under = MonitorUnder(window, rects);
    return under >= 0 ? monitors[under] : glfwGetPrimaryMonitor();
}

bool NativeWindowControl::ApplyPending() {
    if (!Pending().Any()) return false;

    GLFWwindow* native = m_window.GetNativeWindow();
    if (!native) return false;
    if (glfwGetWindowAttrib(native, GLFW_ICONIFIED) == GLFW_TRUE) return false;

    const Requests requests = TakeRequests();

    // The size FIRST. Leaving fullscreen then comes back at the size just
    // asked for, and entering it remembers that size as the one to return to -
    // so a menu applying "1280x720, windowed" as two requests in either order
    // ends up at 1280x720 and windowed.
    if (requests.setWindowedSize) applyWindowedSize(requests.windowedSize);

    if (requests.setFullscreen) {
        const bool fullscreen = IsFullscreen();
        if (requests.fullscreen && !fullscreen) {
            enterFullscreen();
        } else if (!requests.fullscreen && fullscreen) {
            leaveFullscreen();
        }
    }
    return true;
}

void NativeWindowControl::applyWindowedSize(glm::uvec2 size) {
    GLFWwindow* native = m_window.GetNativeWindow();

    if (IsFullscreen()) {
        // Nothing to resize now: the window is the monitor's size. What changes
        // is the size it comes back at, and a size chosen outright is not a
        // maximised one.
        m_windowedWidth = static_cast<int>(size.x);
        m_windowedHeight = static_cast<int>(size.y);
        m_windowedMaximized = false;
        SUPERSONIC_LOG_INFO("Window") << "Windowed size set to " << size.x << "x" << size.y
                                      << "; applies on leaving fullscreen." << std::endl;
        return;
    }

    // A maximised window keeps its maximised size whatever it is told, and
    // reports the new one as though it had taken it.
    if (glfwGetWindowAttrib(native, GLFW_MAXIMIZED) == GLFW_TRUE) glfwRestoreWindow(native);
    glfwSetWindowSize(native, static_cast<int>(size.x), static_cast<int>(size.y));
    SUPERSONIC_LOG_INFO("Window") << "Window resized to " << size.x << "x" << size.y << "."
                                  << std::endl;
}

void NativeWindowControl::enterFullscreen() {
    GLFWwindow* native = m_window.GetNativeWindow();

    GLFWmonitor* monitor = currentMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) {
        SUPERSONIC_LOG_ERROR("Window") << "Fullscreen was asked for, but no monitor reports a "
                                          "video mode; staying windowed." << std::endl;
        return;
    }

    m_windowedMaximized = glfwGetWindowAttrib(native, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (m_windowedMaximized) glfwRestoreWindow(native);
    glfwGetWindowPos(native, &m_windowedX, &m_windowedY);
    glfwGetWindowSize(native, &m_windowedWidth, &m_windowedHeight);
    m_hasWindowedRect = true;

    // At the mode the monitor is ALREADY in, which GLFW recognises and does
    // not switch: no black screen while the display resynchronises, no desktop
    // rearranged behind the game, and leaving is as quick as entering. The
    // cost is that the render resolution is the monitor's, since in game mode
    // the offscreen target follows the window.
    //
    // GLFW_AUTO_ICONIFY is left at its default, which minimises the window
    // when it loses focus. GLFW keeps a fullscreen window topmost, so without
    // it an alt-tab would leave the game drawn over whatever was switched to.
    glfwSetWindowMonitor(native, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);

    SUPERSONIC_LOG_INFO("Window") << "Fullscreen on " << monitorName(monitor) << " at "
                                  << mode->width << "x" << mode->height << " @ "
                                  << mode->refreshRate << " Hz." << std::endl;
}

void NativeWindowControl::leaveFullscreen() {
    GLFWwindow* native = m_window.GetNativeWindow();

    if (!m_hasWindowedRect) {
        // Fullscreen by some route that did not come through here, so there is
        // no rectangle to return to. The engine's default size, centred on
        // the monitor it was covering, rather than a window at the origin.
        GLFWmonitor* monitor = glfwGetWindowMonitor(native);
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        int monitorX = 0;
        int monitorY = 0;
        if (monitor) glfwGetMonitorPos(monitor, &monitorX, &monitorY);
        m_windowedWidth = static_cast<int>(GameManifest::kDefaultWidth);
        m_windowedHeight = static_cast<int>(GameManifest::kDefaultHeight);
        m_windowedX = monitorX + (mode ? (mode->width - m_windowedWidth) / 2 : 0);
        m_windowedY = monitorY + (mode ? (mode->height - m_windowedHeight) / 2 : 0);
        m_windowedMaximized = false;
    }

    // Decorations, resizability and the rest come back with it: GLFW restores
    // the window's own attributes on the way out of a monitor.
    glfwSetWindowMonitor(native, nullptr, m_windowedX, m_windowedY, m_windowedWidth,
                         m_windowedHeight, GLFW_DONT_CARE);
    if (m_windowedMaximized) glfwMaximizeWindow(native);

    SUPERSONIC_LOG_INFO("Window") << "Windowed at " << m_windowedWidth << "x" << m_windowedHeight
                                  << (m_windowedMaximized ? ", maximised" : "") << "."
                                  << std::endl;
}

} // namespace Supersonic
