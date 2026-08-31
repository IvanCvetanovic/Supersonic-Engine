// Regression tests for sprite flipbooks.
//
// The arithmetic here fails quietly and specifically. A cell index off by one
// shows a sliver of the neighbouring frame, which reads as a badly cut sheet. A
// row counted from the bottom plays the animation in the right ORDER from the
// wrong place, which reads as the artist exporting it upside down. Neither
// looks like a renderer bug and neither is visible in a still.
//
// And one failure that is not visual at all: the frame index is written by a
// tick and read by the next one, so it is simulation state. Left out of the
// state hash, a replay diverges on the frame a sprite is showing and reports
// SUCCESS - the falsely-true shape this engine has already shipped twice, once
// for a recycle counter and once for script state.

#include "TestHarness.hpp"

#include "core/ComponentCodec.hpp"
#include "core/Components.hpp"
#include "core/SpriteAnimationSystem.hpp"
#include "core/StateHash.hpp"

#include <cmath>
#include <sstream>
#include <string>

using namespace Supersonic;

namespace {

SpriteAnimationComponent sheet(uint32_t columns, uint32_t rows, float fps) {
    SpriteAnimationComponent sprite;
    sprite.columns = columns;
    sprite.rows = rows;
    sprite.framesPerSecond = fps;
    return sprite;
}

// One tick at sixty hertz, which is what the simulation clock actually runs at.
constexpr float kTick = 1.0f / 60.0f;

} // namespace

// --- where a cell is -------------------------------------------------------

static void testTheFirstCellIsTheTopLeftCornerScaledToTheGrid() {
    glm::vec2 scale(0.0f), offset(1.0f);
    SpriteAnimationSystem::CellTransform(4, 4, 0, scale, offset);

    CHECK_NEAR(scale.x, 0.25f);
    CHECK_NEAR(scale.y, 0.25f);
    CHECK_NEAR(offset.x, 0.0f);
    CHECK_MSG(offset.y == 0.0f, "cell zero is the origin of the texture, not the middle of it");
}

static void testCellsRunAcrossBeforeTheyRunDown() {
    // The packing order of every sprite tool there is. Getting it transposed
    // plays a 4x4 sheet as four columns rather than four rows, which is a
    // legible animation of the wrong thing.
    glm::vec2 scale(0.0f), offset(0.0f);

    SpriteAnimationSystem::CellTransform(4, 4, 1, scale, offset);
    CHECK_MSG(std::fabs(offset.x - 0.25f) < 1e-5f, "cell 1 is one column across");
    CHECK_MSG(offset.y == 0.0f, "and still on the first row");

    SpriteAnimationSystem::CellTransform(4, 4, 4, scale, offset);
    CHECK_MSG(offset.x == 0.0f, "cell 4 has wrapped back to the first column");
    CHECK_MSG(std::fabs(offset.y - 0.25f) < 1e-5f, "and moved one row DOWN");
}

static void testARowIsCountedFromTheTop() {
    // Texture coordinates start at the top left, and so does every sheet. A
    // sheet counted from the bottom animates correctly and shows the wrong
    // cells, which nobody attributes to the engine.
    glm::vec2 scale(0.0f), offset(0.0f);
    SpriteAnimationSystem::CellTransform(1, 4, 3, scale, offset);

    CHECK_MSG(std::fabs(offset.y - 0.75f) < 1e-5f,
              "the last row of a four-row sheet begins three quarters down");
}

static void testANonSquareGridScalesEachAxisByItsOwnCount() {
    // The case a square sheet cannot catch: one divisor used for both axes
    // passes every 4x4 test and stretches an 8x2 strip.
    glm::vec2 scale(0.0f), offset(0.0f);
    SpriteAnimationSystem::CellTransform(8, 2, 9, scale, offset);

    CHECK_NEAR(scale.x, 0.125f);
    CHECK_NEAR(scale.y, 0.5f);
    CHECK_MSG(std::fabs(offset.x - 0.125f) < 1e-5f, "cell 9 is column 1");
    CHECK_MSG(std::fabs(offset.y - 0.5f) < 1e-5f, "of row 1");
}

