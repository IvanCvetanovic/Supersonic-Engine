#pragma once

#include <filesystem>
#include <string>

namespace Engine {

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    void OnImGuiRender();

private:
    std::filesystem::path m_assetsDirectory;
    std::filesystem::path m_currentDirectory;
};

} // namespace Engine
