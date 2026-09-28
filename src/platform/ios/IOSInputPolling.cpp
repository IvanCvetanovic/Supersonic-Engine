// platform/InputPolling.hpp over UIKit's events (IOSApp.mm keeps what they
// left behind; this hands it to Input once a frame).

#include "platform/InputPolling.hpp"

#include "platform/Window.hpp"
#include "platform/ios/IOSApp.hpp"

namespace Supersonic {

namespace {
float g_pendingScroll = 0.0f;
unsigned int g_pendingCharacters[Text::kMaxCharacters]{};
int g_pendingCharacterCount = 0;
} // namespace

void InputPolling::ApplyCursorMode(Window& window) {
    (void)window;
    // The focus flag the desktop reports, for the same readers: a game that
    // pauses when its window loses focus pauses when Control Centre comes down,
    // a call arrives or the app goes to the background. Once a frame, which is
    // what the latch in TakeFocused is counted in. There is no pointer to hide
    // or lock on a touch screen.
    Input::SetWindowFocused(IOS::TakeFocused());
}

void InputPolling::InstallCallbacks(Window& window) {
    // Everything arrives through UIKit's run loop, which IOSApp.mm pumps; there
    // is no callback to install and no ImGui backend to chain after.
    (void)window;
}

void InputPolling::AccumulateScroll(float delta) { g_pendingScroll += delta; }

void InputPolling::AccumulateCharacter(unsigned int codepoint) {
    if (g_pendingCharacterCount >= Text::kMaxCharacters) return;
    g_pendingCharacters[g_pendingCharacterCount++] = codepoint;
}

void InputPolling::Poll(Window& window) {
    (void)window;
    RawInputState state{};
    IOS::FillRawInput(state);

    state.scroll = g_pendingScroll;
    g_pendingScroll = 0.0f;
    for (int i = 0; i < g_pendingCharacterCount && state.textCharacterCount < Text::kMaxCharacters; ++i) {
        state.textCharacters[state.textCharacterCount++] = g_pendingCharacters[i];
    }
    g_pendingCharacterCount = 0;

    Input::Update(state);
}

} // namespace Supersonic
