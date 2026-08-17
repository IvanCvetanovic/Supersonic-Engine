#pragma once

#include <filesystem>

namespace Supersonic {

// Absolute path of the running binary, or empty when the platform has no cheap
// way to ask.
//
// There were two copies of this before, in SupersonicApp and GamePackager, and
// they had already diverged: the packager handled macOS and paths longer than
// MAX_PATH, the app handled neither. A third copy was about to be written for
// the packaged-game check, which is one more than enough.
//
// Everything that has to work both from a build tree and from a packaged folder
// hangs off this: the script plugin sits next to the executable, and so does
// the manifest that says this is a game rather than the editor.
std::filesystem::path ExecutablePath();

// Directory containing the running binary. Empty if the path is unknown.
std::filesystem::path ExecutableDirectory();

} // namespace Supersonic
