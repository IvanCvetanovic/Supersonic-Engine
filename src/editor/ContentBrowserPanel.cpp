#include "editor/ContentBrowserPanel.hpp"
#include "imgui.h"
#include <iostream>

namespace Engine {

ContentBrowserPanel::ContentBrowserPanel()
    : m_assetsDirectory("assets"), m_currentDirectory("assets") {
    if (!std::filesystem::exists(m_assetsDirectory)) {
        std::filesystem::create_directories(m_assetsDirectory);
    }
}

void ContentBrowserPanel::OnImGuiRender() {
    ImGui::Begin("Content Browser");

    if (m_currentDirectory != m_assetsDirectory) {
        if (ImGui::Button("<- Back")) {
            m_currentDirectory = m_currentDirectory.parent_path();
        }
        ImGui::SameLine();
    }

    ImGui::Text("Directory: %s", m_currentDirectory.string().c_str());
    ImGui::Separator();
    ImGui::Spacing();

    float padding = 16.0f;
    float thumbnailSize = 90.0f;
    float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, nullptr, false);

    if (std::filesystem::exists(m_currentDirectory)) {
        for (auto& directoryEntry : std::filesystem::directory_iterator(m_currentDirectory)) {
            const auto& path = directoryEntry.path();
            std::string filenameString = path.filename().string();

            ImGui::PushID(filenameString.c_str());

            if (directoryEntry.is_directory()) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 0.35f));
                if (ImGui::Button(("[DIR]\n" + filenameString).c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                    m_currentDirectory /= path.filename();
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 0.85f));
                ImGui::Button(("[FILE]\n" + filenameString).c_str(), ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor();
            }

            ImGui::TextWrapped("%s", filenameString.c_str());
            ImGui::NextColumn();
            ImGui::PopID();
        }
    }

    ImGui::Columns(1);

    ImGui::End();
}

} // namespace Engine
