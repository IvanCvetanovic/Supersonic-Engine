#include "core/SpriteAnimationSystem.hpp"

namespace Supersonic {

void SpriteAnimationSystem::CellTransform(uint32_t columns, uint32_t rows, uint32_t cell,
                                          glm::vec2& outScale, glm::vec2& outOffset) {
    // A degenerate grid is the whole texture, which is also what a material
    // that never asked for a transform gets. A zero here would otherwise be a
    // division, and a sheet authored as 0x0 is a typo rather than a request for
    // an empty sprite.
    if (columns == 0 || rows == 0) {
        outScale = glm::vec2(1.0f, 1.0f);
        outOffset = glm::vec2(0.0f, 0.0f);
        return;
    }

    const uint32_t total = columns * rows;
    const uint32_t wrapped = cell % total;

    const float invColumns = 1.0f / static_cast<float>(columns);
    const float invRows = 1.0f / static_cast<float>(rows);

    const uint32_t column = wrapped % columns;
    const uint32_t row = wrapped / columns;

    outScale = glm::vec2(invColumns, invRows);

    // Row zero is the TOP one, because texture coordinates start at the top
    // left corner and every tool that packs a sheet numbers its rows the same
    // way. Counting from the bottom would play a four-row sheet in the right
    // order and the wrong place, which looks like the artist exported it wrong.
    outOffset = glm::vec2(static_cast<float>(column) * invColumns,
                          static_cast<float>(row) * invRows);
}

void SpriteAnimationSystem::Advance(SpriteAnimationComponent& sprite, float fixedDelta) {
    if (!sprite.playing) return;
    if (fixedDelta <= 0.0f) return;
    if (sprite.framesPerSecond <= 0.0f) return;

    const uint32_t count = sprite.resolvedFrameCount();

    // Nothing to play. Not an error - a sheet whose first frame is past its own
    // grid is a mis-authored asset, and holding still is what it should look
    // like rather than a division below.
    if (count == 0) return;

    // A single-frame animation is a still. Advancing it would spin the
    // accumulator forever writing the same cell.
    if (count == 1) {
        sprite.frame = 0;
        return;
    }

    const float secondsPerFrame = 1.0f / sprite.framesPerSecond;
    sprite.elapsed += fixedDelta;

    while (sprite.elapsed >= secondsPerFrame) {
        sprite.elapsed -= secondsPerFrame;

        if (sprite.frame + 1 < count) {
            ++sprite.frame;
            continue;
        }

        if (sprite.loop) {
            sprite.frame = 0;
            continue;
        }

        // STOPS ON THE LAST FRAME, not past it and not back at the first. An
        // explosion that ends by showing its opening puff again is a bug
        // everyone recognises and nobody can name.
        sprite.playing = false;
        sprite.elapsed = 0.0f;
        return;
    }
}

void SpriteAnimationSystem::Update(entt::registry& registry, float fixedDelta) {
    // The whole registry, not the pair Apply walks: a sprite whose material has
    // not been given to it yet is still a clock, and stopping it because
    // nothing is drawing it would make the animation jump the moment one was
    // attached.
    for (auto entity : registry.view<SpriteAnimationComponent>()) {
        Advance(registry.get<SpriteAnimationComponent>(entity), fixedDelta);
    }
}

void SpriteAnimationSystem::Apply(entt::registry& registry) {
    auto view = registry.view<SpriteAnimationComponent, MaterialComponent>();
    for (auto entity : view) {
        const auto& sprite = view.get<SpriteAnimationComponent>(entity);
        auto& material = view.get<MaterialComponent>(entity);

        // WRITTEN EVEN WHEN PAUSED, and even when the animation has finished.
        // The cell is where the sprite IS, not something that happens when it
        // moves - so a paused flipbook keeps showing its frame, and a scene
        // loaded with a sprite mid-animation shows that frame on the frame it
        // loads rather than on the first tick that happens to advance it.
        const uint32_t count = sprite.resolvedFrameCount();
        if (count == 0) continue;

        glm::vec2 scale(1.0f);
        glm::vec2 offset(0.0f);
        CellTransform(sprite.columns, sprite.rows,
                      sprite.firstFrame + (sprite.frame % count), scale, offset);

        material.uvScale = scale;
        material.uvOffset = offset;
    }
}

} // namespace Supersonic
