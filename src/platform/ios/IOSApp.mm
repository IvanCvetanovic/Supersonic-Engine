// The iOS host (IOSApp.hpp): the process's main(), the UIKit app and scene
// delegates, the view the game draws into, and the state their events leave
// for the platform classes beside this file. Objective-C++ under ARC
// (CMakeLists.txt), iOS only.
//
// Every Objective-C class the app needs is in this one file, beside main(), on
// purpose: the engine is a static library, and the linker takes an archive
// member only for a symbol something names. main() is named by the process's
// entry, so this file is always linked; a class in a file of its own that is
// only ever named by string would silently not be.

#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include "platform/ios/IOSApp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

// Key codes only; nothing from GLFW is linked on iOS (CMakeLists.txt).
#include <GLFW/glfw3.h>

#include "imgui.h"

#include "core/Log.hpp"

// The safe-area API (platform/SafeArea.hpp) is answered here where the engine
// has it: each window backend defines SafeArea::Get itself, and on iOS it is
// what remains unsafe INSIDE the game's view (the view itself is sized to the
// safe area; SupersonicViewController says how), in the drawable's pixels.
#if __has_include("platform/SafeArea.hpp")
#include "platform/SafeArea.hpp"
#define SUPERSONIC_IOS_SAFE_AREA 1
#endif

// What @autoreleasepool compiles to. The game's loop never returns to the run
// loop that would drain the pool its Objective-C objects are autoreleased into
// (a drawable, a touch set, every string a frame makes), so WaitUntilDrawable
// drains one of its own each frame; a scoped @autoreleasepool cannot span the
// frame, which begins and ends in different calls.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);

@class SupersonicAppDelegate;

namespace Supersonic::IOS {

namespace {

// One finger, from the touch that put it down to the snapshot after the one
// that lifted it (the Android backend's rule, and for the same reason: a tap
// shorter than a frame is still one frame of contact).
struct Finger {
    const void* touch{nullptr};   // the UITouch, as a key only; null once lifted
    int id{-1};
    glm::vec2 position{0.0f};
    bool reported{false};
    bool lifted{false};
};

struct State {
    int argc{0};
    char** argv{nullptr};

    CAMetalLayer* layer{nil};
    __weak UIView* view{nil};
    float scale{1.0f};
    int width{0};
    int height{0};
    bool sizeChanged{false};
    float insetLeft{0.0f};
    float insetTop{0.0f};
    float insetRight{0.0f};
    float insetBottom{0.0f};

    bool gameStarted{false};
    bool destroyRequested{false};
    bool active{false};
    bool focusLostLatched{false};
    bool background{false};
    bool resumedFromSuspend{false};

    void* framePool{nullptr};

    std::function<void()> surfaceLost;
    std::function<void(bool)> audioSuspend;

    std::array<bool, Key::Last + 1> keysDown{};
    std::array<bool, Key::Last + 1> keysPressed{};   // went down since the last snapshot
    std::vector<unsigned int> typed;

