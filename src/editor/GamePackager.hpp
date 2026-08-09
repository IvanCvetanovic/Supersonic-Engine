#pragma once

#include <string>

namespace Engine {

class GamePackager {
public:
    static bool PackageStandaloneGame(const std::string& outputFolder);
};

} // namespace Engine
