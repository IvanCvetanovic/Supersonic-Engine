#pragma once

#include <filesystem>
#include <string>

namespace Supersonic {

class ThumbnailCache;
class MaterialLibrary;

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    // Non-owning. Null is fine and simply means every file draws as its icon,
    // which is what happens before the editor has a device.
    void SetThumbnails(ThumbnailCache* thumbnails) { m_thumbnails = thumbnails; }
    void SetMaterialLibrary(MaterialLibrary* library) { m_materials = library; }

    // Set when the user clicks a .material tile, consumed by the editor so it
    // can assign the asset to the current selection. The browser deliberately
    // does not know what is selected.
    std::string ConsumeMaterialClick();

    // Returns a non-empty status message when something went wrong, so the
    // editor can show it rather than throwing out of the render loop.
    std::string OnImGuiRender();

private:
    std::string createMaterial();

    std::filesystem::path m_assetsDirectory;
    std::filesystem::path m_currentDirectory;
    ThumbnailCache* m_thumbnails{nullptr};
    MaterialLibrary* m_materials{nullptr};
    std::string m_clickedMaterial;
};

} // namespace Supersonic