    std::vector<Finger> touches;   // in the order they went down
    int primaryId{-1};             // the finger the mouse follows
    glm::vec2 mousePosition{0.0f};
};

State g;

// ---- The view -----------------------------------------------------------------

void refreshSize(UIView* view) {
    const CGSize bounds = view.bounds.size;
    const int width = static_cast<int>(std::lround(bounds.width * g.scale));
    const int height = static_cast<int>(std::lround(bounds.height * g.scale));
    if (width <= 0 || height <= 0) return;
    if (width != g.width || height != g.height) {
        g.width = width;
        g.height = height;
        g.sizeChanged = true;
    }
    if (g.layer != nil) g.layer.drawableSize = CGSizeMake(width, height);
}

void refreshSafeArea(UIView* view) {
    const UIEdgeInsets insets = view.safeAreaInsets;
    g.insetLeft = static_cast<float>(insets.left) * g.scale;
    g.insetTop = static_cast<float>(insets.top) * g.scale;
    g.insetRight = static_cast<float>(insets.right) * g.scale;
    g.insetBottom = static_cast<float>(insets.bottom) * g.scale;
}

// ---- Input ----------------------------------------------------------------------

bool fingerIdInUse(int candidate) {
    for (const Finger& finger : g.touches) {
        if (finger.id == candidate) return true;
    }
    return false;
}

Finger* findFinger(const void* touch) {
    for (Finger& finger : g.touches) {
        if (finger.touch == touch) return &finger;
    }
    return nullptr;
}

// Everything held is let go when the scene stops receiving events: a finger on
// the glass when Control Centre came down never reports lifting, and a key held
// across it would stay held in the game until pressed again.
void releaseAllInput() {
    g.keysDown.fill(false);
    // A finger no frame has seen yet still gets its one frame; the rest end now.
    g.touches.erase(std::remove_if(g.touches.begin(), g.touches.end(), [](const Finger& f) { return f.reported; }),
                    g.touches.end());
    for (Finger& finger : g.touches) {
        finger.lifted = true;
        finger.touch = nullptr;
    }
    if (!fingerIdInUse(g.primaryId)) g.primaryId = -1;
}

// ImGui has no platform backend here, so the pointer it hit-tests with (the
// engine's own UI canvas reads io.MousePos) is fed from the finger the mouse
// follows, as on Android.
void feedImGuiPointer(bool buttonDown) {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(g.mousePosition.x, g.mousePosition.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, buttonDown);
}

glm::vec2 pixelPosition(UITouch* touch, UIView* view) {
    const CGPoint point = [touch locationInView:view];
    return glm::vec2(static_cast<float>(point.x) * g.scale, static_cast<float>(point.y) * g.scale);
}

void touchesBegan(NSSet<UITouch*>* touches, UIView* view) {
    for (UITouch* touch in touches) {
        const glm::vec2 position = pixelPosition(touch, view);
        // The first finger of a gesture - none other is down - is the one the
        // mouse follows until it lifts, as Android's pointer emulation does;
        // later fingers are contacts only.
        const bool first = std::none_of(g.touches.begin(), g.touches.end(), [](const Finger& f) { return !f.lifted; });
        Finger finger;
        finger.touch = (__bridge const void*)touch;
        // The lowest id free: stable while the finger is down, reused after.
        int fingerId = 0;
        while (fingerIdInUse(fingerId)) ++fingerId;
        finger.id = fingerId;
        finger.position = position;
        g.touches.push_back(finger);
        if (first) {
            g.primaryId = fingerId;
            g.mousePosition = position;
            feedImGuiPointer(true);
        }
    }
}

void touchesMoved(NSSet<UITouch*>* touches, UIView* view) {
    for (UITouch* touch in touches) {
        Finger* finger = findFinger((__bridge const void*)touch);
        if (finger == nullptr) continue;
        finger->position = pixelPosition(touch, view);
        if (finger->id == g.primaryId) {
            g.mousePosition = finger->position;
            feedImGuiPointer(true);
        }
    }
}

void touchesEnded(NSSet<UITouch*>* touches, UIView* view) {
    for (UITouch* touch in touches) {
        Finger* finger = findFinger((__bridge const void*)touch);
        if (finger == nullptr) continue;
        finger->position = pixelPosition(touch, view);
        // The mouse stays where the finger left the glass: moved anywhere else,
        // the game would read the lift as the pointer moving.
        if (finger->id == g.primaryId) {
            g.mousePosition = finger->position;
            feedImGuiPointer(false);
        }
        finger->touch = nullptr;
        if (finger->reported) {
            if (finger->id == g.primaryId) g.primaryId = -1;
            g.touches.erase(g.touches.begin() + (finger - g.touches.data()));
        } else {
            finger->lifted = true;
        }
    }
}

// A hardware keyboard's key (an iPad's, or one paired with a phone), as the
// GLFW code Input speaks. -1 for a key the engine has no name for.
int glfwKeyFor(UIKeyboardHIDUsage usage) {
    if (usage >= UIKeyboardHIDUsageKeyboardA && usage <= UIKeyboardHIDUsageKeyboardZ) {
        return GLFW_KEY_A + static_cast<int>(usage - UIKeyboardHIDUsageKeyboardA);
    }
    if (usage >= UIKeyboardHIDUsageKeyboard1 && usage <= UIKeyboardHIDUsageKeyboard9) {
        return GLFW_KEY_1 + static_cast<int>(usage - UIKeyboardHIDUsageKeyboard1);
    }
    if (usage >= UIKeyboardHIDUsageKeyboardF1 && usage <= UIKeyboardHIDUsageKeyboardF12) {
        return GLFW_KEY_F1 + static_cast<int>(usage - UIKeyboardHIDUsageKeyboardF1);
    }
    switch (usage) {
    case UIKeyboardHIDUsageKeyboard0: return GLFW_KEY_0;
    case UIKeyboardHIDUsageKeyboardReturnOrEnter: return GLFW_KEY_ENTER;
    case UIKeyboardHIDUsageKeypadEnter: return GLFW_KEY_KP_ENTER;
    case UIKeyboardHIDUsageKeyboardEscape: return GLFW_KEY_ESCAPE;
    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: return GLFW_KEY_BACKSPACE;
    case UIKeyboardHIDUsageKeyboardDeleteForward: return GLFW_KEY_DELETE;
    case UIKeyboardHIDUsageKeyboardTab: return GLFW_KEY_TAB;
    case UIKeyboardHIDUsageKeyboardSpacebar: return GLFW_KEY_SPACE;
    case UIKeyboardHIDUsageKeyboardHyphen: return GLFW_KEY_MINUS;
    case UIKeyboardHIDUsageKeyboardEqualSign: return GLFW_KEY_EQUAL;
    case UIKeyboardHIDUsageKeyboardOpenBracket: return GLFW_KEY_LEFT_BRACKET;
    case UIKeyboardHIDUsageKeyboardCloseBracket: return GLFW_KEY_RIGHT_BRACKET;
    case UIKeyboardHIDUsageKeyboardBackslash: return GLFW_KEY_BACKSLASH;
    case UIKeyboardHIDUsageKeyboardSemicolon: return GLFW_KEY_SEMICOLON;
    case UIKeyboardHIDUsageKeyboardQuote: return GLFW_KEY_APOSTROPHE;
    case UIKeyboardHIDUsageKeyboardGraveAccentAndTilde: return GLFW_KEY_GRAVE_ACCENT;
    case UIKeyboardHIDUsageKeyboardComma: return GLFW_KEY_COMMA;
    case UIKeyboardHIDUsageKeyboardPeriod: return GLFW_KEY_PERIOD;
    case UIKeyboardHIDUsageKeyboardSlash: return GLFW_KEY_SLASH;
    case UIKeyboardHIDUsageKeyboardRightArrow: return GLFW_KEY_RIGHT;
    case UIKeyboardHIDUsageKeyboardLeftArrow: return GLFW_KEY_LEFT;
    case UIKeyboardHIDUsageKeyboardDownArrow: return GLFW_KEY_DOWN;
    case UIKeyboardHIDUsageKeyboardUpArrow: return GLFW_KEY_UP;
    case UIKeyboardHIDUsageKeyboardPageUp: return GLFW_KEY_PAGE_UP;
    case UIKeyboardHIDUsageKeyboardPageDown: return GLFW_KEY_PAGE_DOWN;
    case UIKeyboardHIDUsageKeyboardHome: return GLFW_KEY_HOME;
    case UIKeyboardHIDUsageKeyboardEnd: return GLFW_KEY_END;
    case UIKeyboardHIDUsageKeyboardInsert: return GLFW_KEY_INSERT;
    case UIKeyboardHIDUsageKeyboardLeftShift: return GLFW_KEY_LEFT_SHIFT;
    case UIKeyboardHIDUsageKeyboardRightShift: return GLFW_KEY_RIGHT_SHIFT;
    case UIKeyboardHIDUsageKeyboardLeftControl: return GLFW_KEY_LEFT_CONTROL;
    case UIKeyboardHIDUsageKeyboardRightControl: return GLFW_KEY_RIGHT_CONTROL;
    case UIKeyboardHIDUsageKeyboardLeftAlt: return GLFW_KEY_LEFT_ALT;
    case UIKeyboardHIDUsageKeyboardRightAlt: return GLFW_KEY_RIGHT_ALT;
    default: return -1;
    }
}

// The keys a press set carries that the engine has a name for; the rest are
// left for UIKit. True when every press was one of them.
bool handlePresses(NSSet<UIPress*>* presses, bool down, NSMutableSet<UIPress*>* unhandled) {
    for (UIPress* press in presses) {
        UIKey* key = press.key;
        const int code = key != nil ? glfwKeyFor(key.keyCode) : -1;
        if (code < 0) {
            [unhandled addObject:press];
            continue;
        }
        g.keysDown[static_cast<std::size_t>(code)] = down;
        if (!down) continue;
        g.keysPressed[static_cast<std::size_t>(code)] = true;
        // What it types, for the few printable keys a name needs. Characters
        // below space, DEL and the private-use range UIKit spells function and
        // arrow keys in are not text.
        NSData* utf32 = [key.characters dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
        const auto* codepoints = static_cast<const uint32_t*>(utf32.bytes);
        const std::size_t count = utf32.length / sizeof(uint32_t);
        for (std::size_t i = 0; i < count; ++i) {
            const uint32_t c = codepoints[i];
            const bool text = c >= 0x20 && c != 0x7F && !(c >= 0xF700 && c <= 0xF8FF);
            if (text && g.typed.size() < static_cast<std::size_t>(Text::kMaxCharacters)) g.typed.push_back(c);
        }
    }
    return unhandled.count == 0;
}

// ---- Lifecycle ------------------------------------------------------------------

// Everything UIKit has to deliver, without waiting for more.
void pumpPending() {
    while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE) == kCFRunLoopRunHandledSource) {
    }
}

void configureAudioSession() {
    // Ambient: the game's sound mixes with whatever else is playing and
    // follows the ring/silent switch, as a game's sound effects should.
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    const auto describe = [](NSError* failure) {
        return failure != nil ? std::string(failure.localizedDescription.UTF8String) : std::string("no detail");
    };
    if (![session setCategory:AVAudioSessionCategoryAmbient error:&error]) {
        SUPERSONIC_LOG_WARN("iOS") << "Audio session category: " << describe(error);
    }
    if (![session setActive:YES error:&error]) {
        SUPERSONIC_LOG_WARN("iOS") << "Audio session activation: " << describe(error);
    }
    // A call or an alarm takes the session; the output unit stops with it and
    // starts again once it is handed back.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:session
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  const NSUInteger type =
                      [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
                  if (type == AVAudioSessionInterruptionTypeBegan) {
                      if (g.audioSuspend) g.audioSuspend(true);
                  } else if (type == AVAudioSessionInterruptionTypeEnded) {
                      [[AVAudioSession sharedInstance] setActive:YES error:nil];
                      if (!g.background && g.audioSuspend) g.audioSuspend(false);
                  }
                }];
}