static void testACellPastTheEndWrapsRatherThanLeavingTheSheet() {
    // Sampling past the edge hands the look of the animation to whatever clamp
    // mode the texture happens to carry.
    glm::vec2 wrapped(0.0f), first(0.0f), scale(0.0f);
    SpriteAnimationSystem::CellTransform(4, 4, 16, scale, wrapped);
    SpriteAnimationSystem::CellTransform(4, 4, 0, scale, first);

    CHECK_MSG(wrapped == first, "cell sixteen of a sixteen-cell sheet is cell zero");
}

static void testADegenerateGridIsTheWholeTexture() {
    // A division, otherwise. A sheet authored 0x0 is a typo, and the whole
    // texture is what a material that never asked for a transform gets.
    glm::vec2 scale(0.0f), offset(1.0f);
    SpriteAnimationSystem::CellTransform(0, 4, 2, scale, offset);

    CHECK_NEAR(scale.x, 1.0f);
    CHECK_NEAR(scale.y, 1.0f);
    CHECK_MSG(offset == glm::vec2(0.0f), "and it is the identity, not an arbitrary corner");
}

// --- when it advances ------------------------------------------------------

static void testAFrameLastsExactlyAsLongAsTheRateSays() {
    // Ten frames a second at a sixtieth of a second per tick is six ticks a
    // frame. Asserted on both sides of the boundary, because a comparison
    // written the wrong way round is off by exactly one tick and looks fine.
    SpriteAnimationComponent sprite = sheet(4, 4, 10.0f);

    for (int i = 0; i < 5; ++i) SpriteAnimationSystem::Advance(sprite, kTick);
    CHECK_MSG(sprite.frame == 0u, "five ticks is not yet a tenth of a second");

    SpriteAnimationSystem::Advance(sprite, kTick);
    CHECK_MSG(sprite.frame == 1u, "and the sixth one is");
}

static void testOneLongTickCrossesEveryFrameItShould() {
    // The accumulator is a while loop rather than a single decrement, so a tick
    // longer than a frame does not silently drop the ones it passed. A single
    // `if` here would cap a flipbook at one frame per tick, which only shows on
    // a slow sheet nobody tests or a fast rate nobody expects.
    SpriteAnimationComponent sprite = sheet(4, 4, 100.0f);

    SpriteAnimationSystem::Advance(sprite, 0.045f);   // four and a half frames
    CHECK_MSG(sprite.frame == 4u, "four whole frames elapsed in one step");
    CHECK_MSG(sprite.elapsed > 0.0f, "and the remainder is carried, not discarded");
}

static void testALoopingAnimationComesBackToItsFirstFrame() {
    SpriteAnimationComponent sprite = sheet(2, 2, 60.0f);
    sprite.loop = true;

    for (int i = 0; i < 4; ++i) SpriteAnimationSystem::Advance(sprite, kTick);

    CHECK_MSG(sprite.frame == 0u, "four frames of a four-cell loop is back at the start");
    CHECK_MSG(sprite.playing, "and it is still running");
}

static void testAOneShotStopsOnItsLastFrameRatherThanItsFirst() {
    // The bug everybody recognises and nobody can name: an explosion that ends
    // by showing its opening puff again. Stopping is not the same as wrapping.
    SpriteAnimationComponent sprite = sheet(2, 2, 60.0f);
    sprite.loop = false;

    for (int i = 0; i < 20; ++i) SpriteAnimationSystem::Advance(sprite, kTick);

    CHECK_MSG(sprite.frame == 3u, "it holds the last cell of the four");
    CHECK_MSG(!sprite.playing, "and reports that it has finished");
}

static void testAPausedFlipbookDoesNotMove() {
    SpriteAnimationComponent sprite = sheet(4, 4, 60.0f);
    sprite.playing = false;

    for (int i = 0; i < 30; ++i) SpriteAnimationSystem::Advance(sprite, kTick);

    CHECK_MSG(sprite.frame == 0u, "a paused animation stays on its frame");
    CHECK_MSG(sprite.elapsed == 0.0f, "and does not bank the time to spend on resuming");
}

