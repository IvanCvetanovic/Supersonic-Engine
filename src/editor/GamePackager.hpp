#pragma once

#include <string>

#include "core/SceneSerializer.hpp"

namespace Supersonic {

class GamePackager {
public:
    // Locates the running executable rather than guessing a build layout, and
    // reports failure instead of printing SUCCESS over an empty folder.
    // startupScene is the scene the packaged game boots. It used to be a
    // literal in the .cpp, so "package the current scene" shipped whatever was
    // last written to MainScene.scene no matter which scene was open - and said
    // it had succeeded.
    static SerializationResult PackageStandaloneGame(const std::string& outputFolder,
                                                     const std::string& startupScene);
};

} // namespace Supersonic
