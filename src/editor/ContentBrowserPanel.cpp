#include "editor/ContentBrowserPanel.hpp"
#include "imgui.h"

#include <system_error>

namespace Engine {

ContentBrowserPanel::ContentBrowserPanel()
    : m_assetsDirectory("assets"), m_currentDirectory("assets") {
    // Best effort only. Creating this relative to the process working directory
    // is why launching from elsewhere used to litter that directory.
    std::error_code ec;
    std::filesystem::create_directories(m_assetsDirectory, ec);
}

std::string ContentBrowserPanel::OnImGuiRender() {
    std::string status;

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

    const float padding = 16.0f;
    const float thumbnailSize = 90.0f;
    const float cellSize = thumbnailSize + padding;

    const float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, nullptr, false);

    // The non-throwing directory_iterator overloads matter here: a removable
    // drive or network share can vanish between the exists() check and the
    // iteration, and an escaping filesystem_error would unwind through an open
    // ImGui window stack and a recording command buffer.
    std::error_code ec;
    if (std::filesystem::exists(m_currentDirectory, ec)) {
        std::filesystem::directory_iterator it(m_currentDirectory, ec);
        if (ec) {
            status = "Cannot read " + m_currentDirectory.string() + ": " + ec.message();
            m_currentDirectory = m_assetsDirectory;
        } else {
            for (const auto& directoryEntry : it) {
                const auto& path = directoryEntry.path();
                const std::string filenameString = path.filename().string();

                ImGui::PushID(filenameString.c_str());

                if (directoryEntry.is_directory(ec) && !ec) {
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
                ec.clear();

                ImGui::TextWrapped("%s", filenameString.c_str());
                ImGui::NextColumn();
                ImGui::PopID();
            }
        }
    } else {
        ImGui::TextDisabled("Directory not found.");
    }

    ImGui::Columns(1);
    ImGui::End();

    return status;
}

} // namespace Engine