void popFramePool() {
    if (g.framePool != nullptr) {
        objc_autoreleasePoolPop(g.framePool);
        g.framePool = nullptr;
    }
}

} // namespace

// ---- For the platform classes ---------------------------------------------------

void* CurrentLayer() { return (__bridge void*)g.layer; }

bool DestroyRequested() { return g.destroyRequested; }

bool TakeFocused() {
    const bool focused = g.active && !g.focusLostLatched;
    g.focusLostLatched = false;
    return focused;
}

void WaitUntilDrawable() {
    popFramePool();
    pumpPending();
    bool waited = false;
    while (!g.destroyRequested && (g.background || g.layer == nil)) {
        if (!waited) SUPERSONIC_LOG_INFO("iOS") << "In the background: the frame waits.";
        waited = true;
        // Wakes for whatever UIKit delivers; the timeout only bounds the wait.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, TRUE);
    }
    if (waited) {
        g.resumedFromSuspend = true;
        if (!g.destroyRequested) SUPERSONIC_LOG_INFO("iOS") << "Back in the foreground.";
    }
    g.framePool = objc_autoreleasePoolPush();
}

void WindowSize(int& width, int& height) {
    width = g.layer != nil ? g.width : 0;
    height = g.layer != nil ? g.height : 0;
}

