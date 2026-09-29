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
    // Only while the window still covers the monitor it switched: when a
    // monitor goes away GLFW drops the window to windowed without coming
    // through leaveFullscreen, and one plugged in later may reuse the pointer.
    GLFWwindow* native = m_window.GetNativeWindow();
    if (monitor && monitor == m_switchedMonitor && native && glfwGetWindowMonitor(native) == monitor) {
        return m_desktopMode;
    }
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
    // ends up at 1280x720 and windowed. A fitted window is a size too.
    if (requests.setWindowedSize) {
        applyWindowedSize(requests.windowedSize, requests.centreWindow);
    } else if (requests.fitWindow) {
        applyFitWindow(requests.fitFraction);
    }

    if (requests.setFullscreen) {
        const bool fullscreen = IsFullscreen();
        if (requests.fullscreen && requests.fullscreenMode.x > 0 && requests.fullscreenMode.y > 0) {
            // Whether windowed or already fullscreen: a mode is asked of both.
            enterFullscreenMode(requests.fullscreenMode, requests.fullscreenRate);
        } else if (requests.fullscreen && !fullscreen) {
            enterFullscreen();
        } else if (!requests.fullscreen && fullscreen) {
            leaveFullscreen();
        }
    }

    // requests.setRefreshRate is taken and left: a desktop window runs at the
    // compositor's rate, and fullscreen at its mode's (SetFullscreenMode).
    return requests.setWindowedSize || requests.fitWindow || requests.setFullscreen;
}

void NativeWindowControl::applyWindowedSize(glm::uvec2 size, bool centre) {
    GLFWwindow* native = m_window.GetNativeWindow();

    if (centre) {
        placeCentred(size, currentMonitor());
        return;
    }

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

WindowControl::ScreenRect NativeWindowControl::workAreaOf(GLFWmonitor* monitor) const {
    ScreenRect workArea;
    if (!monitor) return workArea;
    // While SetFullscreenMode has the monitor at another mode, GLFW measures
    // that mode's screen, and the window comes back to the desktop's: the
    // work area taken before the switch, as DesktopMode() answers the mode.
    GLFWwindow* native = m_window.GetNativeWindow();
    if (monitor == m_switchedMonitor && native && glfwGetWindowMonitor(native) == monitor &&
        m_desktopWorkArea.width > 0 && m_desktopWorkArea.height > 0) {
        return m_desktopWorkArea;
    }
    glfwGetMonitorWorkarea(monitor, &workArea.x, &workArea.y, &workArea.width, &workArea.height);
    if (workArea.width <= 0 || workArea.height <= 0) {
        // No work area to give: the whole monitor.
        glfwGetMonitorPos(monitor, &workArea.x, &workArea.y);
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
            workArea.width = mode->width;
            workArea.height = mode->height;
        }
    }
    return workArea;
}

void NativeWindowControl::applyFitWindow(float fraction) {
    GLFWmonitor* monitor = currentMonitor();
    const ScreenRect workArea = workAreaOf(monitor);
    const DisplayMode desktop = DesktopMode();
    const glm::uvec2 size = FitWindowedSize(workArea, desktop, fraction);
    if (size.x == 0 || size.y == 0) {
        SUPERSONIC_LOG_WARN("Window") << "A window fitted to the monitor was asked for, but no monitor "
                                         "reports its size; the window is left as it is." << std::endl;
        return;
    }
    SUPERSONIC_LOG_INFO("Window") << "Window fitted to " << monitorName(monitor) << ": " << size.x << "x"
                                  << size.y << ", the desktop's " << desktop.width << "x" << desktop.height
                                  << " shape within " << fraction << " of the work area "
                                  << workArea.width << "x" << workArea.height << "." << std::endl;
    placeCentred(size, monitor);
}

