#pragma once

#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/AnimationLibrary.hpp"
#include "core/Skeleton.hpp"

namespace Supersonic {

// Skeletal animation: clock, pose evaluation and palette packing.
//
// Deliberately free of Vulkan, which is why the palette gather lives here rather
// than in RenderSystem: RenderSystem calls vkCmd* and cannot be linked by a test
// target, and this is precisely the code - interpolation, joint composition,
// bounds - that most needs one.
class AnimationSystem {
public:
    // Resolves each animated entity's rig from its mesh path, replacing
    // SkinnedMeshComponent. Resolved rather than serialised, so a Play/Stop or
    // an undo cannot lose the rig.
    static void SyncSkeletons(entt::registry& registry, AnimationLibrary& library);

    // Advances the clock only. Separate from EvaluatePoses because the editor
    // must be able to scrub a pose without gameplay running.
    static void Advance(entt::registry& registry, const AnimationLibrary& library, float deltaTime);

    // Samples each animator's clip and composes the joint matrices. Also widens
    // RenderableComponent's bounds to the posed extent, since culling against
    // the bind pose makes a character reaching outside it vanish mid-animation.
    static void EvaluatePoses(entt::registry& registry, const AnimationLibrary& library);

    // Packs every skinned entity's matrices back to back and records where each
    // one starts. Returns the number of matrices written; entities that do not
    // fit get paletteBase = -1 and draw unskinned rather than reading past the
    // end of the buffer.
    static uint32_t GatherPalettes(entt::registry& registry,
                                   std::vector<glm::mat4>& outPalette,
                                   uint32_t capacity);

    // Samples one clip at a time into local joint transforms. Exposed for tests.
    static void SampleClip(const Skeleton& skeleton, const AnimationClip& clip, float time,
                           std::vector<glm::mat4>& outLocals);

    // Composes local transforms into model-space joint matrices, applying the
    // inverse bind. One forward pass, which is only correct because the skeleton
    // stores joints parent-before-child.
    static void ComposePose(const Skeleton& skeleton, const std::vector<glm::mat4>& locals,
                            std::vector<glm::mat4>& outMatrices);
};

} // namespace Supersonic
