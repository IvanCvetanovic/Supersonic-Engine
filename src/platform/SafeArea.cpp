#include "platform/SafeArea.hpp"

#include "platform/WindowBackend.hpp"

// Only where the window is GLFW's. A native-surface backend defines Get itself
// (src/platform/android/AndroidApp.cpp), from what its platform reports.
#if SUPERSONIC_WINDOW_GLFW

namespace Supersonic::SafeArea {

// A desktop window is a plain rectangle: nothing in it is covered.
SafeAreaInsets Get() { return {}; }

} // namespace Supersonic::SafeArea

#endif
