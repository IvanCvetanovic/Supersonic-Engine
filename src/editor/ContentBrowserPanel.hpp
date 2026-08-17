#pragma once

#include <filesystem>
#include <string>

namespace Supersonic {

class ThumbnailCache;

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    // Non-owning. Null is fine and simply means every file draws as its icon,
    // which is what happens before the editor has a device.
    void SetThumbnails(ThumbnailCache* thumbnails) { m_thumbnails = thumbnails; }

    // Returns a non-empty status message when something went wrong, so the
    // editor can show it rather than throwing out of the render loop.
    std::string OnImGuiRender();

private:
    std::filesystem::path m_assetsDirectory;
    std::filesystem::path m_currentDirectory;
    ThumbnailCache* m_thumbnails{nullptr};
};

} // namespace Supersonic
