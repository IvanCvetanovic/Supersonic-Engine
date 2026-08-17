#pragma once

#include <filesystem>
#include <string>

namespace Engine {

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    // Returns a non-empty status message when something went wrong, so the
    // editor can show it rather than throwing out of the render loop.
    std::string OnImGuiRender();

private:
    std::filesystem::path m_assetsDirectory;
    std::filesystem::path m_currentDirectory;
};

} // namespace Engine
