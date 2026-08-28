// Regression tests for the fly camera that runs in Play.
//
// What made these possible: CameraSystem used to poll glfwGetKey directly, so
// it could not be called at all without a real window - and it was therefore
// the one system with no coverage, while being the one that reaches into a
// shipped game. A packaged game goes straight into Play and this system runs in
// Play, so the editor's fly controls were bolted to exactly the keys the
// default bindings give a player: MoveX is D/A, MoveY is W/S, Jump is Space,
// Sprint is Left Shift. Pressing W walked the character forward and flew the
// view through the wall behind it, and nothing could turn the second one off.
//
// Everything here drives Input's snapshot, which is the same thing the polling
// layer fills from glfwGetKey earlier in the frame.

#include "TestHarness.hpp"

#include "core/CameraSystem.hpp"
#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/SceneSerializer.hpp"

#include <cmath>
#include <string>

using namespace Supersonic;

namespace {

void frame(const RawInputState& state) { Input::Update(state); }

// Bindings AND devices AND the look latch.
//
// s_looking is a file-static that outlives a registry, so a case that leaves a
// right-drag in progress would hand it to the next one. Two released frames put
// it back through the only door there is.
void reset() {
    Input::ClearBindings();
    Input::SetCursorMode(CursorMode::Normal);
    Input::SuppressCursorCapture(false);
    Input::SetWindowFocused(true);
    Input::SetTextCaptureActive(false);

    frame(RawInputState{});
    frame(RawInputState{});

    // Two updates with no button held, which is what ends a latched look.
    entt::registry scratch;
    CameraSystem::Update(scratch, 0.0f);
    CameraSystem::Update(scratch, 0.0f);
}

RawInputState withKey(int key) {
    RawInputState state{};
    state.keys[key] = true;
    return state;
}

entt::entity addCamera(entt::registry& registry, bool flyControls = true) {
    const auto entity = registry.create();
    auto& camera = registry.emplace<CameraComponent>(entity);
    camera.isPrimary = true;
    camera.flyControlsEnabled = flyControls;
    camera.position = glm::vec3(0.0f);
    camera.updateCameraVectors();
    return entity;
}

// A mouse that has moved, which takes two frames: Input rebases the delta on
// the frame the cursor mode changes, so a one-frame test reads a delta of zero
// and passes for the wrong reason.
void moveMouseTo(const glm::vec2& first, const glm::vec2& second) {
    RawInputState a{};
    a.mousePosition = first;
    frame(a);

    RawInputState b{};
    b.mousePosition = second;
    frame(b);
}

} // namespace

static void testWasdFliesTheCameraByDefault() {
    // The behaviour every existing scene has and must keep. Also the control
    // for the case below - without it, a test that asserts a disabled camera
    // does not move would pass with the flag unimplemented, because a camera
    // nobody pushed does not move either.
    reset();
    entt::registry registry;
    const auto entity = addCamera(registry);

    frame(withKey(Key::W));
    CameraSystem::Update(registry, 1.0f);

    const auto& camera = registry.get<CameraComponent>(entity);
    CHECK_MSG(std::fabs(camera.position.z) > 0.1f,
              "W must fly the camera forward when fly controls are on");
}

static void testTheSameKeyLeavesADisabledCameraExactlyWhereItWas() {
    // THE HEADLINE CLAIM, and it is written as a pair on purpose: the same
    // injected keystroke, the same one call, and the only difference between
    // the two cameras is the field. Asserted as bit equality rather than a
    // tolerance, because "did not move" is not a question about how far.
    reset();
    entt::registry registry;
    const auto flying = addCamera(registry, true);
    const glm::vec3 before = registry.get<CameraComponent>(flying).position;

    frame(withKey(Key::W));
    CameraSystem::Update(registry, 1.0f);
    const glm::vec3 moved = registry.get<CameraComponent>(flying).position;
    CHECK_MSG(moved != before, "the control camera moved");

    reset();
    entt::registry still;
    const auto welded = addCamera(still, false);
    const glm::vec3 start = still.get<CameraComponent>(welded).position;

    frame(withKey(Key::W));
    CameraSystem::Update(still, 1.0f);

    CHECK_MSG(still.get<CameraComponent>(welded).position == start,
              "the same key must not move a camera whose fly controls are off");
}

static void testACameraWithFlyControlsOffIsNotTurnedByTheMouse() {
    // The half a movement-only gate would silently pass. A game that has turned
    // the flycam off and can still have its view spun by a right-drag has not
    // turned it off.
    reset();
    Input::SetCursorMode(CursorMode::Locked);   // a locked pointer IS the look

    entt::registry registry;
    const auto entity = addCamera(registry, false);
    const float yaw = registry.get<CameraComponent>(entity).yaw;
    const float pitch = registry.get<CameraComponent>(entity).pitch;

    moveMouseTo(glm::vec2(100.0f, 100.0f), glm::vec2(400.0f, 300.0f));
    CameraSystem::Update(registry, 1.0f);

    const auto& camera = registry.get<CameraComponent>(entity);
    CHECK_MSG(camera.yaw == yaw, "a disabled camera does not yaw");
    CHECK_MSG(camera.pitch == pitch, "nor pitch");

    Input::SetCursorMode(CursorMode::Normal);
}