void NativeWindowControl::placeCentred(glm::uvec2 size, GLFWmonitor* monitor) {
    GLFWwindow* native = m_window.GetNativeWindow();

    const ScreenRect workArea = workAreaOf(monitor);

    if (IsFullscreen()) {
        // The rectangle to come back at, whole: the size alone would leave
        // the position the window had before, and a window never taken
        // windowed would come back at the engine's default size instead.
        const glm::ivec2 at = CentredWindowPosition(workArea, size, m_frame);
        m_windowedX = at.x;
        m_windowedY = at.y;
        m_windowedWidth = static_cast<int>(size.x);
        m_windowedHeight = static_cast<int>(size.y);
        m_windowedMaximized = false;
        m_hasWindowedRect = true;
        SUPERSONIC_LOG_INFO("Window") << "Windowed size set to " << size.x << "x" << size.y << ", centred at "
                                      << at.x << "," << at.y << "; applies on leaving fullscreen." << std::endl;
        return;
    }

    if (glfwGetWindowAttrib(native, GLFW_MAXIMIZED) == GLFW_TRUE) glfwRestoreWindow(native);
    glfwSetWindowSize(native, static_cast<int>(size.x), static_cast<int>(size.y));
    // The frame after the resize: GLFW answers it for the window as it is.
    glfwGetWindowFrameSize(native, &m_frame.left, &m_frame.top, &m_frame.right, &m_frame.bottom);
    const glm::ivec2 at = CentredWindowPosition(workArea, size, m_frame);
    glfwSetWindowPos(native, at.x, at.y);
    SUPERSONIC_LOG_INFO("Window") << "Window resized to " << size.x << "x" << size.y << ", centred at " << at.x
                                  << "," << at.y << " on " << monitorName(monitor) << "." << std::endl;
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

    rememberWindowedRect();

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

void NativeWindowControl::rememberWindowedRect() {
    GLFWwindow* native = m_window.GetNativeWindow();
    m_windowedMaximized = glfwGetWindowAttrib(native, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (m_windowedMaximized) glfwRestoreWindow(native);
    glfwGetWindowPos(native, &m_windowedX, &m_windowedY);
    glfwGetWindowSize(native, &m_windowedWidth, &m_windowedHeight);
    glfwGetWindowFrameSize(native, &m_frame.left, &m_frame.top, &m_frame.right, &m_frame.bottom);
    m_hasWindowedRect = true;
}

void NativeWindowControl::enterFullscreenMode(glm::uvec2 size, uint32_t refreshRate) {
    GLFWwindow* native = m_window.GetNativeWindow();

    GLFWmonitor* monitor = currentMonitor();
    const GLFWvidmode* current = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!current) {
        SUPERSONIC_LOG_ERROR("Window") << "A fullscreen mode was asked for, but no monitor "
                                          "reports a video mode; the window is left as it is."
                                       << std::endl;
        return;
    }

    // Chosen again here rather than trusted from the request: the window may
    // have moved to another monitor since, or the monitor lost the mode.
    const DisplayMode desktop = DesktopMode();
    const DisplayMode mode = ChooseFullscreenMode(DisplayModes(), desktop, size.x, size.y, refreshRate);
    if (mode.width == 0) {
        SUPERSONIC_LOG_WARN("Window") << "A fullscreen mode of " << size.x << "x" << size.y
                                      << " was asked for, which " << monitorName(monitor)
                                      << " does not offer; the window is left as it is."
                                      << std::endl;
        return;
    }

    const bool fullscreen = IsFullscreen();
    const bool atMode = static_cast<uint32_t>(current->width) == mode.width &&
                        static_cast<uint32_t>(current->height) == mode.height &&
                        static_cast<uint32_t>(current->refreshRate) == mode.refreshRate;
    if (fullscreen && atMode) {
        SUPERSONIC_LOG_INFO("Window") << "Fullscreen on " << monitorName(monitor) << " already at "
                                      << mode.width << "x" << mode.height << " @ "
                                      << mode.refreshRate << " Hz." << std::endl;
        return;
    }

    // Only on the way in: while fullscreen the window's rectangle is the
    // monitor's, and the one to return to was taken when it entered.
    if (!fullscreen) rememberWindowedRect();
    // The desktop's work area, while the monitor still runs the desktop's
    // mode: a window centred for later must be centred on the screen it will
    // come back to (workAreaOf), not on the switched one.
    const ScreenRect desktopWorkArea = workAreaOf(monitor);

    // Already on this monitor, GLFW sets the mode and fits the window to the
    // monitor's new size in place; the framebuffer callback carries the new
    // size to the swapchain as it does for any resize. GLFW restores the
    // desktop's mode itself when the window leaves the monitor or is iconified.
    const int glfwRate = mode.refreshRate > 0 ? static_cast<int>(mode.refreshRate) : GLFW_DONT_CARE;
    glfwSetWindowMonitor(native, monitor, 0, 0, static_cast<int>(mode.width),
                         static_cast<int>(mode.height), glfwRate);

    // What the monitor runs at now, not what was asked: a driver can refuse a
    // mode it listed (GLFW reports that through the error callback and leaves
    // the mode as it was), and a compositor can decline to switch at all. The
    // log then says so rather than claiming a switch that did not happen.
    DisplayMode reached = mode;
    if (const GLFWvidmode* now = glfwGetVideoMode(monitor)) {
        reached = DisplayMode{static_cast<uint32_t>(now->width), static_cast<uint32_t>(now->height),
                              static_cast<uint32_t>(now->refreshRate > 0 ? now->refreshRate : 0)};
    }
    const bool switched = reached != desktop;
    m_switchedMonitor = switched ? monitor : nullptr;
    m_desktopMode = desktop;
    m_desktopWorkArea = switched ? desktopWorkArea : ScreenRect{};

    if (reached.width != mode.width || reached.height != mode.height) {
        SUPERSONIC_LOG_WARN("Window")
            << "Fullscreen on " << monitorName(monitor) << " at " << mode.width << "x"
            << mode.height << " @ " << mode.refreshRate << " Hz was asked for; the display runs at "
            << reached.width << "x" << reached.height << " @ " << reached.refreshRate << " Hz."
            << std::endl;
    } else if (switched) {
        SUPERSONIC_LOG_INFO("Window")
            << "Fullscreen on " << monitorName(monitor) << " at " << reached.width << "x"
            << reached.height << " @ " << reached.refreshRate << " Hz, switched from the desktop's "
            << desktop.width << "x" << desktop.height << " @ " << desktop.refreshRate << " Hz."
            << std::endl;
    } else {
        SUPERSONIC_LOG_INFO("Window") << "Fullscreen on " << monitorName(monitor) << " at "
                                      << mode.width << "x" << mode.height << " @ "
                                      << mode.refreshRate << " Hz, the desktop's mode."
                                      << std::endl;
    }
}

void NativeWindowControl::leaveFullscreen() {
    GLFWwindow* native = m_window.GetNativeWindow();

    // GLFW puts the desktop's mode back as the window leaves the monitor.
    m_switchedMonitor = nullptr;

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