bool TakeWindowSizeChanged() {
    const bool changed = g.sizeChanged;
    g.sizeChanged = false;
    return changed;
}

bool TakeResumedFromSuspend() {
    const bool resumed = g.resumedFromSuspend;
    g.resumedFromSuspend = false;
    return resumed;
}

void SetSurfaceLostCallback(std::function<void()> callback) { g.surfaceLost = std::move(callback); }

void SetAudioSuspendHandler(std::function<void(bool)> handler) {
    g.audioSuspend = std::move(handler);
    // Installed while the app is in the background, it starts suspended.
    if (g.audioSuspend && g.background) g.audioSuspend(true);
}

void FillRawInput(RawInputState& state) {
    for (std::size_t key = 0; key < g.keysDown.size(); ++key) {
        state.keys[key] = g.keysDown[key] || g.keysPressed[key];
    }
    g.keysPressed.fill(false);

    // Every finger that is down, or went down since the last snapshot, in the
    // order they landed; the mouse button is down for exactly as long as the
    // first finger of the gesture is in a snapshot, which makes a tap a click.
    // Input::SynthesiseMouseContact is not called - these are real contacts.
    int count = 0;
    bool primaryDown = false;
    for (Finger& finger : g.touches) {
        if (finger.id == g.primaryId) primaryDown = true;
        if (count < Touch::kMaxContacts) {
            state.contacts[count].id = finger.id;
            state.contacts[count].position = finger.position;
            ++count;
        }
        finger.reported = true;
    }
    state.contactCount = count;
    state.mouseButtons[MouseButton::Left] = primaryDown;
    state.mousePosition = g.mousePosition;

    // Lifted fingers have had their frame.
    for (const Finger& finger : g.touches) {
        if (finger.lifted && finger.id == g.primaryId) g.primaryId = -1;
    }
    g.touches.erase(std::remove_if(g.touches.begin(), g.touches.end(), [](const Finger& f) { return f.lifted; }),
                    g.touches.end());

    const int typed = std::min(static_cast<int>(g.typed.size()), Text::kMaxCharacters);
    for (int i = 0; i < typed; ++i) state.textCharacters[i] = g.typed[static_cast<std::size_t>(i)];
    state.textCharacterCount = typed;
    g.typed.clear();

    GamepadState first{};
    if (GetGamepad(0, first)) {
        state.padConnected = true;
        for (int button = 0; button < Pad::ButtonCount; ++button) state.padButtons[button] = first.buttons[button];
        for (int a = 0; a < Pad::AxisCount; ++a) state.padAxes[a] = first.axes[a];
    }
}

