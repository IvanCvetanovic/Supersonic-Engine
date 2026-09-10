#pragma once

#include <string>

#include "platform/PlatformDefs.hpp"
#include "sim/Progression.hpp"

namespace WolfBrigade {

// Which control scheme hero mode uses, from `scripts/input/control_scheme.gd`.
//
// TOUCH is the on-screen joystick and attack button; DESKTOP steers with WASD
// and strikes with the primary click. AUTO detects the platform: a mobile OS
// resolves to touch and everything else to desktop, so a touchscreen Windows
// laptop is desktop under AUTO - which is exactly the case the Settings
// override exists for.
//
// The preference is the Profile's, which already saves it as
// `control_scheme`; this only resolves it. Resolution happens at USE time,
// when hero mode is entered, so a changed setting applies to the next match
// without a restart.
namespace ControlScheme {

inline constexpr const char* kAuto = "auto";
inline constexpr const char* kDesktop = "desktop";
inline constexpr const char* kTouch = "touch";

// Godot's `OS.has_feature("mobile")`, answered when the engine is built:
// Android and iOS are mobile, and every other platform it builds for is not.
inline constexpr bool kMobilePlatform =
#if defined(SUPERSONIC_PLATFORM_ANDROID) || defined(SUPERSONIC_PLATFORM_IOS)
    true;
#else
    false;
#endif

// The scheme in effect for a stored preference, and never AUTO. Anything but
// an explicit desktop or touch - AUTO, empty, or a value a hand-edited save
// invented - is detected, which is the original's reading.
//
// `mobile` is a parameter rather than the constant above so that both answers
// are testable on the one machine a suite runs on.
inline std::string Resolve(const std::string& pref, bool mobile) {
    if (pref == kDesktop || pref == kTouch) return pref;
    return mobile ? kTouch : kDesktop;
}

inline std::string Resolved(const Profile& profile) {
    return Resolve(profile.ControlScheme(kAuto), kMobilePlatform);
}

// The Settings button's label for a stored preference. AUTO spells out what
// it detected so the player can see it worked: "Auto (Touch)" on a phone.
inline std::string Label(const std::string& pref, bool mobile) {
    if (pref == kDesktop) return "PC";
    if (pref == kTouch) return "Touch";
    return std::string("Auto (") + (mobile ? "Touch" : "PC") + ")";
}

} // namespace ControlScheme
} // namespace WolfBrigade