static void testASingleCellAnimationDoesNotSpin() {
    // A 1x1 sheet is a still. Advancing it would burn the accumulator forever
    // rewriting the same cell, and a non-looping one would "finish".
    SpriteAnimationComponent sprite = sheet(1, 1, 60.0f);
    sprite.loop = false;

    for (int i = 0; i < 10; ++i) SpriteAnimationSystem::Advance(sprite, kTick);

    CHECK_MSG(sprite.frame == 0u, "there is nowhere for a one-cell animation to go");
    CHECK_MSG(sprite.playing, "and it has not 'finished' either");
}

static void testAZeroRateHoldsStillRatherThanDividingByIt() {
    SpriteAnimationComponent sprite = sheet(4, 4, 0.0f);
    for (int i = 0; i < 10; ++i) SpriteAnimationSystem::Advance(sprite, kTick);
    CHECK_MSG(sprite.frame == 0u, "no rate is no motion, not an infinite one");
}

static void testFrameCountZeroMeansEveryCellFromTheFirst() {
    SpriteAnimationComponent all = sheet(4, 4, 10.0f);
    CHECK_EQ(all.resolvedFrameCount(), 12u * 0u + 16u);

    SpriteAnimationComponent tail = sheet(4, 4, 10.0f);
    tail.firstFrame = 12;
    CHECK_MSG(tail.resolvedFrameCount() == 4u,
              "a run starting twelve cells in has four left, not sixteen");

    SpriteAnimationComponent named = sheet(4, 4, 10.0f);
    named.firstFrame = 4;
    named.frameCount = 4;
    CHECK_MSG(named.resolvedFrameCount() == 4u, "and an explicit count wins outright");
}

static void testASecondAnimationInTheSameSheetPlaysItsOwnCells() {
    // Several animations packed into one sheet is the ordinary case and the
    // reason firstFrame exists. A run that ignored it would play the walk cycle
    // whichever animation you asked for.
    entt::registry registry;
    const auto entity = registry.create();
    auto& sprite = registry.emplace<SpriteAnimationComponent>(entity);
    sprite.columns = 4;
    sprite.rows = 4;
    sprite.firstFrame = 8;
    sprite.frameCount = 4;
    sprite.framesPerSecond = 60.0f;
    registry.emplace<MaterialComponent>(entity);

    SpriteAnimationSystem::Update(registry, kTick);

    // One tick at sixty is one frame: cell 9, which is row 2 column 1.
    const auto& material = registry.get<MaterialComponent>(entity);
    CHECK_MSG(std::fabs(material.uvOffset.x - 0.25f) < 1e-5f, "column 1");
    CHECK_MSG(std::fabs(material.uvOffset.y - 0.5f) < 1e-5f, "of row 2");
}

// --- what it writes --------------------------------------------------------

static void testTheSystemWritesTheMaterialItAnimates() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& sprite = registry.emplace<SpriteAnimationComponent>(entity);
    sprite.columns = 4;
    sprite.rows = 1;
    sprite.framesPerSecond = 60.0f;
    auto& material = registry.emplace<MaterialComponent>(entity);
    material.uvScale = glm::vec2(1.0f);

    SpriteAnimationSystem::Update(registry, kTick);

    CHECK_NEAR(registry.get<MaterialComponent>(entity).uvScale.x, 0.25f);
    CHECK_MSG(std::fabs(registry.get<MaterialComponent>(entity).uvOffset.x - 0.25f) < 1e-5f,
              "one tick at sixty frames a second is the second cell");
}

static void testAPausedSpriteStillSaysWhichCellItIsShowing() {
    // The cell is where the sprite IS, not something that happens when it
    // moves. Writing it only on advance leaves a scene loaded mid-animation
    // drawing its whole sheet until something happens to step it - and a paused
    // sprite drawing the wrong cell is the same bug held still.
    entt::registry registry;
    const auto entity = registry.create();
    auto& sprite = registry.emplace<SpriteAnimationComponent>(entity);
    sprite.columns = 4;
    sprite.rows = 1;
    sprite.frame = 2;
    sprite.playing = false;
    registry.emplace<MaterialComponent>(entity);

    SpriteAnimationSystem::Update(registry, kTick);

    const auto& material = registry.get<MaterialComponent>(entity);
    CHECK_NEAR(material.uvScale.x, 0.25f);
    CHECK_MSG(std::fabs(material.uvOffset.x - 0.5f) < 1e-5f,
              "a paused sprite on cell two shows cell two");
}

