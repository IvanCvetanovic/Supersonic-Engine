// platform/InputPolling.hpp over Android's input events (AndroidApp.cpp keeps
// what they left behind; this hands it to Input once a frame).

#include "platform/InputPolling.hpp"

#include "platform/Window.hpp"
#include "platform/android/AndroidApp.hpp"

namespace Supersonic {

namespace {
float g_pendingScroll = 0.0f;
unsigned int g_pendingCharacters[Text::kMaxCharacters]{};
int g_pendingCharacterCount = 0;
} // namespace

void InputPolling::ApplyCursorMode(Window& window) {
    (void)window;
    // The same focus flag the desktop reports, and for the same readers: a game
    // that pauses when its window loses focus pauses when the notification
    // shade comes down or another app takes the screen. There is no pointer to
    // hide or lock on a touch screen, so the cursor mode itself has nothing to
    // be applied to.
    Input::SetWindowFocused(Android::IsFocused());
}

void InputPolling::InstallCallbacks(Window& window) {
    // Everything arrives through the looper AndroidApp.cpp reads; there is no
    // callback to install and no ImGui backend to chain after.
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
    Android::FillRawInput(state);

    state.scroll = g_pendingScroll;
    g_pendingScroll = 0.0f;
    for (int i = 0; i < g_pendingCharacterCount && state.textCharacterCount < Text::kMaxCharacters; ++i) {
        state.textCharacters[state.textCharacterCount++] = g_pendingCharacters[i];
    }
    g_pendingCharacterCount = 0;

    Input::Update(state);
}

} // namespace Supersonic
