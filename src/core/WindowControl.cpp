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

} // namespace Supersonic
