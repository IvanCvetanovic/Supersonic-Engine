#include "editor/Theme.hpp"
#include "editor/EditorFonts.hpp"
#include "imgui_internal.h"

namespace Supersonic {

void Theme::ApplyEngineDarkTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ---- Metrics ----
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    // Roomier than ImGui's defaults. A tool UI is read for hours, and the
    // difference between cramped and comfortable is a few pixels per row.
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(9.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 11.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(16.0f, 6.0f);

    // ---- Palette ----
    colors[ImGuiCol_WindowBg]             = Brand::Bg1;
    colors[ImGuiCol_ChildBg]              = Brand::Bg1;
    colors[ImGuiCol_PopupBg]              = ImVec4(Brand::Bg2.x, Brand::Bg2.y, Brand::Bg2.z, 0.98f);
    colors[ImGuiCol_Border]               = Brand::Line;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg]              = Brand::Bg2;
    colors[ImGuiCol_FrameBgHovered]       = Brand::Bg3;
    colors[ImGuiCol_FrameBgActive]        = Brand::Bg4;

    colors[ImGuiCol_TitleBg]              = Brand::Bg0;
    colors[ImGuiCol_TitleBgActive]        = Brand::Bg2;
    colors[ImGuiCol_TitleBgCollapsed]     = Brand::Bg0;
    colors[ImGuiCol_MenuBarBg]            = Brand::Bg0;

    colors[ImGuiCol_Header]               = Brand::Bg3;
    colors[ImGuiCol_HeaderHovered]        = Brand::Bg4;
    colors[ImGuiCol_HeaderActive]         = Brand::AmberSoft;

    colors[ImGuiCol_Button]               = Brand::Bg3;
    colors[ImGuiCol_ButtonHovered]        = Brand::Bg4;
    colors[ImGuiCol_ButtonActive]         = Brand::Amber;

    // The selected tab carries the accent as a thin overline rather than a
    // fill, so a row of tabs does not become a row of orange blocks.
    colors[ImGuiCol_Tab]                  = Brand::Bg1;
    colors[ImGuiCol_TabHovered]           = Brand::Bg3;
    colors[ImGuiCol_TabSelected]          = Brand::Bg2;
    colors[ImGuiCol_TabSelectedOverline]  = Brand::Amber;
    colors[ImGuiCol_TabDimmed]            = Brand::Bg0;
    colors[ImGuiCol_TabDimmedSelected]    = Brand::Bg1;
    colors[ImGuiCol_TabDimmedSelectedOverline] = Brand::Line;

    colors[ImGuiCol_DockingPreview]       = ImVec4(Brand::Amber.x, Brand::Amber.y, Brand::Amber.z, 0.55f);
    colors[ImGuiCol_DockingEmptyBg]       = Brand::Bg0;

    colors[ImGuiCol_Separator]            = Brand::Line;
    colors[ImGuiCol_SeparatorHovered]     = Brand::AmberDeep;
    colors[ImGuiCol_SeparatorActive]      = Brand::Amber;

    colors[ImGuiCol_ResizeGrip]           = ImVec4(Brand::TextDim.x, Brand::TextDim.y, Brand::TextDim.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]    = Brand::AmberDeep;
    colors[ImGuiCol_ResizeGripActive]     = Brand::Amber;

    colors[ImGuiCol_SliderGrab]           = Brand::AmberDeep;
    colors[ImGuiCol_SliderGrabActive]     = Brand::Amber;

    // Cyan is held back for state rather than action: a ticked box reads as
    // information, not as something the user just pressed.
    colors[ImGuiCol_CheckMark]            = Brand::Cyan;

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab]        = Brand::Bg3;
    colors[ImGuiCol_ScrollbarGrabHovered] = Brand::Bg4;
    colors[ImGuiCol_ScrollbarGrabActive]  = Brand::AmberDeep;

    colors[ImGuiCol_Text]                 = Brand::Text;
    colors[ImGuiCol_TextDisabled]         = Brand::TextDim;
    colors[ImGuiCol_TextSelectedBg]       = Brand::AmberSoft;

    colors[ImGuiCol_PlotLines]            = Brand::Cyan;
    colors[ImGuiCol_PlotLinesHovered]     = Brand::Amber;
    colors[ImGuiCol_PlotHistogram]        = Brand::Amber;
    colors[ImGuiCol_PlotHistogramHovered] = Brand::Cyan;

    colors[ImGuiCol_TableHeaderBg]        = Brand::Bg2;
    colors[ImGuiCol_TableBorderStrong]    = Brand::Line;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(Brand::Line.x, Brand::Line.y, Brand::Line.z, 0.5f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_DragDropTarget]       = Brand::Cyan;

    // Every metric above is in logical pixels; this converts them once for the
    // display. Colours are unaffected.
    if (dpiScale > 0.0f && dpiScale != 1.0f) {
        style.ScaleAllSizes(dpiScale);
    }
}

void Theme::SectionLabel(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Brand::TextDim);
    ImGui::PushFont(EditorFonts::Strong(), 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

bool Theme::SectionHeader(const char* text, bool defaultOpen) {
    ImGui::PushFont(EditorFonts::Heading(), 0.0f);
    const bool open = ImGui::CollapsingHeader(
        text, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopFont();
    return open;
}

void Theme::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth) {
    ImGui::PushID(label.c_str());

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, columnWidth);
    ImGui::Text("%s", label.c_str());
    ImGui::NextColumn();

    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

    float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImVec2 buttonSize = ImVec2(lineHeight + 3.0f, lineHeight);

    // X Axis Control (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, Brand::AxisX);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.42f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Brand::AxisX);
    if (ImGui::Button("X", buttonSize)) {
        values.x = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    // v_min and v_max were both 0.0f, which tells ImGui the field is unbounded
    // AND disables clamping of typed input - so ctrl-clicking and entering
    // 1e40 stored an infinity, and a scene saved with one could never be read
    // back. A generous finite bound keeps dragging unrestricted in practice
    // while making a non-finite value impossible to author here.
    constexpr float kLimit = 1.0e9f;
    ImGui::DragFloat("##X", &values.x, 0.05f, -kLimit, kLimit, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Y Axis Control (Green)
    ImGui::PushStyleColor(ImGuiCol_Button, Brand::AxisY);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.56f, 0.84f, 0.47f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Brand::AxisY);
    if (ImGui::Button("Y", buttonSize)) {
        values.y = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Y", &values.y, 0.05f, -kLimit, kLimit, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Z Axis Control (Blue)
    ImGui::PushStyleColor(ImGuiCol_Button, Brand::AxisZ);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.44f, 0.66f, 0.93f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Brand::AxisZ);
    if (ImGui::Button("Z", buttonSize)) {
        values.z = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Z", &values.z, 0.05f, -kLimit, kLimit, "%.2f");
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();
    ImGui::Columns(1);

    ImGui::PopID();
}

} // namespace Supersonic
