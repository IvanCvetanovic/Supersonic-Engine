#include "editor/EditorFonts.hpp"
#include "core/Log.hpp"
#include "editor/EditorIcons.hpp"

#include <algorithm>
#include <iostream>

#include "imgui.h"

namespace Supersonic {

namespace {

// Compressed, base85-encoded TTFs, generated from subset fonts with ImGui's own
// misc/fonts/binary_to_compressed_c.cpp. Subset first: the full faces are around
// 400 KB each, and the editor draws Latin text and thirty-six icons.
#include "editor/fonts/InterRegular.inl"
#include "editor/fonts/InterSemiBold.inl"
#include "editor/fonts/IconsSolid.inl"

ImFont* g_body = nullptr;
ImFont* g_heading = nullptr;
ImFont* g_strong = nullptr;
float g_bodySize = 16.0f;
float g_scale = 1.0f;

// Merges the icon font into whichever font was added last.
void mergeIcons(float size) {
    ImFontConfig config;
    config.MergeMode = true;

    // Icons are drawn from a font with different metrics to the text, so they
    // are forced to a common advance and nudged down onto the text baseline.
    // Without this they sit high and the spacing between an icon and its label
    // varies per glyph.
    config.GlyphMinAdvanceX = size;
    config.GlyphOffset = ImVec2(0.0f, size * 0.06f);

    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(
        IconsSolid_compressed_data_base85, size, &config);
}

} // namespace

void EditorFonts::Load(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();

    // Clamped: a bad monitor scale should degrade to a readable UI, not a
    // one-pixel or a full-screen one.
    g_scale = std::clamp(dpiScale, 0.5f, 4.0f);

    const float bodySize = 16.0f * g_scale;
    const float headingSize = 17.0f * g_scale;
    g_bodySize = bodySize;

    ImFontConfig textConfig;
    // Sub-pixel positioning. At 16px the difference between 1 and 2 is the
    // difference between letters that jitter as a panel is dragged and letters
    // that do not.
    textConfig.OversampleH = 2;
    textConfig.OversampleV = 1;

    g_body = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        InterRegular_compressed_data_base85, bodySize, &textConfig);
    mergeIcons(bodySize);

    g_heading = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        InterSemiBold_compressed_data_base85, headingSize, &textConfig);
    mergeIcons(headingSize);

    g_strong = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        InterSemiBold_compressed_data_base85, bodySize, &textConfig);
    mergeIcons(bodySize);

    // The first font added is the default, but say so rather than relying on it.
    io.FontDefault = g_body;

    if (!g_body || !g_heading || !g_strong) {
        // A failed decode leaves ImGui with no font at all and the first draw
        // call asserts, so say which one rather than letting that happen
        // somewhere less obvious.
        SUPERSONIC_LOG_ERROR("EditorFonts") << "A font failed to decode; falling back to the built-in face."
                  << std::endl;
        io.Fonts->AddFontDefault();
        return;
    }

    SUPERSONIC_LOG_INFO("EditorFonts") << "Inter " << bodySize << "px + Font Awesome merged (DPI scale "
              << g_scale << ")." << std::endl;
}

ImFont* EditorFonts::Body() { return g_body; }
ImFont* EditorFonts::Heading() { return g_heading; }
ImFont* EditorFonts::Strong() { return g_strong; }
float EditorFonts::BodySize() { return g_bodySize; }
float EditorFonts::Scale() { return g_scale; }

} // namespace Supersonic
