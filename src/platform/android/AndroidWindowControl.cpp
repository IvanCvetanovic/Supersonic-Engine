// platform/NativeWindowControl.hpp on Android, where the window is the screen.
//
// A game's options screen still asks - fullscreen, a windowed size, the modes
// the monitor offers - and gets the honest answers: always fullscreen, at the
// one size the display has. Requests are taken and dropped, so they do not
// sit pending forever and a game polling Pending() sees them go.

#include "platform/NativeWindowControl.hpp"

#include "core/Log.hpp"
#include "platform/Window.hpp"
#include "platform/android/AndroidApp.hpp"

namespace Supersonic {

NativeWindowControl::NativeWindowControl(Window& window) : m_window(window) {}

bool NativeWindowControl::IsFullscreen() const { return true; }

glm::uvec2 NativeWindowControl::WindowSize() const {
    int width = 0;
    int height = 0;
    m_window.GetFramebufferSize(width, height);
    return {static_cast<uint32_t>(width > 0 ? width : 0), static_cast<uint32_t>(height > 0 ? height : 0)};
}

std::vector<DisplayMode> NativeWindowControl::DisplayModes() const { return {DesktopMode()}; }

DisplayMode NativeWindowControl::DesktopMode() const {
    const glm::uvec2 size = WindowSize();
    return DisplayMode{size.x, size.y, 60};
}

bool NativeWindowControl::ApplyPending() {
    if (!Pending().Any()) return false;
    const Requests requests = TakeRequests();
    if (requests.setFullscreen && !requests.fullscreen) {
        SUPERSONIC_LOG_INFO("WindowControl") << "A windowed mode was asked for; an Android app is its screen.";
    }
    // The one request a phone's screen can take: the display's refresh rate.
    if (requests.setRefreshRate) Android::RequestRefreshRate(requests.refreshRate);
    return false;
}

} // namespace Supersonic
