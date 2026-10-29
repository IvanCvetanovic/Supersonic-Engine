#include "core/SpriteAnimationSystem.hpp"

#include <cmath>

namespace Supersonic {

namespace {

// How many frames one Advance steps through one at a time before it does the rest
// arithmetically. 1024 frames in a sixtieth of a second is 61,440 frames a second,
// which no flipbook shows, so every rate a real animation has takes the loop and
// is bit-identical to what it always was. Past it the loop is not a way of
// stepping but a way of hanging: `FramesPerSecond: 1e30` is 1.6e28 iterations in
// ONE tick, and infinity is a period of zero, which never ends at all.
constexpr int kMaxStepsPerAdvance = 1024;

// What the loop would have reached, in closed form, for the steps it did not take.
// Called with the state the loop had after kMaxStepsPerAdvance real steps, so it
// only ever finishes a pathological rate - and in doubles, which is exact enough
// here (fmod and floor are exactly specified) and as deterministic as the loop.
void skipAhead(SpriteAnimationComponent& sprite, uint32_t count, float secondsPerFrame) {
    // An accumulator that is not a number cannot be stepped from: drop it.
    if (!std::isfinite(sprite.elapsed)) {
        sprite.elapsed = 0.0f;
        return;
    }

    const double period = static_cast<double>(secondsPerFrame);
    const double steps = std::floor(static_cast<double>(sprite.elapsed) / period);

    if (!sprite.loop) {
        // From frame f the loop takes (count - f) steps to stop on the last frame.
        if (steps >= static_cast<double>(count - sprite.frame)) {
            sprite.frame = count - 1;
            sprite.playing = false;
            sprite.elapsed = 0.0f;
            return;
        }
        sprite.frame += static_cast<uint32_t>(steps);
    } else {
        // A cycle is `count` steps: from the last frame one step is the first.
        const double within = std::fmod(steps, static_cast<double>(count));
        sprite.frame = static_cast<uint32_t>(
            std::fmod(static_cast<double>(sprite.frame) + within, static_cast<double>(count)));
    }

    // Whatever is left over, less than one frame. At this size the subtraction
    // cancels to noise, so it is clamped rather than trusted.
    double rest = static_cast<double>(sprite.elapsed) - steps * period;
    if (!(rest >= 0.0) || !(rest < period)) rest = 0.0;
    sprite.elapsed = static_cast<float>(rest);
    if (!(sprite.elapsed < secondsPerFrame)) sprite.elapsed = 0.0f;
}

} // namespace

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

    // 64-bit, because a grid is a number a scene writes: 65536 x 65536 is 2^32
    // cells, which as a uint32 was ZERO, and `cell % 0` is an integer division by
    // zero - a crash. For every grid whose cells fit a uint32 the arithmetic below
    // gives exactly the answers it gave.
    const uint64_t total = static_cast<uint64_t>(columns) * rows;
    const uint64_t wrapped = cell % total;

    const float invColumns = 1.0f / static_cast<float>(columns);
    const float invRows = 1.0f / static_cast<float>(rows);

    const uint32_t column = static_cast<uint32_t>(wrapped % columns);
    const uint32_t row = static_cast<uint32_t>(wrapped / columns);

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

    // A period that is not a positive number - the reciprocal of infinity is zero,
    // and of NaN is NaN - has no frames to cross. Zero in particular would make the
    // loop below `while (elapsed >= 0)`, which never ends.
    if (!(secondsPerFrame > 0.0f)) return;

    int budget = kMaxStepsPerAdvance;
    while (sprite.elapsed >= secondsPerFrame) {
        if (budget-- == 0) {
            skipAhead(sprite, count, secondsPerFrame);
            return;
        }
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
