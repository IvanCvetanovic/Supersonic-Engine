#include "editor/ContentBrowserPanel.hpp"
#include "editor/EditorIcons.hpp"

#include <algorithm>
#include <cctype>
#include "editor/Theme.hpp"
#include "editor/ThumbnailCache.hpp"
#include "imgui.h"

#include <system_error>

namespace Supersonic {

namespace {

// Icon for a file, by extension. A grid of identical [FILE] tags tells the user
// nothing; a shape per kind is legible at a glance.
const char* iconForFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") return ICON_FA_IMAGE;
    if (ext == ".gltf" || ext == ".glb" || ext == ".obj")                  return ICON_FA_DIAGRAM;
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")                   return ICON_FA_MUSIC;
    if (ext == ".scene" || ext == ".prefab")                               return ICON_FA_LAYER_GROUP;
    if (ext == ".vert" || ext == ".frag" || ext == ".spv")                 return ICON_FA_CODE;
    if (ext == ".svg")                                                     return ICON_FA_IMAGE;
    return ICON_FA_FILE;
}

} // namespace


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
                    ImGui::PushStyleColor(ImGuiCol_Button, Brand::Bg3);
                    if (ImGui::Button((std::string(ICON_FA_FOLDER) + "\n" + filenameString).c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                        m_currentDirectory /= path.filename();
                    }
                    ImGui::PopStyleColor();
                } else {
                    // A real preview where the file can be decoded, the type
                    // glyph otherwise. A grid of identical tiles told the user
                    // nothing about which texture was which.
                    const ImTextureID preview =
                        m_thumbnails ? m_thumbnails->Get(path) : ImTextureID{0};

                    ImGui::PushStyleColor(ImGuiCol_Button, Brand::Bg2);
                    if (preview != 0) {
                        const float inset = 12.0f;
                        ImGui::ImageButton("##thumb", preview,
                                           ImVec2(thumbnailSize - inset, thumbnailSize - inset));
                    } else {
                        ImGui::Button((std::string(iconForFile(path)) + "\n\n" + filenameString).c_str(),
                                      ImVec2(thumbnailSize, thumbnailSize));
                    }
                    ImGui::PopStyleColor();

                    // The image tile carries no label of its own, so it gets a
                    // caption; the glyph tile already has the name inside it.
                    if (preview != 0) {
                        ImGui::PushStyleColor(ImGuiCol_Text, Brand::TextDim);
                        ImGui::TextWrapped("%s", filenameString.c_str());
                        ImGui::PopStyleColor();
                    }
                }
                ec.clear();

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

} // namespace Supersonic
