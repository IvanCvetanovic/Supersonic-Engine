#pragma once

#include <string>

#include "core/SceneSerializer.hpp"

namespace Supersonic {

class GamePackager {
public:
    // Locates the running executable rather than guessing a build layout, and
    // reports failure instead of printing SUCCESS over an empty folder.
    static SerializationResult PackageStandaloneGame(const std::string& outputFolder);
};

} // namespace Supersonic
