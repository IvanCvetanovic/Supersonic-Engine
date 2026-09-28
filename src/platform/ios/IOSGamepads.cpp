// platform/Gamepads.hpp on iOS: the extended gamepads GameController lists
// (IOSApp.mm reads them).

#include "platform/Gamepads.hpp"

#include "platform/ios/IOSApp.hpp"

namespace Supersonic::Gamepads {

int Count() { return IOS::GamepadCount(); }

bool Get(int index, GamepadState& out) { return IOS::GetGamepad(index, out); }

} // namespace Supersonic::Gamepads
