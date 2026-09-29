#pragma once

#include "core/WindowControl.hpp"

struct GLFWmonitor;

namespace Supersonic {

class Window;

// The platform's half of WindowControl: the queries answered from GLFW, and
// the requests applied to the window.
//
// SupersonicApp owns the one instance and calls ApplyPending at the top of each
// frame; everything else - a layer, a game's main - sees only the WindowControl
// it publishes. No test target links this file, which is why the decisions it
// makes are WindowControl's static functions rather than its own.
class NativeWindowControl final : public WindowControl {
public:
    explicit NativeWindowControl(Window& window);

    bool IsFullscreen() const override;
    glm::uvec2 WindowSize() const override;
    std::vector<DisplayMode> DisplayModes() const override;
    DisplayMode DesktopMode() const override;

    // Performs whatever has been asked for since the last call. True when
    // something was applied.
    //
    // At the top of the frame and BEFORE events are polled. Every change here
    // resizes the window, and the resize has to reach the swapchain and the
    // offscreen target through the path an edge dragged by hand already takes:
    // the framebuffer-size callback raising the window's resized flag, which
    // DrawFrame checks before it acquires, and ImGui's display size, which the
    // game view hands the offscreen target to rebuild at the top of the next
    // frame. Win32 delivers the size messages inside the call; other platforms
    // queue them, and polling after rather than before is what lets this
    // frame's poll collect them on every platform.
    //
    // A request made while the window is minimised is kept until it is not: a
    // minimised window reports a position far off-screen, and fullscreen
    // entered from there would come back to it.
    bool ApplyPending();

private:
    // The monitor the window is on: its own while fullscreen, otherwise the
    // one it overlaps most, otherwise the primary.
    GLFWmonitor* currentMonitor() const;

    void enterFullscreen();
    void leaveFullscreen();
    void applyWindowedSize(glm::uvec2 size, bool centre);
    // FitWindowToMonitor's half: the size measured from the monitor the window
    // is on, then placed as a centred SetWindowedSize.
    void applyFitWindow(float fraction);
    // A windowed size centred on `monitor`'s work area: the window moved and
    // sized now, or, while fullscreen, the rectangle it comes back at.
    void placeCentred(glm::uvec2 size, GLFWmonitor* monitor);
    // The work area a window on `monitor` has - the desktop's while
    // SetFullscreenMode has switched it - or the whole monitor when the
    // platform gives none.
    ScreenRect workAreaOf(GLFWmonitor* monitor) const;

    // SetFullscreenMode's half: enters fullscreen at the mode for `size` and
    // `refreshRate`, or changes mode while already there.
    void enterFullscreenMode(glm::uvec2 size, uint32_t refreshRate);
    // The rectangle SetFullscreen(false) returns to, taken on the way in.
    void rememberWindowedRect();

    Window& m_window;

    // Where the window was before it covered a monitor, which is where
    // SetFullscreen(false) puts it back. The NORMAL rectangle, taken after
    // un-maximising, with the maximise remembered separately: the maximised
    // rectangle would come back as an ordinary window the size of the screen.
    int m_windowedX{0};
    int m_windowedY{0};
    int m_windowedWidth{0};
    int m_windowedHeight{0};
    bool m_windowedMaximized{false};
    bool m_hasWindowedRect{false};
    // The window's frame, measured while it had one: a fullscreen window has
    // none, and a rectangle centred for it to come back at must still leave
    // room for its title bar.
    FrameInsets m_frame;

    // The monitor SetFullscreenMode has switched away from its desktop mode,
    // and that mode. glfwGetVideoMode answers with the switched mode until
    // GLFW puts the desktop's back on the way out, so the desktop's is kept
    // here for DesktopMode() and for the next choice of mode to measure from.
    // Null while nothing is switched.
    GLFWmonitor* m_switchedMonitor{nullptr};
    DisplayMode m_desktopMode;
    // And its work area then, which glfwGetMonitorWorkarea no longer gives
    // either: an automatic window asked for on the way out of a switched mode
    // is fitted to the desktop it returns to.
    ScreenRect m_desktopWorkArea;
};

} // namespace Supersonic
