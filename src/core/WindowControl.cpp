#include "core/WindowControl.hpp"

#include "core/GameRuntime.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

namespace Supersonic {

void WindowControl::SetFullscreen(bool fullscreen) {
    m_pending.setFullscreen = true;
    m_pending.fullscreen = fullscreen;
    m_pending.fullscreenMode = glm::uvec2(0u);
    m_pending.fullscreenRate = kDesktopRefreshRate;
}

bool WindowControl::SetFullscreenMode(uint32_t width, uint32_t height, uint32_t refreshRate) {
    const DisplayMode desktop = DesktopMode();
    if (ChooseFullscreenMode(DisplayModes(), desktop, width, height, refreshRate).width == 0) {
        // A warning, not an error: a monitor that lacks a saved mode is the
        // expected case this refusal exists for, and the caller has the
        // desktop's mode to fall back to.
        // The rate is named only when one was: the message for a size alone
        // is the one it always was.
        std::string rate;
        if (refreshRate == kHighestRefreshRate) {
            rate = " at its highest rate";
        } else if (refreshRate != kDesktopRefreshRate) {
            rate = " @ " + std::to_string(refreshRate) + " Hz";
        }
        SUPERSONIC_LOG_WARN("Window")
            << "A fullscreen mode of " << width << "x" << height << rate << " was asked for, which the "
            << "monitor does not offer (its desktop mode is " << desktop.width << "x"
            << desktop.height << "); the window is left as it is." << std::endl;
        return false;
    }
    m_pending.setFullscreen = true;
    m_pending.fullscreen = true;
    m_pending.fullscreenMode = glm::uvec2(width, height);
    m_pending.fullscreenRate = refreshRate;
    return true;
}

bool WindowControl::SetWindowedSize(uint32_t width, uint32_t height, bool centre) {
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
    m_pending.centreWindow = centre;
    // One request with FitWindowToMonitor: the later wins.
    m_pending.fitWindow = false;
    m_pending.fitFraction = 0.0f;
    return true;
}

bool WindowControl::FitWindowToMonitor(float fraction) {
    if (!(fraction > 0.0f && fraction <= 1.0f)) {
        SUPERSONIC_LOG_ERROR("Window") << "A window fitted to " << fraction
                                       << " of the monitor was asked for, which is outside (0, 1]; "
                                       << "the window is left as it is." << std::endl;
        return false;
    }
    m_pending.fitWindow = true;
    m_pending.fitFraction = fraction;
    m_pending.setWindowedSize = false;
    m_pending.windowedSize = glm::uvec2(0u);
    m_pending.centreWindow = false;
    return true;
}

void WindowControl::SetPreferredRefreshRate(uint32_t refreshRate) {
    m_pending.setRefreshRate = true;
    m_pending.refreshRate = refreshRate;
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
                                                uint32_t height, uint32_t refreshRate) {
    if (width == 0 || height == 0) return {};
    const bool desktopSize = width == desktop.width && height == desktop.height;

    if (refreshRate == kDesktopRefreshRate) {
        // The desktop's size even when the list lacks it - a platform can leave the
        // mode it is running at out of its own list - since covering the monitor
        // at the mode it is already in is always possible.
        if (desktopSize) return desktop;

        DisplayMode chosen;
        for (const DisplayMode& mode : modes) {
            if (mode.width != width || mode.height != height) continue;
            if (mode.refreshRate == desktop.refreshRate) return mode;
            if (chosen.width == 0 || mode.refreshRate > chosen.refreshRate) chosen = mode;
        }
        return chosen;
    }

    // A rate named, the highest or a number. The desktop's own mode is a
    // candidate like a listed one - never an early answer, or the highest at
    // the desktop's size would be whatever the desktop happens to run at.
    DisplayMode chosen;
    const auto consider = [&](const DisplayMode& mode) {
        if (mode.width != width || mode.height != height) return;
        if (refreshRate == kHighestRefreshRate) {
            if (chosen.width == 0 || mode.refreshRate > chosen.refreshRate) chosen = mode;
        } else if (chosen.width == 0 && mode.refreshRate == refreshRate) {
            chosen = mode;
        }
    };
    if (desktopSize) consider(desktop);
    for (const DisplayMode& mode : modes) consider(mode);
    return chosen;
}

std::vector<uint32_t> WindowControl::RefreshRatesAt(const std::vector<DisplayMode>& modes,
                                                    const DisplayMode& desktop, uint32_t width,
                                                    uint32_t height) {
    std::vector<uint32_t> rates;
    if (width == 0 || height == 0) return rates;
    if (width == desktop.width && height == desktop.height && desktop.refreshRate > 0) {
        rates.push_back(desktop.refreshRate);
    }
    for (const DisplayMode& mode : modes) {
        if (mode.width == width && mode.height == height && mode.refreshRate > 0) {
            rates.push_back(mode.refreshRate);
        }
    }
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
    return rates;
}

glm::uvec2 WindowControl::FitWindowedSize(const ScreenRect& workArea, const DisplayMode& desktop,
                                          float fraction) {
    if (!(fraction > 0.0f && fraction <= 1.0f)) return glm::uvec2(0u);

    // What there is room in, and the shape to fill it with.
    double areaWidth = workArea.width > 0 && workArea.height > 0 ? workArea.width : desktop.width;
    double areaHeight = workArea.width > 0 && workArea.height > 0 ? workArea.height : desktop.height;
    if (areaWidth <= 0.0 || areaHeight <= 0.0) return glm::uvec2(0u);
    const double shapeWidth = desktop.width > 0 && desktop.height > 0 ? desktop.width : areaWidth;
    const double shapeHeight = desktop.width > 0 && desktop.height > 0 ? desktop.height : areaHeight;

    // The scale that meets the tighter of the two limits, then each extent
    // floored: never past the limit, and the shape off by under a pixel.
    const double scale = std::min(areaWidth * fraction / shapeWidth, areaHeight * fraction / shapeHeight);
    const double minimum = GameManifest::kMinimumExtent;
    const double maximum = GameManifest::kMaximumExtent;
    const auto extent = [&](double shape) {
        return static_cast<uint32_t>(std::clamp(std::floor(shape * scale + 1e-9), minimum, maximum));
    };
    return glm::uvec2(extent(shapeWidth), extent(shapeHeight));
}

glm::ivec2 WindowControl::CentredWindowPosition(const ScreenRect& workArea, glm::uvec2 clientSize,
                                                const FrameInsets& frame) {
    const long long outerWidth = static_cast<long long>(clientSize.x) + frame.left + frame.right;
    const long long outerHeight = static_cast<long long>(clientSize.y) + frame.top + frame.bottom;
    // A spare pixel goes to the right and the bottom. A negative spare (a
    // window larger than the work area) is overruled by the clamp below.
    long long x = workArea.x + (workArea.width - outerWidth) / 2 + frame.left;
    long long y = workArea.y + (workArea.height - outerHeight) / 2 + frame.top;
    x = std::max(x, static_cast<long long>(workArea.x) + frame.left);
    y = std::max(y, static_cast<long long>(workArea.y) + frame.top);
    return glm::ivec2(static_cast<int>(x), static_cast<int>(y));
}

} // namespace Supersonic