namespace {
// GameController's controllers that have the extended (two-stick) profile:
// the ones Pad's standard layout describes.
NSArray<GCController*>* extendedGamepads() {
    NSMutableArray<GCController*>* pads = [NSMutableArray array];
    for (GCController* controller in GCController.controllers) {
        if (controller.extendedGamepad != nil) [pads addObject:controller];
        if (pads.count >= static_cast<NSUInteger>(Gamepads::kMaxGamepads)) break;
    }
    return pads;
}
} // namespace

int GamepadCount() { return static_cast<int>(extendedGamepads().count); }

bool GetGamepad(int index, GamepadState& out) {
    NSArray<GCController*>* pads = extendedGamepads();
    if (index < 0 || static_cast<NSUInteger>(index) >= pads.count) return false;
    GCExtendedGamepad* pad = pads[static_cast<NSUInteger>(index)].extendedGamepad;
    GamepadState state{};
    // Buttons by meaning, as Pad names them; a button the pad lacks is nil,
    // and a message to nil answers NO.
    state.buttons[Pad::A] = pad.buttonA.pressed;
    state.buttons[Pad::B] = pad.buttonB.pressed;
    state.buttons[Pad::X] = pad.buttonX.pressed;
    state.buttons[Pad::Y] = pad.buttonY.pressed;
    state.buttons[Pad::LeftBumper] = pad.leftShoulder.pressed;
    state.buttons[Pad::RightBumper] = pad.rightShoulder.pressed;
    state.buttons[Pad::Back] = pad.buttonOptions.pressed;
    state.buttons[Pad::Start] = pad.buttonMenu.pressed;
    state.buttons[Pad::Guide] = pad.buttonHome.pressed;
    state.buttons[Pad::LeftThumb] = pad.leftThumbstickButton.pressed;
    state.buttons[Pad::RightThumb] = pad.rightThumbstickButton.pressed;
    state.buttons[Pad::DpadUp] = pad.dpad.up.pressed;
    state.buttons[Pad::DpadRight] = pad.dpad.right.pressed;
    state.buttons[Pad::DpadDown] = pad.dpad.down.pressed;
    state.buttons[Pad::DpadLeft] = pad.dpad.left.pressed;
    // GameController's sticks are y up; Pad's (GLFW's) are y down. Its
    // triggers run 0..1; Pad's rest at -1.
    state.axes[Pad::LeftX] = pad.leftThumbstick.xAxis.value;
    state.axes[Pad::LeftY] = -pad.leftThumbstick.yAxis.value;
    state.axes[Pad::RightX] = pad.rightThumbstick.xAxis.value;
    state.axes[Pad::RightY] = -pad.rightThumbstick.yAxis.value;
    state.axes[Pad::LeftTrigger] = pad.leftTrigger.value * 2.0f - 1.0f;
    state.axes[Pad::RightTrigger] = pad.rightTrigger.value * 2.0f - 1.0f;
    out = state;
    return true;
}

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace Supersonic::IOS