static void testASpriteWithNoMaterialIsSkippedRatherThanCrashing() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<SpriteAnimationComponent>(entity).columns = 4;

    SpriteAnimationSystem::Update(registry, kTick);
    CHECK_MSG(!registry.all_of<MaterialComponent>(entity),
              "the system pairs the two rather than inventing one");
}

// --- and it is in the oracle -----------------------------------------------

static void testTheStateHashSeesWhichFrameASpriteIsOn() {
    // THE ONE THAT MATTERS. The frame index is written by a tick and read by
    // the next, so it is state by the same test that admitted a script's
    // counter and a body's velocity. Left out, a replay whose sprites diverge
    // reports success - and it would keep reporting success until the
    // divergence reached something the hash could see, naming a tick thousands
    // after the one that caused it.
    entt::registry a;
    entt::registry b;
    for (entt::registry* registry : {&a, &b}) {
        const auto entity = registry->create();
        registry->emplace<TransformComponent>(entity);
        auto& sprite = registry->emplace<SpriteAnimationComponent>(entity);
        sprite.columns = 4;
        sprite.rows = 4;
    }

    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b),
              "two registries in the same state agree - the control for the case below");

    for (auto entity : b.view<SpriteAnimationComponent>()) {
        b.get<SpriteAnimationComponent>(entity).frame = 1;
    }

    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b),
              "one sprite one frame apart is a divergence the oracle must report");
}

static void testTheStateHashSeesTheAccumulatorAndTheStopToo() {
    // The two other things a tick writes. The accumulator diverging by one tick
    // is a divergence that has not reached the frame index YET, which is
    // exactly when it is cheap to find.
    entt::registry a;
    entt::registry b;
    for (entt::registry* registry : {&a, &b}) {
        const auto entity = registry->create();
        registry->emplace<TransformComponent>(entity);
        registry->emplace<SpriteAnimationComponent>(entity);
    }

    for (auto entity : b.view<SpriteAnimationComponent>()) {
        b.get<SpriteAnimationComponent>(entity).elapsed = 0.001f;
    }
    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b),
              "a sprite a fraction of a frame ahead has diverged");

    entt::registry c;
    {
        const auto entity = c.create();
        c.emplace<TransformComponent>(entity);
        c.emplace<SpriteAnimationComponent>(entity).playing = false;
    }
    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(c),
              "and so has one whose one-shot has finished when the other's has not");
}

static void testAuthoredFieldsAreNotHashed() {
    // The other half, and it is not a nicety. `columns`, `rows` and the rate
    // are authored: no tick can change them, and hashing them would make
    // retuning an animation in the inspector read as a simulation divergence.
    // The hash covers what a tick WRITES, which is the rule the header states.
    entt::registry a;
    entt::registry b;
    for (entt::registry* registry : {&a, &b}) {
        const auto entity = registry->create();
        registry->emplace<TransformComponent>(entity);
        auto& sprite = registry->emplace<SpriteAnimationComponent>(entity);
        sprite.columns = 4;
        sprite.rows = 4;
    }

    for (auto entity : b.view<SpriteAnimationComponent>()) {
        auto& sprite = b.get<SpriteAnimationComponent>(entity);
        sprite.columns = 8;
        sprite.framesPerSecond = 30.0f;
        sprite.loop = false;
    }

    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b),
              "an authored change is an edit, not a divergence");
}

// --- and it survives being saved -------------------------------------------

