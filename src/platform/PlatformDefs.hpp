#pragma once

// Platform Detection Macros
#if defined(_WIN32) || defined(_WIN64)
    #define ENGINE_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
    #define ENGINE_PLATFORM_ANDROID 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #define ENGINE_PLATFORM_IOS 1
    #else
        #define ENGINE_PLATFORM_MACOS 1
    #endif
#elif defined(__linux__)
    #define ENGINE_PLATFORM_LINUX 1
#else
    #error "Unsupported Target Platform!"
#endif

// Vulkan Surface Extension Macros
#if defined(ENGINE_PLATFORM_WINDOWS)
    #define VK_USE_PLATFORM_WIN32_KHR 1
#elif defined(ENGINE_PLATFORM_ANDROID)
    #define VK_USE_PLATFORM_ANDROID_KHR 1
#elif defined(ENGINE_PLATFORM_MACOS)
    #define VK_USE_PLATFORM_MACOS_MVK 1
#elif defined(ENGINE_PLATFORM_IOS)
    #define VK_USE_PLATFORM_METAL_EXT 1
#elif defined(ENGINE_PLATFORM_LINUX)
    #define VK_USE_PLATFORM_XCB_KHR 1
#endif