#if defined(SUPERSONIC_IOS_SAFE_AREA)
namespace Supersonic::SafeArea {

// In the drawable's pixels, which is what the swapchain, the mouse and the
// touch contacts are in. Updated by the view as UIKit reports changes.
SafeAreaInsets Get() {
    using Supersonic::IOS::g;
    SafeAreaInsets insets;
    insets.left = g.insetLeft;
    insets.top = g.insetTop;
    insets.right = g.insetRight;
    insets.bottom = g.insetBottom;
    return insets;
}

} // namespace Supersonic::SafeArea
#endif

// ---- UIKit ------------------------------------------------------------------------

// The view the game draws into: a CAMetalLayer MoltenVK presents to, and the
// touches that land on it.
@interface SupersonicMetalView : UIView
@end

@implementation SupersonicMetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self != nil) {
        self.multipleTouchEnabled = YES;
        self.opaque = YES;
        self.backgroundColor = UIColor.blackColor;
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    Supersonic::IOS::refreshSize(self);
}

- (void)safeAreaInsetsDidChange {
    [super safeAreaInsetsDidChange];
    Supersonic::IOS::refreshSafeArea(self);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    Supersonic::IOS::touchesBegan(touches, self);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    Supersonic::IOS::touchesMoved(touches, self);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    Supersonic::IOS::touchesEnded(touches, self);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    // The system took the gesture (an edge swipe, an alert): every finger
    // ends where it was.
    (void)event;
    Supersonic::IOS::touchesEnded(touches, self);
}

