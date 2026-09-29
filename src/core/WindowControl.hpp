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
    // opens at. Zeroes when there is no monitor to ask. While SetFullscreenMode
    // has switched the monitor to another mode, the mode it goes back to - the
    // desktop's - rather than the switched one, which is WindowSize().
    virtual DisplayMode DesktopMode() const = 0;

    // ---- Refresh rates ----------------------------------------------------

    // How a rate is named where it is not a number of Hz: SetFullscreenMode's
    // and ChooseFullscreenMode's `refreshRate`, and SetPreferredRefreshRate's.
    //
    // kDesktopRefreshRate, zero, is what a request that names no rate has
    // always meant, so a Requests left zeroed means it too: for a fullscreen
    // mode, the desktop's rate where the monitor offers the size at it, else
    // the highest; for a preferred rate, no preference - the platform's own.
    // kHighestRefreshRate is the highest the monitor offers. Any other value
    // is that rate exactly, as DisplayModes lists it.
    static constexpr uint32_t kDesktopRefreshRate = 0;
    static constexpr uint32_t kHighestRefreshRate = 0xFFFFFFFFu;

    // ---- What a game asks for ---------------------------------------------

    // Cover the monitor the window is mostly on, at that monitor's current
    // mode - so no mode switch, and nothing for the display to resynchronise
    // on the way in or out. false returns to the size and position the window
    // had before, maximised again if it was.
    //
    // Applied at the top of the next frame. The last request before then wins,
    // a SetFullscreenMode included: true after one covers the monitor at its
    // current mode instead.
    void SetFullscreen(bool fullscreen);

    // Cover the monitor at a mode of this size, switching the display to it -
    // the exclusive fullscreen of a 2000s game's resolution list, where the
    // picture is rendered at that size and the monitor scales it. Opt-in:
    // SetFullscreen(true) never switches.
    //
    // The rate is the desktop's when the monitor offers this size at it, else
    // the highest it offers the size at; the desktop's own size is the
    // desktop's mode, so asking for it switches nothing (and switches back
    // from another mode). Already fullscreen, it changes mode in place and
    // keeps the windowed rectangle SetFullscreen(false) returns to. Leaving
    // fullscreen, or losing the focus (GLFW_AUTO_ICONIFY), puts the desktop's
    // mode back.
    //
    // Refused - false, nothing latched - for a size the monitor does not list
    // (DisplayModes) and is not the desktop's: a settings file saved on another
    // monitor asks for modes this one may not have, and the caller falls back
    // to SetFullscreen(true) rather than to a mode the platform would round to.
    //
    // Applied at the top of the next frame. The last request before then wins.
    // Ignored where the window is the screen (Android, iOS).
    //
    // `refreshRate` (opt-in; the default is the rule above): kHighestRefreshRate
    // runs the size at the highest rate the monitor offers it at, and a number
    // at exactly that rate - refused, as an unlisted size is, when the monitor
    // does not offer the size at it (the desktop's own mode always counts as
    // offered). A game offering "the best refresh rate" asks for the highest;
    // one remembering the player's pick asks for that number and falls back
    // to the highest when a monitor lacks it.
    bool SetFullscreenMode(uint32_t width, uint32_t height, uint32_t refreshRate = kDesktopRefreshRate);

    // The windowed size, applied at the top of the next frame. While
    // fullscreen it is the size the window comes back at.
    //
    // Refused - false, nothing latched - outside the range a manifest or
    // `--window` is held to (GameManifest::kMinimumExtent..kMaximumExtent): a
    // settings file with a typo in it should leave the window alone rather
    // than open one nobody can see.
    //
    // `centre` (opt-in; false keeps the top-left where it is, as it always
    // did): the window is also centred on its monitor's work area - the
    // screen less the taskbar and docks - title bar included, and never put
    // above or left of the work area, so a window taller than the work area
    // keeps its title bar where it can be grabbed. While fullscreen, the
    // centred rectangle is the one the window comes back at.
    //
    // This and FitWindowToMonitor are one request: the last of them wins.
    bool SetWindowedSize(uint32_t width, uint32_t height, bool centre = false);

    // The window sized to its monitor and centred on it, at the top of the
    // next frame: the largest size of the monitor's own shape (its desktop
    // mode's) within `fraction` of its work area each way, in whole pixels
    // (FitWindowedSize), placed as SetWindowedSize(..., true) places a size.
    // For a game's "automatic" window, which should be as large as it can be
    // while the taskbar and the title bar stay in view, on any monitor,
    // without the game knowing what the monitor is. Measured when applied, so
    // it follows the monitor the window is on then. While fullscreen, it is
    // the rectangle the window comes back at.
    //
    // Refused - false, nothing latched - for a fraction outside (0, 1].
    // Ignored where the window is the screen (Android, iOS).
    bool FitWindowToMonitor(float fraction);

    // The refresh rate the game would like the display to run at while it is
    // shown, where the platform chooses that for the app rather than through
    // a fullscreen mode: kHighestRefreshRate, a number of Hz, or
    // kDesktopRefreshRate to withdraw the preference. Android asks the
    // activity for the display mode of that rate at the current resolution
    // (WindowManager.LayoutParams.preferredDisplayModeId, API 23) and, from API
    // 30, sets the surface's frame rate (ANativeWindow_setFrameRate), again for
    // every new surface; the display may still decide otherwise (a battery
    // saver, a thermal limit). Logged there. The desktop takes it and does
    // nothing: a window runs at the compositor's rate, and fullscreen at its
    // mode's (SetFullscreenMode). Applied at the top of the next frame.
    void SetPreferredRefreshRate(uint32_t refreshRate);

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
        // With fullscreen: the size SetFullscreenMode asked for. Zeroes for
        // SetFullscreen(true), the monitor's current mode.
        glm::uvec2 fullscreenMode{0u, 0u};
        // With a fullscreenMode: the rate it asked for, kDesktopRefreshRate
        // when it named none.
        uint32_t fullscreenRate{kDesktopRefreshRate};

        bool setWindowedSize{false};
        glm::uvec2 windowedSize{0u, 0u};
        // With setWindowedSize: SetWindowedSize's `centre`.
        bool centreWindow{false};

        // FitWindowToMonitor, and its fraction of the work area.
        bool fitWindow{false};
        float fitFraction{0.0f};

        // SetPreferredRefreshRate.
        bool setRefreshRate{false};
        uint32_t refreshRate{kDesktopRefreshRate};

        bool Any() const { return setFullscreen || setWindowedSize || fitWindow || setRefreshRate; }
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

    // The mode SetFullscreenMode covers a monitor at for a size, from that
    // monitor's modes (DisplayModes) and its desktop mode: the desktop's own
    // mode for the desktop's size; else that size at the desktop's rate when
    // it is offered there - the rate the monitor and its cable are known to
    // run - else at the highest rate it is offered at. Zeroes when the size is
    // neither listed nor the desktop's. The same answer refuses a request when
    // it is made and chooses the mode when it is applied, so the two cannot
    // disagree.
    //
    // `refreshRate` as SetFullscreenMode takes it. kHighestRefreshRate: the
    // highest rate among the size's listed modes and, for the desktop's size,
    // the desktop's own mode - which a platform can leave out of its list, and
    // which is the answer when nothing listed beats it. A number: the mode of
    // exactly that rate, the desktop's own for the desktop's size and rate,
    // else zeroes. The default is the rule above, unchanged.
    static DisplayMode ChooseFullscreenMode(const std::vector<DisplayMode>& modes,
                                            const DisplayMode& desktop, uint32_t width,
                                            uint32_t height,
                                            uint32_t refreshRate = kDesktopRefreshRate);

    // Every rate a size can be driven at, lowest first, each once: the
    // listed modes' of that size, and the desktop's rate at the desktop's
    // size. A rate the platform reports as 0 - it does not know, as a virtual
    // X server's modes say - is left out: it cannot be asked for by number.
    // What a menu offers as "refresh rate" for the size it would go
    // fullscreen at.
    static std::vector<uint32_t> RefreshRatesAt(const std::vector<DisplayMode>& modes,
                                                const DisplayMode& desktop, uint32_t width,
                                                uint32_t height);

    // FitWindowToMonitor's size: the largest of the desktop mode's shape
    // within `fraction` of the work area's width and of its height, each
    // extent floored to a whole pixel. The work area is the desktop's size
    // when the platform gives none; the shape is the work area's own when
    // there is no desktop mode. Never below GameManifest::kMinimumExtent.
    // Zeroes when there is neither, or the fraction is not in (0, 1].
    static glm::uvec2 FitWindowedSize(const ScreenRect& workArea, const DisplayMode& desktop,
                                      float fraction);

    // The width of a window's frame on each side of its client area - the
    // title bar is `top` - in screen coordinates.
    struct FrameInsets {
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};
    };

    // Where a client area of `clientSize` goes for its window, frame included,
    // to be centred on the work area: the client area's top-left. Never above
    // or left of the work area, so a window larger than it keeps its title bar
    // and its left edge on screen and hangs off the bottom and the right.
    static glm::ivec2 CentredWindowPosition(const ScreenRect& workArea, glm::uvec2 clientSize,
                                            const FrameInsets& frame);

protected:
    WindowControl() = default;

private:
    Requests m_pending;
};

} // namespace Supersonic
