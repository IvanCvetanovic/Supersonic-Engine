#pragma once

// WHICH WINDOW SYSTEM THIS BUILD REACHES THE SCREEN THROUGH, as one switch.
//
// Desktop builds reach it through GLFW: the window, the Vulkan surface, the
// cursor, the keys and the pads all come from there, and the files that talk to
// it call GLFW directly. A phone has no GLFW. Android hands the process an
// ANativeWindow and a looper, and iOS will hand it a CAMetalLayer - each is a
// window the engine did not create and cannot keep, because the platform takes
// it away whenever the app leaves the screen.
//
// So the handful of places that touch the window system branch on THIS, rather
// than on a list of platforms: `#if SUPERSONIC_WINDOW_GLFW` keeps the desktop
// code exactly as it was, and the other branch is the native-surface seam that
// src/platform/android/ implements today. An iOS backend joins it by adding
// its platform to the condition below and implementing the same few functions
// (platform/Window.hpp's native-surface Window) - nothing above the seam
// changes.
//
// Preprocessor only, with no includes, on purpose. PlatformDefs.hpp would be
// the obvious home, and it cannot be included by a desktop header: it defines
// VK_USE_PLATFORM_WIN32_KHR, which makes every later vulkan.h pull in
// windows.h and its min/max macros.
#if defined(SUPERSONIC_PLATFORM_ANDROID) || defined(__ANDROID__)
#define SUPERSONIC_WINDOW_GLFW 0
#else
#define SUPERSONIC_WINDOW_GLFW 1
#endif
