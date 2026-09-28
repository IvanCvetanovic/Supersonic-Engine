// platform/Gamepads.hpp on Android: the pads AndroidApp.cpp has heard from.

#include "platform/Gamepads.hpp"

#include "platform/android/AndroidApp.hpp"

namespace Supersonic::Gamepads {

int Count() { return Android::GamepadCount(); }

bool Get(int index, GamepadState& out) { return Android::GetGamepad(index, out); }

} // namespace Supersonic::Gamepads
