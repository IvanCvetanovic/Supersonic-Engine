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
    void applyWindowedSize(glm::uvec2 size);

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
};

} // namespace Supersonic