static void testTheAnimationSurvivesARoundTrip() {
    entt::registry source;
    const auto entity = source.create();
    source.emplace<TagComponent>(entity, "Flame");
    source.emplace<TransformComponent>(entity);
    auto& sprite = source.emplace<SpriteAnimationComponent>(entity);
    sprite.columns = 8;
    sprite.rows = 2;
    sprite.firstFrame = 3;
    sprite.frameCount = 5;
    sprite.framesPerSecond = 24.0f;
    sprite.loop = false;
    sprite.playing = false;
    sprite.frame = 2;
    sprite.elapsed = 0.01f;

    std::ostringstream out;
    out << "{\n";
    ComponentCodec::Write(source, entity, out, "  ");
    out << "}\n";

    Json::Value node;
    std::string error;
    CHECK_MSG(Json::Parse(out.str(), node, error),
              "the written component must be valid JSON: " + error);

    entt::registry loaded;
    const auto restored = loaded.create();
    ComponentCodec::Read(loaded, restored, node);

    CHECK_MSG(loaded.all_of<SpriteAnimationComponent>(restored), "the component came back");
    const auto& back = loaded.get<SpriteAnimationComponent>(restored);
    CHECK_EQ(back.columns, 8u);
    CHECK_EQ(back.rows, 2u);
    CHECK_EQ(back.firstFrame, 3u);
    CHECK_EQ(back.frameCount, 5u);
    CHECK_NEAR(back.framesPerSecond, 24.0f);
    CHECK_MSG(!back.loop, "loop is false and false is not the default");
    CHECK_MSG(!back.playing, "and so is playing");

    // Where it had got to comes back too, for the reason AnimatorComponent::time
    // does: otherwise stopping and starting play mode snaps every animation in
    // the level back to its opening cell.
    CHECK_EQ(back.frame, 2u);
    CHECK_MSG(std::fabs(back.elapsed - 0.01f) < 1e-5f, "and so does the part-frame");
}

static void testASceneWrittenBeforeThisExistedLoadsAsAStill() {
    // Columns and rows default to ONE, not zero. A key missing from an older
    // scene must never be the difference between a picture and a division.
    Json::Value node;
    std::string error;
    CHECK_MSG(Json::Parse("{ \"SpriteAnimation\": { } }", node, error), error);

    entt::registry registry;
    const auto entity = registry.create();
    ComponentCodec::Read(registry, entity, node);

    const auto& sprite = registry.get<SpriteAnimationComponent>(entity);
    CHECK_EQ(sprite.columns, 1u);
    CHECK_EQ(sprite.rows, 1u);
    CHECK_MSG(sprite.resolvedFrameCount() == 1u, "which is a one-cell still, and drawable");
}

static void runTests() {
    testTheFirstCellIsTheTopLeftCornerScaledToTheGrid();
    testCellsRunAcrossBeforeTheyRunDown();
    testARowIsCountedFromTheTop();
    testANonSquareGridScalesEachAxisByItsOwnCount();
    testACellPastTheEndWrapsRatherThanLeavingTheSheet();
    testADegenerateGridIsTheWholeTexture();

    testAFrameLastsExactlyAsLongAsTheRateSays();
    testOneLongTickCrossesEveryFrameItShould();
    testALoopingAnimationComesBackToItsFirstFrame();
    testAOneShotStopsOnItsLastFrameRatherThanItsFirst();
    testAPausedFlipbookDoesNotMove();
    testASingleCellAnimationDoesNotSpin();
    testAZeroRateHoldsStillRatherThanDividingByIt();
    testFrameCountZeroMeansEveryCellFromTheFirst();
    testASecondAnimationInTheSameSheetPlaysItsOwnCells();

    testTheSystemWritesTheMaterialItAnimates();
    testAPausedSpriteStillSaysWhichCellItIsShowing();
    testASpriteWithNoMaterialIsSkippedRatherThanCrashing();

    testTheStateHashSeesWhichFrameASpriteIsOn();
    testTheStateHashSeesTheAccumulatorAndTheStopToo();
    testAuthoredFieldsAreNotHashed();

    testTheAnimationSurvivesARoundTrip();
    testASceneWrittenBeforeThisExistedLoadsAsAStill();
}

TEST_MAIN("test_sprite", 60)
