#pragma once

#include "platform/PlatformDefs.hpp"

#if defined(ENGINE_PLATFORM_ANDROID)
#include <android/native_window.h>
#include <android_native_app_glue.h>

namespace Engine {

class AndroidNativeApp {
public:
    static void InitAndroidApp(struct android_app* appState);
    static ANativeWindow* GetNativeWindow();
    static bool IsAppInitialized();

private:
    static ANativeWindow* s_window;
    static bool s_initialized;
};

} // namespace Engine
#endif
