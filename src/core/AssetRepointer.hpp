#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/AssetDatabase.hpp"

namespace Supersonic {

class MaterialLibrary;

// Rewrites the asset paths a LOADED scene is holding, after an import worked
// out that a file moved.
//
// Identity has survived a rename since asset identity landed: the sidecar left
// behind by the rename is matched against the new file by content, and a saved
// reference carries a guid alongside its path so the next load resolves it. The
// gap was the scene already open. Nothing re-pointed it, so the editor went on
// naming a file that was not there any more - a checkerboard where a texture
// used to be, until somebody saved and reloaded.
//
// Matching is by PATH, not by guid, and that is deliberate. A component holds
// only a path; the guid lives in the scene FILE and is spent at load time
// resolving it. Import is the one place where both ends of the move are known
// at once, so it hands them over and this rewrites what matches.
struct RepointResult {
    size_t componentFields{0};   // fields on live components
    size_t materialFields{0};    // texture references inside cached .material assets
    size_t environmentFields{0}; // the scene's HDRI

    size_t Total() const { return componentFields + materialFields + environmentFields; }
};

// `materials` may be null, in which case only components are rewritten. It
// normally is not: a cached MaterialAsset holds the three texture paths that
// MaterialSystem::Sync copies onto every component using it, EVERY frame - so
// re-pointing the components alone is undone within one frame by the copy that
// puts the stale paths back.
RepointResult RepointAssets(entt::registry& registry, MaterialLibrary* materials,
                            const std::vector<AssetDatabase::Move>& moves);

} // namespace Supersonic
