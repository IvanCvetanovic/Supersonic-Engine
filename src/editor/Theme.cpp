#include "editor/Theme.hpp"
#include "imgui_internal.h"

namespace Engine {

void Theme::ApplyEngineDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Styling & Rounding Sizes
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 13.0f;

    // Sleek UE5 / JetBrains Dark Theme Palette
    // Window & Backgrounds
    colors[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.13f, 0.13f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame & Controls
    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.28f, 0.34f, 1.00f);

    // Titles & Headers
    colors[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);

    // Headers & Trees
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);

    // Buttons
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.18f, 0.45f, 0.85f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);

    // Docking & Separators
    colors[ImGuiCol_DockingPreview]       = ImVec4(0.20f, 0.45f, 0.85f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.25f, 0.55f, 0.95f, 1.00f);

    // Sliders & Grabs
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.30f, 0.58f, 0.98f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.25f, 0.65f, 1.00f, 1.00f);

    // Text & Selection
    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.20f, 0.45f, 0.85f, 0.35f);
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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("X", buttonSize)) {
        values.x = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##X", &values.x, 0.05f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Y Axis Control (Green)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.85f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
    if (ImGui::Button("Y", buttonSize)) {
        values.y = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Y", &values.y, 0.05f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Z Axis Control (Blue)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
    if (ImGui::Button("Z", buttonSize)) {
        values.z = resetValue;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Z", &values.z, 0.05f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();
    ImGui::Columns(1);

    ImGui::PopID();
}

} // namespace Engine
