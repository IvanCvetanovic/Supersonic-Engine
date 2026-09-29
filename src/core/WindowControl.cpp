#include "core/WindowControl.hpp"

#include "core/GameRuntime.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <tuple>

namespace Supersonic {

void WindowControl::SetFullscreen(bool fullscreen) {
    m_pending.setFullscreen = true;
    m_pending.fullscreen = fullscreen;
    m_pending.fullscreenMode = glm::uvec2(0u);
}

bool WindowControl::SetFullscreenMode(uint32_t width, uint32_t height) {
    const DisplayMode desktop = DesktopMode();
    if (ChooseFullscreenMode(DisplayModes(), desktop, width, height).width == 0) {
        // A warning, not an error: a monitor that lacks a saved mode is the
        // expected case this refusal exists for, and the caller has the
        // desktop's mode to fall back to.
        SUPERSONIC_LOG_WARN("Window")
            << "A fullscreen mode of " << width << "x" << height << " was asked for, which the "
            << "monitor does not offer (its desktop mode is " << desktop.width << "x"
            << desktop.height << "); the window is left as it is." << std::endl;
        return false;
    }
    m_pending.setFullscreen = true;
    m_pending.fullscreen = true;
    m_pending.fullscreenMode = glm::uvec2(width, height);
    return true;
}

bool WindowControl::SetWindowedSize(uint32_t width, uint32_t height) {
    if (!IsUsableWindowSize(width, height)) {
        // Logged, as a manifest's refused size is: a request that silently
        // did nothing leaves somebody looking at a window they did not ask for
        // with nothing to explain it.
        SUPERSONIC_LOG_ERROR("Window")
            << "A windowed size of " << width << "x" << height << " was asked for, which is "
            << "outside " << GameManifest::kMinimumExtent << ".." << GameManifest::kMaximumExtent
            << "; the window is left as it is." << std::endl;
        return false;
    }
    m_pending.setWindowedSize = true;
    m_pending.windowedSize = glm::uvec2(width, height);
    return true;
}

void WindowControl::SetCursorVisible(bool visible) {
    // Through Input, never to the window: InputPolling applies the effective
    // mode and remembers what it applied, and a mode set behind its back is one
    // it would never correct.
    const CursorMode requested = Input::RequestedCursorMode();
    if (visible && requested == CursorMode::Hidden) {
        Input::SetCursorMode(CursorMode::Normal);
    } else if (!visible && requested == CursorMode::Normal) {
        Input::SetCursorMode(CursorMode::Hidden);
    }
}

WindowControl::Requests WindowControl::TakeRequests() {
    const Requests taken = m_pending;
    m_pending = Requests{};
    return taken;
}

std::vector<DisplayMode> WindowControl::SelectDisplayModes(const std::vector<VideoMode>& modes) {
    std::vector<DisplayMode> selected;
    selected.reserve(modes.size());
    for (const VideoMode& mode : modes) {
        if (mode.redBits != 8 || mode.greenBits != 8 || mode.blueBits != 8) continue;
        if (mode.width <= 0 || mode.height <= 0) continue;
        selected.push_back(DisplayMode{static_cast<uint32_t>(mode.width),
                                       static_cast<uint32_t>(mode.height),
                                       static_cast<uint32_t>(std::max(mode.refreshRate, 0))});
    }

    // Sorted here rather than trusted to arrive sorted. GLFW documents an
    // order, but it sorts by bit depth first, so once the 16-bit modes are
    // dropped the order is only as good as the next platform's reading of it.
    const auto key = [](const DisplayMode& m) {
        return std::make_tuple(uint64_t{m.width} * m.height, m.width, m.refreshRate);
    };
    std::stable_sort(selected.begin(), selected.end(),
                     [&key](const DisplayMode& a, const DisplayMode& b) { return key(a) < key(b); });
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

int WindowControl::MonitorUnder(const ScreenRect& window, const std::vector<ScreenRect>& monitors) {
    int best = -1;
    long long bestArea = 0;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const ScreenRect& m = monitors[i];
        const long long left = std::max(window.x, m.x);
        const long long top = std::max(window.y, m.y);
        const long long right = std::min(static_cast<long long>(window.x) + window.width,
                                         static_cast<long long>(m.x) + m.width);
        const long long bottom = std::min(static_cast<long long>(window.y) + window.height,
                                          static_cast<long long>(m.y) + m.height);
        if (right <= left || bottom <= top) continue;

        // Strictly greater, so a window split exactly in half goes to the
        // first monitor rather than to whichever the loop saw last.
        const long long area = (right - left) * (bottom - top);
        if (area > bestArea) {
            bestArea = area;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool WindowControl::IsUsableWindowSize(uint32_t width, uint32_t height) {
    return width >= GameManifest::kMinimumExtent && height >= GameManifest::kMinimumExtent &&
           width <= GameManifest::kMaximumExtent && height <= GameManifest::kMaximumExtent;
}

DisplayMode WindowControl::ChooseFullscreenMode(const std::vector<DisplayMode>& modes,
                                                const DisplayMode& desktop, uint32_t width,
                                                uint32_t height) {
    if (width == 0 || height == 0) return {};

    // The desktop's size even when the list lacks it - a platform can leave the
    // mode it is running at out of its own list - since covering the monitor
    // at the mode it is already in is always possible.
    if (width == desktop.width && height == desktop.height) return desktop;

    DisplayMode chosen;
    for (const DisplayMode& mode : modes) {
        if (mode.width != width || mode.height != height) continue;
        if (mode.refreshRate == desktop.refreshRate) return mode;
        if (chosen.width == 0 || mode.refreshRate > chosen.refreshRate) chosen = mode;
    }
    return chosen;
}

} // namespace Supersonic
