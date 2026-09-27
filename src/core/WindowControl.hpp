#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// One way a monitor can be driven, in the terms a settings menu lists it.
struct DisplayMode {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t refreshRate{0}; // Hz

    bool operator==(const DisplayMode& other) const {
        return width == other.width && height == other.height &&
               refreshRate == other.refreshRate;
    }
    bool operator!=(const DisplayMode& other) const { return !(*this == other); }
};

// The window, as a game may ask things of it.
//
// A game could not change its own window. The size was decided once, before the
// window existed - the flag, then the manifest, then the default - and after
// that the only thing that could touch it was the player dragging an edge. A
// layer cannot reach the Window, deliberately, so the "Fullscreen" line that is
// on the options screen of nearly every PC game had nothing to call, and
// neither did a resolution list.
//
// Published in the registry context as `WindowControl*`, the way SceneManager*
// is and for the same reason: it is the one place a layer can reach without
// the engine handing it another argument, and a pointer survives the scene
// loads that clear the registry around it.
//
//     if (auto* const* window = registry.ctx().find<WindowControl*>()) {
//         (*window)->SetFullscreen(!(*window)->IsFullscreen());
//     }
//
// QUERIES ARE LIVE, REQUESTS ARE DEFERRED. What the window is right now is read
// from the platform when asked. What a game wants it to become is recorded and
// applied at the top of the next frame, before events are polled - never where
// it was asked for. Whoever toggles fullscreen is inside a tick with a frame
// half-built around them, and the swapchain and the offscreen target both have
// to follow the new size; both already follow a resize at exactly that point,
// which is the path a mode change takes rather than a second one. So
// `IsFullscreen()` keeps answering the old value until then, and a menu that
// toggles and reads back in the same tick reads what the window still is.
//
// MAIN THREAD ONLY, as every layer callback and script already is: the queries
// go to GLFW, which answers only there. A job must not call it.
//
// The requests are latched here, in a class with no platform behind it, so the
// latching is testable without a window; the queries and the applying belong to
// the platform's subclass. It is the split Input and InputPolling make, for the
// same reason.
class WindowControl {
public:
    virtual ~WindowControl() = default;

    WindowControl(const WindowControl&) = delete;
    WindowControl& operator=(const WindowControl&) = delete;

    // ---- What the window is ---------------------------------------------

    // Covering a monitor, as SetFullscreen(true) leaves it.
    virtual bool IsFullscreen() const = 0;

    // The client area in screen coordinates: what `--window` and the
    // manifest's Width and Height ask for, and in game mode what the offscreen
    // target follows, so the render resolution. Zero while minimised.
    virtual glm::uvec2 WindowSize() const = 0;

    // Every mode the monitor the window is on can be driven at, with eight bits
    // per colour channel, smallest first, each size and rate once. Read from
    // the platform on every call, so it follows a window dragged to another
    // monitor: call it when a menu opens, not every frame.
    virtual std::vector<DisplayMode> DisplayModes() const = 0;

    // The mode that monitor is running at now, which is the size fullscreen
    // opens at. Zeroes when there is no monitor to ask.
    virtual DisplayMode DesktopMode() const = 0;

    // ---- What a game asks for ---------------------------------------------

    // Cover the monitor the window is mostly on, at that monitor's current
    // mode - so no mode switch, and nothing for the display to resynchronise
    // on the way in or out. false returns to the size and position the window
    // had before, maximised again if it was.
    //
    // Applied at the top of the next frame. The last request before then wins.
    void SetFullscreen(bool fullscreen);

    // The windowed size, applied at the top of the next frame. While
    // fullscreen it is the size the window comes back at.
    //
    // Refused - false, nothing latched - outside the range a manifest or
    // `--window` is held to (GameManifest::kMinimumExtent..kMaximumExtent): a
    // settings file with a typo in it should leave the window alone rather
    // than open one nobody can see.
    bool SetWindowedSize(uint32_t width, uint32_t height);

    // Show or hide the pointer over the window.
    //
    // NOT A SECOND PATH to the pointer: it is the same request as
    // Input::SetCursorMode(CursorMode::Hidden), made through it, so it answers
    // to the same two vetoes - the editor's hold on the pointer between plays,
    // and a window without focus. A pointer this set directly would be one the
    // polling layer did not know it had to give back. false turns Normal into
    // Hidden and true turns Hidden into Normal; a Locked pointer is already
    // invisible and is left alone, or hiding the cursor would end a mouse-look.
    void SetCursorVisible(bool visible);

    // ---- The platform's side ----------------------------------------------

    // What has been asked for since the requests were last taken.
    struct Requests {
        bool setFullscreen{false};
        bool fullscreen{false};

        bool setWindowedSize{false};
        glm::uvec2 windowedSize{0u, 0u};

        bool Any() const { return setFullscreen || setWindowedSize; }
    };

    const Requests& Pending() const { return m_pending; }

    // Hands the requests over and forgets them.
    Requests TakeRequests();

    // ---- The decisions, pure so a suite can reach them ------------------

    // A mode as the platform reports it, before it is a DisplayMode.
    struct VideoMode {
        int width{0};
        int height{0};
        int redBits{0};
        int greenBits{0};
        int blueBits{0};
        int refreshRate{0};
    };

    // The modes worth listing: eight bits per channel only - the others are
    // the 16-bit leftovers a desktop compositor will not run - and each
    // size-and-rate once, ordered by size and then rate. The platform lists
    // some modes more than once, and a menu that offers the same line twice
    // looks broken.
    static std::vector<DisplayMode> SelectDisplayModes(const std::vector<VideoMode>& modes);

    // A rectangle on the virtual desktop, in screen coordinates.
    struct ScreenRect {
        int x{0};
        int y{0};
        int width{0};
        int height{0};
    };

    // Which monitor a window is on: the one it overlaps most, the first on a
    // tie. A window straddling two screens goes fullscreen on the one holding
    // most of it, which is the one the player was looking at. -1 when it
    // overlaps none of them - dragged entirely off-screen - and the caller
    // takes the primary.
    static int MonitorUnder(const ScreenRect& window, const std::vector<ScreenRect>& monitors);

    // Whether a size is one a window may be asked for. The manifest's range.
    static bool IsUsableWindowSize(uint32_t width, uint32_t height);

protected:
    WindowControl() = default;

private:
    Requests m_pending;
};

} // namespace Supersonic