@end

// Landscape only, nothing of the system's drawn over the game that can be
// hidden, and the edges' system gestures asked to wait for a second swipe, so a
// thumb on an on-screen button near the edge does not leave the app.
//
// The game is drawn CLEAR of the notch or Dynamic Island and the rounded
// corners beside it: the Metal view is the safe area's width and height (black
// outside it), not the screen's, so no HUD is cut. Down to the bottom edge,
// though - the home indicator hides itself and covers nothing of the picture -
// and what remains unsafe inside the view, that band, is what SafeArea::Get
// reports, for on-screen controls to keep clear of.
@interface SupersonicViewController : UIViewController
@property(strong, nonatomic) SupersonicMetalView* metalView;
@end

@implementation SupersonicViewController

- (void)loadView {
    UIView* root = [[UIView alloc] initWithFrame:CGRectZero];
    root.backgroundColor = UIColor.blackColor;
    SupersonicMetalView* metal = [[SupersonicMetalView alloc] initWithFrame:CGRectZero];
    metal.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:metal];
    UILayoutGuide* safe = root.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [metal.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [metal.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [metal.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [metal.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
    ]];
    self.metalView = metal;
    self.view = root;
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeAll;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    // Presses (a hardware keyboard's keys) go to the first responder.
    [self becomeFirstResponder];
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    NSMutableSet<UIPress*>* unhandled = [NSMutableSet set];
    if (!Supersonic::IOS::handlePresses(presses, true, unhandled)) [super pressesBegan:unhandled withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    NSMutableSet<UIPress*>* unhandled = [NSMutableSet set];
    if (!Supersonic::IOS::handlePresses(presses, false, unhandled)) [super pressesEnded:unhandled withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    NSMutableSet<UIPress*>* unhandled = [NSMutableSet set];
    if (!Supersonic::IOS::handlePresses(presses, false, unhandled)) [super pressesCancelled:unhandled withEvent:event];
}

@end

@interface SupersonicAppDelegate : UIResponder <UIApplicationDelegate>
- (void)runGame;
@end

// The app's one scene: its window, and the lifecycle a game cares about.
@interface SupersonicSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation SupersonicSceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)connectionOptions {
    (void)session;
    (void)connectionOptions;
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    UIWindowScene* windowScene = (UIWindowScene*)scene;

    using namespace Supersonic::IOS;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    SupersonicViewController* controller = [[SupersonicViewController alloc] init];
    [controller loadViewIfNeeded];   // and with it the Metal view
    UIView* view = controller.metalView;
    // The display's own pixels, not the points UIKit lays out in - set before
    // the first layout, which is what sizes the drawable.
    g.scale = static_cast<float>(windowScene.screen.nativeScale);
    view.contentScaleFactor = windowScene.screen.nativeScale;
    g.view = view;
    g.layer = (CAMetalLayer*)view.layer;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    // Laid out now, so the view has its safe-area size before the game makes
    // a swapchain for it.
    [self.window layoutIfNeeded];
    refreshSize(view);
    refreshSafeArea(view);
    const UIEdgeInsets screenInsets = self.window.safeAreaInsets;
    SUPERSONIC_LOG_INFO("iOS") << "Scene connected: the game's view " << g.width << "x" << g.height
                               << " pixels at scale " << g.scale << ", inside screen safe-area insets (points) L"
                               << screenInsets.left << " T" << screenInsets.top << " R" << screenInsets.right << " B"
                               << screenInsets.bottom << "; insets left inside the view (pixels) L" << g.insetLeft
                               << " T" << g.insetTop << " R" << g.insetRight << " B" << g.insetBottom << ".";

    // The game is entered once, from the run loop's next turn, rather than
    // from inside UIKit's connection callback: a timer, not the main dispatch
    // queue, which would stay blocked for as long as the game runs.
    if (!g.gameStarted) {
        g.gameStarted = true;
        SupersonicAppDelegate* app = (SupersonicAppDelegate*)UIApplication.sharedApplication.delegate;
        [app performSelector:@selector(runGame) withObject:nil afterDelay:0.0];
    }
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
    (void)scene;
    Supersonic::IOS::g.active = true;
}

- (void)sceneWillResignActive:(UIScene*)scene {
    (void)scene;
    using namespace Supersonic::IOS;
    g.active = false;
    g.focusLostLatched = true;
    releaseAllInput();
}

- (void)sceneDidEnterBackground:(UIScene*)scene {
    (void)scene;
    using namespace Supersonic::IOS;
    SUPERSONIC_LOG_INFO("iOS") << "Lifecycle: entered the background.";
    g.background = true;
    releaseAllInput();
    // A suspended app may own no GPU work: the swapchain and surface go now,
    // and the next frame on screen builds them again.
    if (g.surfaceLost) g.surfaceLost();
    if (g.audioSuspend) g.audioSuspend(true);
}

- (void)sceneWillEnterForeground:(UIScene*)scene {
    (void)scene;
    using namespace Supersonic::IOS;
    SUPERSONIC_LOG_INFO("iOS") << "Lifecycle: entering the foreground.";
    g.background = false;
    if (g.audioSuspend) g.audioSuspend(false);
}

- (void)sceneDidDisconnect:(UIScene*)scene {
    (void)scene;
    using namespace Supersonic::IOS;
    SUPERSONIC_LOG_INFO("iOS") << "Lifecycle: the scene was disconnected; finishing.";
    if (g.surfaceLost) g.surfaceLost();
    g.layer = nil;
    g.destroyRequested = true;
}

@end

@implementation SupersonicAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)launchOptions;
    // A game is played without touching the screen for minutes at a time with
    // a pad; the display must not dim and lock under it.
    application.idleTimerDisabled = YES;
    Supersonic::IOS::configureAudioSession();
    return YES;
}

