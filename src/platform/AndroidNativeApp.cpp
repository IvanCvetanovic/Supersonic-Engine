#include "platform/AndroidNativeApp.hpp"

#if defined(SUPERSONIC_PLATFORM_ANDROID)
#include <iostream>

namespace Supersonic {

ANativeWindow* AndroidNativeApp::s_window = nullptr;
bool AndroidNativeApp::s_initialized = false;

void AndroidNativeApp::InitAndroidApp(struct android_app* appState) {
    if (!appState) return;

    s_window = appState->window;
    s_initialized = (s_window != nullptr);

    std::cout << "[AndroidNativeApp] Initialized Android ANativeWindow Vulkan Surface." << std::endl;
}

ANativeWindow* AndroidNativeApp::GetNativeWindow() {
    return s_window;
}

bool AndroidNativeApp::IsAppInitialized() {
    return s_initialized;
}

} // namespace Supersonic
#endif