static void testAMouseThatTurnsAnEnabledCameraIsTheSameMouse() {
    // The control for the case above, for the same reason the keyboard has one.
    reset();
    Input::SetCursorMode(CursorMode::Locked);

    entt::registry registry;
    const auto entity = addCamera(registry, true);
    const float yaw = registry.get<CameraComponent>(entity).yaw;

    moveMouseTo(glm::vec2(100.0f, 100.0f), glm::vec2(400.0f, 300.0f));
    CameraSystem::Update(registry, 1.0f);

    CHECK_MSG(registry.get<CameraComponent>(entity).yaw != yaw,
              "the identical mouse movement turns a camera that allows it");

    Input::SetCursorMode(CursorMode::Normal);
}

static void testAFocusedTextFieldStillStopsTheFlyCamera() {
    // The veto the caller passes, which the switch from GLFW to Input must not
    // be taken to have made redundant. Input's RAW key queries are deliberately
    // ungated - only actions and axes fall silent while a name is being typed -
    // so this camera still has to be told.
    reset();
    entt::registry registry;
    const auto entity = addCamera(registry);
    const glm::vec3 start = registry.get<CameraComponent>(entity).position;

    frame(withKey(Key::W));
    CameraSystem::Update(registry, 1.0f, /*allowKeyboard=*/false);

    CHECK_MSG(registry.get<CameraComponent>(entity).position == start,
              "a camera whose keyboard is vetoed does not move");
}

static void testOnlyThePrimaryCameraFlies() {
    // Driving every CameraComponent in lockstep broke as soon as a scene had
    // two, and the gate must not have reintroduced a second entry point that
    // forgets it.
    reset();
    entt::registry registry;

    const auto primary = addCamera(registry, true);
    const auto secondary = registry.create();
    auto& other = registry.emplace<CameraComponent>(secondary);
    other.isPrimary = false;
    other.flyControlsEnabled = true;
    other.position = glm::vec3(0.0f);
    other.updateCameraVectors();
    const glm::vec3 secondaryStart = other.position;

    frame(withKey(Key::W));
    CameraSystem::Update(registry, 1.0f);

    CHECK_MSG(registry.get<CameraComponent>(primary).position != glm::vec3(0.0f),
              "the primary camera flew");
    CHECK_MSG(registry.get<CameraComponent>(secondary).position == secondaryStart,
              "and the other one stayed where it was");
}

static void testASceneWrittenBeforeTheFlagExistedStillFlies() {
    // The compatibility claim, and the one a careless AsBool(false) breaks: it
    // would turn every camera in every existing scene to stone with no error
    // message anywhere. Built by serialising a real camera and deleting the
    // line, rather than by typing JSON from memory - a hand-guessed shape that
    // failed to parse would make this pass through the fallback path without
    // testing anything.
    entt::registry authored;
    addCamera(authored, true);
    std::string text = SceneSerializer::SerializeToString(authored);

    const std::string key = "\"FlyControls\"";
    const size_t at = text.find(key);
    CHECK_MSG(at != std::string::npos, "the field is written at all");
    if (at == std::string::npos) return;

    const size_t lineStart = text.rfind('\n', at) + 1;
    const size_t lineEnd = text.find('\n', at);
    text.erase(lineStart, lineEnd - lineStart + 1);
    CHECK_MSG(text.find(key) == std::string::npos, "and the line is gone from the copy");

    entt::registry loaded;
    const SerializationResult result = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(result.ok, "a scene without the field must still parse: " + result.message);

    bool sawCamera = false;
    for (auto [entity, camera] : loaded.view<CameraComponent>().each()) {
        sawCamera = true;
        CHECK_MSG(camera.flyControlsEnabled,
                  "a scene authored before the flag existed keeps its fly camera");
    }
    CHECK_MSG(sawCamera, "the scene really did carry a camera");
}

static void testTheFlagSurvivesBeingSavedAndLoaded() {
    // The other direction: a game turns it off, and the answer has to be in the
    // file. This is what makes the inspector checkbox the shipped switch rather
    // than an editor session's opinion.
    entt::registry authored;
    addCamera(authored, false);

    entt::registry loaded;
    const SerializationResult result = SceneSerializer::DeserializeFromString(
        loaded, SceneSerializer::SerializeToString(authored));
    CHECK_MSG(result.ok, result.message);

    for (auto [entity, camera] : loaded.view<CameraComponent>().each()) {
        CHECK_MSG(!camera.flyControlsEnabled,
                  "flyControlsEnabled must come back false, not default back to true");
    }
}

static void runTests() {
    testWasdFliesTheCameraByDefault();
    testTheSameKeyLeavesADisabledCameraExactlyWhereItWas();
    testACameraWithFlyControlsOffIsNotTurnedByTheMouse();
    testAMouseThatTurnsAnEnabledCameraIsTheSameMouse();
    testAFocusedTextFieldStillStopsTheFlyCamera();
    testOnlyThePrimaryCameraFlies();
    testASceneWrittenBeforeTheFlagExistedStillFlies();
    testTheFlagSurvivesBeingSavedAndLoaded();
}

TEST_MAIN("test_camera", 14)