// The one scene's configuration, in code rather than in Info.plist, so the
// scene delegate is named by a symbol the linker keeps (see the top of this
// file). The game's Info.plist only opts in to scenes (UIApplicationSceneManifest).
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
                                   options:(UISceneConnectionOptions*)options {
    (void)application;
    (void)options;
    UISceneConfiguration* configuration =
        [[UISceneConfiguration alloc] initWithName:@"Supersonic" sessionRole:connectingSceneSession.role];
    configuration.delegateClass = [SupersonicSceneDelegate class];
    return configuration;
}

- (void)applicationWillTerminate:(UIApplication*)application {
    (void)application;
    Supersonic::IOS::g.destroyRequested = true;
}

- (void)runGame {
    using namespace Supersonic::IOS;
    // Entered with the scene active, as android_main enters with a window and
    // the activity resumed: the first frame then sees the focus it has. Bounded,
    // so a scene that never activates still runs (and is paused by the game).
    const double deadline = Now() + 2.0;
    while (!g.active && !g.destroyRequested && Now() < deadline) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, TRUE);
    }

    const int status = SupersonicMain(g.argc, g.argv);
    popFramePool();
    SUPERSONIC_LOG_INFO("iOS") << "The game returned " << status << "; exiting.";
    std::fflush(stdout);
    std::fflush(stderr);
    // There is nothing to go back to: UIKit would keep showing the last frame
    // of a game that has ended. _exit, not exit, as on Android: static
    // destructors after a game that failed half-way (worker threads still
    // joinable) end in std::terminate, and UIKit is still mid-callout.
    _exit(status);
}

@end

// ---- The process's entry ------------------------------------------------------

int main(int argc, char* argv[]) {
    Supersonic::IOS::g.argc = argc;
    Supersonic::IOS::g.argv = argv;
    // Line by line, so a run watched through `simctl launch --console` shows
    // the log as it happens.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([SupersonicAppDelegate class]));
    }
}
