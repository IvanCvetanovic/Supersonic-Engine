#pragma once

#include "imgui.h"
#include <glm/glm.hpp>
#include <string>

namespace Supersonic {

// The editor's palette, derived from the engine's own branding rather than
// from a stock IDE theme.
//
// The mark is a Mach dart with trailing shock fronts, coloured from schlieren
// photography - the technique actually used to photograph shock waves, where
// compression reads warm and expansion reads cool. Those two hues are the
// editor's accents: amber for anything the user is acting on, cyan reserved for
// live state. Everything else is a graphite ramp, so the accent has somewhere
// quiet to land.
namespace Brand {
// Accents.
constexpr ImVec4 Amber      = ImVec4(1.000f, 0.478f, 0.239f, 1.0f);  // #FF7A3D
constexpr ImVec4 AmberDeep  = ImVec4(0.812f, 0.322f, 0.106f, 1.0f);  // #CF521B
constexpr ImVec4 AmberSoft  = ImVec4(1.000f, 0.478f, 0.239f, 0.28f);
constexpr ImVec4 Cyan       = ImVec4(0.310f, 0.890f, 0.949f, 1.0f);  // #4FE3F2
constexpr ImVec4 CyanDeep   = ImVec4(0.055f, 0.604f, 0.690f, 1.0f);  // #0E9AB0

// Graphite ramp, cool-biased so the amber sits warm against it.
constexpr ImVec4 Bg0        = ImVec4(0.043f, 0.051f, 0.067f, 1.0f);  // deepest
constexpr ImVec4 Bg1        = ImVec4(0.078f, 0.090f, 0.110f, 1.0f);  // window
constexpr ImVec4 Bg2        = ImVec4(0.102f, 0.122f, 0.149f, 1.0f);  // child, frame
constexpr ImVec4 Bg3        = ImVec4(0.145f, 0.169f, 0.204f, 1.0f);  // hover
constexpr ImVec4 Bg4        = ImVec4(0.196f, 0.227f, 0.271f, 1.0f);  // raised
constexpr ImVec4 Line       = ImVec4(0.165f, 0.192f, 0.227f, 1.0f);  // borders
constexpr ImVec4 Text       = ImVec4(0.902f, 0.918f, 0.941f, 1.0f);
constexpr ImVec4 TextDim    = ImVec4(0.482f, 0.529f, 0.588f, 1.0f);

// Semantic, kept separate from the accent so "this needs attention" never
// reads as "this is selected".
constexpr ImVec4 Good       = ImVec4(0.427f, 0.808f, 0.463f, 1.0f);
constexpr ImVec4 Warn       = ImVec4(0.949f, 0.769f, 0.310f, 1.0f);
constexpr ImVec4 Bad        = ImVec4(0.910f, 0.376f, 0.353f, 1.0f);

// Gizmo axes. Desaturated from pure RGB so three saturated buttons in a row do
// not shout over the rest of the inspector.
constexpr ImVec4 AxisX      = ImVec4(0.839f, 0.310f, 0.318f, 1.0f);
constexpr ImVec4 AxisY      = ImVec4(0.451f, 0.729f, 0.361f, 1.0f);
constexpr ImVec4 AxisZ      = ImVec4(0.322f, 0.557f, 0.855f, 1.0f);
} // namespace Brand

class Theme {
public:
    // dpiScale also scales every padding, rounding and border, so the editor
    // keeps its proportions on a high-DPI display instead of shrinking.
    static void ApplyEngineDarkTheme(float dpiScale = 1.0f);

    static void DrawVec3Control(const std::string& label, glm::vec3& values,
                                float resetValue = 0.0f, float columnWidth = 100.0f);

    // A small uppercase, letter-spaced, dimmed label - the section eyebrow used
    // to break a panel into parts without spending a heading on each one.
    static void SectionLabel(const char* text);

    // A collapsing header drawn in the heading face.
    static bool SectionHeader(const char* text, bool defaultOpen = true);
};

} // namespace Supersonic
