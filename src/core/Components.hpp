#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// HierarchyComponent stores an entt::entity, so the handle type is needed here.
#include <entt/entt.hpp>

// GLM_FORCE_RADIANS / GLM_FORCE_DEPTH_ZERO_TO_ONE are set on the target in
// CMakeLists.txt, never here. glm/detail/setup.hpp latches its configuration on
// first inclusion, so a header-local define only applies when this header
// happens to be the first one to pull GLM in - which silently produced two
// different getProjectionMatrix() bodies across translation units.
#include <glm/glm.hpp>

#include "core/UICanvas.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.hpp>

namespace Supersonic {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;

    // xyz = tangent, w = bitangent handedness (+1 or -1).
    //
    // Required for normal mapping: a normal map stores directions in tangent
    // space, and without a tangent basis there is nothing to transform them
    // into world space with. Packing handedness into w is the standard trick
    // that avoids storing a full bitangent.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

    // Skinning influences: up to four joints per vertex.
    //
    // 8-bit indices are exact for any rig inside the 128-joint cap, and float
    // weights sidestep the normalised-integer renormalisation trap entirely. The
    // default is "fully bound to joint 0 with the identity palette", so an
    // unskinned mesh needs no special case in either vertex shader.
    glm::u8vec4 jointIndices{0, 0, 0, 0};
    glm::vec4 jointWeights{1.0f, 0.0f, 0.0f, 0.0f};

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 7> getAttributeDescriptions() {
        std::array<vk::VertexInputAttributeDescription, 7> attributeDescriptions{};

        // Location 0: Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Location 1: Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Location 2: Color
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[2].offset = offsetof(Vertex, color);

        // Location 3: TexCoord
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

        // Location 4: Tangent (xyz) + handedness (w)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        // Location 5: Joint indices. An integer format, so the shader must
        // declare uvec4 - a float declaration against a UINT format is a
        // vertex-input type mismatch.
        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = vk::Format::eR8G8B8A8Uint;
        attributeDescriptions[5].offset = offsetof(Vertex, jointIndices);

        // Location 6: Joint weights
        attributeDescriptions[6].binding = 0;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[6].offset = offsetof(Vertex, jointWeights);

        return attributeDescriptions;
    }
};

// Every attribute offset and the stride must stay 4-byte aligned: this repo
// carries macOS and iOS targets, and Metal rejects an unaligned vertex attribute
// outright. Asserted rather than assumed, because inserting a member anywhere
// above is what would silently break it.
static_assert(offsetof(Vertex, jointIndices) == 60, "vertex layout shifted");
static_assert(offsetof(Vertex, jointWeights) == 64, "vertex layout shifted");
static_assert(sizeof(Vertex) == 80, "vertex stride shifted");
static_assert(offsetof(Vertex, jointIndices) % 4 == 0, "attribute offsets must be 4-byte aligned");
static_assert(sizeof(Vertex) % 4 == 0, "vertex stride must be 4-byte aligned");

// Parent link. Held as a separate component so the common case - an entity with
// no parent - costs nothing, and so a view of "things with parents" is cheap.
struct HierarchyComponent {
    entt::entity parent{entt::null};
};

// Cached world matrix, recomputed each frame by TransformSystem. Rendering,
// picking and the gizmo all read this rather than composing the local transform
// themselves, which is what makes parenting work everywhere at once.
struct WorldTransformComponent {
    glm::mat4 matrix{1.0f};
};

struct TransformComponent {
    // Local to the parent. With no parent this is world space, which is why the
    // engine behaved correctly before hierarchies existed.
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in radians
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 getModelMatrix() const {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), position);
        mat = glm::rotate(mat, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        mat = glm::rotate(mat, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        mat = glm::rotate(mat, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        mat = glm::scale(mat, scale);
        return mat;
    }
};

struct CameraComponent {
    float fov{45.0f};
    float aspect{16.0f / 9.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};

    glm::vec3 position{0.0f, 1.0f, 4.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    float yaw{-90.0f};
    float pitch{0.0f};
    float movementSpeed{3.5f};
    float mouseSensitivity{0.1f};

    // The scene can hold several cameras; this marks the one play mode renders
    // through and the one input drives. CameraSystem used to move every camera
    // entity in lockstep, which broke as soon as a scene had two.
    bool isPrimary{true};

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    glm::mat4 getProjectionMatrix() const {
        glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
        proj[1][1] *= -1.0f; // Flip Y coordinate for Vulkan
        return proj;
    }

    void updateCameraVectors() {
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};

// Named here rather than in the renderer, because it describes what
// LightComponent::type means and core code has to be able to ask.
enum class LightType : int {
    Directional = 0,
    Point = 1,

    // A point light that only shines within a cone. A torch, a street lamp, a
    // stage light, headlights - none of which could be expressed by the two
    // above, because neither can be aimed.
    Spot = 2,
};

struct LightComponent {
    // 0 = directional (uses `direction`), 1 = point (uses the entity transform).
    int type{0};

    // Points TOWARD the light, matching the shader's L vector.
    glm::vec3 direction{0.6f, 1.0f, 0.5f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity{1.2f};

    // Ambient is a scene-wide term; only the first light's value is used.
    //
    // Two colours rather than one: everything outdoors is lit from above by
    // sky and from below by bounce off the ground, and a single flat term
    // makes the underside of every object exactly as blue as its top. The
    // shader interpolates between them by how far the surface faces up.
    glm::vec3 ambient{0.12f, 0.12f, 0.14f};        // sky, above the horizon
    glm::vec3 ambientGround{0.10f, 0.09f, 0.08f};  // bounce, below it

    // Point and spot lights: cutoff distance.
    float range{25.0f};

    // Spot lights only. Full brightness within the inner angle, fading to
    // nothing at the outer one - a single angle would give a hard-edged circle
    // that crawls across the floor, which is the classic look of a spotlight
    // done badly. Measured from the axis, so these are half-angles.
    float innerAngle{0.35f};   // ~20 degrees
    float outerAngle{0.52f};   // ~30 degrees

    bool castsShadow{true};
};

struct MaterialComponent {
    glm::vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness{0.4f};
    float metallic{0.1f};
    float ao{1.0f};
    std::string albedoTexturePath;
    std::string normalTexturePath;

    // When set, the fields above are driven by a shared .material asset and
    // MaterialSystem overwrites them every frame. Empty means the entity owns
    // its own values, which is how every material worked before assets existed.
    //
    // The values are still stored here rather than only in the asset, because
    // the renderer reads this component and should not have to know that assets
    // exist at all.
    std::string materialPath;

    // Rate-limits the "asset is missing" diagnostic to once per path change.
    // Opaque or blended.
    //
    // Alpha was authored everywhere and honoured nowhere: the inspector draws a
    // four-component albedo tint on every material, ParticleEmitterComponent's
    // endColor defaults to alpha zero, and shader.frag dutifully computes
    // albedoTex.a * push.albedoColor.a - which the scene pipeline then
    // discarded, because blendEnable was false and never overridden. The only
    // blended thing in the renderer was the editor grid.
    //
    // What that cost is not polish. It is a construction site that cannot fade
    // in, a build-placement ghost that cannot be tinted, glass, water, foliage,
    // smoke, and every fade-out in any game built on this.
    // Light this surface gives off on its own. Multiplied by emissiveStrength,
    // which is what lets it exceed 1.0 and therefore trip the bloom threshold -
    // a colour picker cannot express "brighter than white", so the intensity is
    // a separate number rather than a fourth channel nobody can drag.
    glm::vec3 emissiveColor{0.0f};
    float emissiveStrength{0.0f};

    bool transparent{false};

    bool warnedMissingAsset{false};
};

struct MeshComponent {
    std::string primitiveType{"Cube"};
    std::string filePath;
    uint32_t vertexCount{0};
    uint32_t indexCount{0};
};

struct RenderableComponent {
    uint32_t meshID{0};
    uint32_t materialID{0};

    // Resolved from MaterialComponent's texture paths by
    // RenderSystem::SyncResources. Default to the built-in white albedo and
    // flat normal, so an untextured material needs no special case.
    uint32_t albedoTextureID{0};
    uint32_t normalTextureID{1};

    bool isVisible{true};
    bool castsShadow{true};

    // Local-space bounds of the resolved mesh, refreshed by
    // RenderSystem::SyncMeshes. Picking uses these so selection matches the
    // geometry actually drawn instead of assuming a unit cube.
    glm::vec3 localBoundsMin{-0.5f};
    glm::vec3 localBoundsMax{0.5f};
};

struct TagComponent {
    std::string tag;
};

// =========================================================================
// NEW SUB-SYSTEM COMPONENTS
// =========================================================================

struct RigidBodyComponent {
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    float mass{1.0f};
    bool isKinematic{false};
    bool useGravity{true};

    // How much of the approach speed comes back as bounce, and how hard it is
    // to slide. These were one pair of constants for the entire world, so a
    // rubber ball and a wooden crate behaved identically - which is not a
    // tuning problem, it is the absence of the setting.
    //
    // 0 is dead, 1 bounces forever. Above 1 gains energy every impact and
    // shakes the scene apart, so the solver clamps.
    float restitution{0.3f};

    // 0 is ice. There is no upper bound in principle - rubber on rubber is
    // above 1 - but the solver clamps to something sane rather than letting a
    // typo stop a body dead.
    float friction{0.4f};

    // Bleeds off speed with no contact involved: air resistance, near enough.
    // Zero is a vacuum, which is what this used to be, and is why a nudged
    // body drifted forever.
    float linearDamping{0.0f};

    // Radians per second about each world axis. Nothing rotated at all before
    // this: a crate dropped on its corner landed flat, a ball never rolled,
    // and a hit off the centre of mass pushed a body without turning it.
    glm::vec3 angularVelocity{0.0f};

    float angularDamping{0.05f};

    // Locks rotation. A character or a camera boom wants to be pushed around
    // without ever tipping over, and the alternative - an enormous inertia -
    // is a number nobody can pick correctly.
    bool freezeRotation{false};
};

// Collision layers.
//
// Without them everything collides with everything, which is not a tuning
// problem but a design ceiling: a bullet cannot ignore the thing that fired it,
// a camera boom cannot pass through the player, and a trigger volume meant for
// the player fires on every crate that touches it.
//
// A bitmask pair rather than a matrix: `layer` is what this collider IS, and
// `collidesWith` is what it will talk to. Both sides must agree before a pair
// is tested, so making something invisible to a category is one edit rather
// than one per counterpart.
namespace CollisionLayer {
inline constexpr uint32_t kDefault = 1u << 0;
inline constexpr uint32_t kAll = 0xFFFFFFFFu;
} // namespace CollisionLayer

struct BoxColliderComponent {
    glm::vec3 size{1.0f, 1.0f, 1.0f};

    // Offset from the entity's origin, in local space.
    //
    // Without this a collider is nailed to the origin of whatever it is
    // attached to, so a character whose mesh has its pivot at the feet cannot
    // have a body around its chest, and a door cannot have its collider on its
    // hinge side. Every workaround involves an extra child entity.
    glm::vec3 center{0.0f};

    bool isTrigger{false};
    uint32_t layer{CollisionLayer::kDefault};
    uint32_t collidesWith{CollisionLayer::kAll};
};

struct SphereColliderComponent {
    float radius{0.5f};
    glm::vec3 center{0.0f};
    bool isTrigger{false};
    uint32_t layer{CollisionLayer::kDefault};
    uint32_t collidesWith{CollisionLayer::kAll};
};

struct AudioSourceComponent {
    std::string soundFile{"assets/audio/ambient.wav"};
    float volume{0.8f};
    float pitch{1.0f};
    bool isPlaying{true};
    bool loop{true};

    // Inverse-distance attenuation bounds.
    float referenceDistance{1.5f};
    float maxDistance{40.0f};

    // Runtime state owned by AudioSystem. 0xFFFFFFFF is AudioEngine::kInvalidVoice;
    // spelled out here so Components.hpp stays free of renderer/audio includes.
    uint32_t voice{0xFFFFFFFFu};
    bool failedToLoad{false};
};

struct AudioListenerComponent {
    bool isPrimary{true};
};

// Playback state for a skinned mesh. Authored, serialised and editable.
struct AnimatorComponent {
    std::string clipName;
    float time{0.0f};
    float speed{1.0f};
    bool loop{true};
    bool playing{true};

    // How long a change of clip takes to cross-fade. Zero snaps, which is what
    // the engine used to do unconditionally: a character switching from idle to
    // run jumped between two unrelated poses in a single frame.
    float blendDuration{0.25f};

    // Rate-limits the "no such clip" diagnostic to once per name change.
    bool warnedMissing{false};

    // Transition state. Derived from a change of clipName and deliberately not
    // serialised: persisting a half-finished cross-fade would restore a pose
    // the scene was never actually in, and a Play/Stop or an undo would land
    // mid-transition between two clips it no longer remembers choosing.
    std::string blendFromClip;
    float blendFromTime{0.0f};
    float blendRemaining{0.0f};
    float blendTotal{0.0f};

    // What clipName was the last time it was looked at, so a change can be
    // noticed wherever it came from - the inspector, a script, or a load.
    std::string activeClip;
};

// Resolved rig for an entity. Entirely derived - AnimationSystem rebuilds it
// from the mesh path every frame - which is why it is not serialised: an undo
// or a Play/Stop must not be able to lose a rig.
struct SkinnedMeshComponent {
    uint32_t skeletonID{0xFFFFFFFFu};

    // World-space-free joint matrices: model space, excluding the entity's own
    // world transform, because the vertex shader already applies that.
    std::vector<glm::mat4> jointMatrices;

    // Offset into this frame's joint palette buffer, or -1 when the entity is
    // not skinned this frame (no rig, or the palette is full).
    int32_t paletteBase{-1};

    // The mesh's own bind-pose bounds, captured once.
    //
    // The pose bounds are the union of the bind box carried by each joint, and
    // unioning that into RenderableComponent's CURRENT bounds would feed the
    // result back into itself and grow without limit. It happens to be safe
    // today only because SyncResources resets those bounds from the static mesh
    // every frame - a coupling that would break silently the moment that refresh
    // was gated on anything. Keeping our own copy removes the dependency.
    glm::vec3 bindBoundsMin{0.0f};
    glm::vec3 bindBoundsMax{0.0f};
    bool bindBoundsCaptured{false};

    bool valid() const { return skeletonID != 0xFFFFFFFFu && !jointMatrices.empty(); }
};

struct ScriptComponent {
    std::string scriptName{"RotatorScript"}; // RotatorScript, OscillatorScript, LightFlickerScript
    bool isEnabled{true};

    // Per-entity script state. A single file-static clock in ScriptEngine meant
    // oscillators could not be rewound or phase-offset from each other.
    float elapsed{0.0f};
    glm::vec3 baseline{0.0f};
    float baseIntensity{0.0f};
    bool baselineCaptured{false};

    // Rate-limits the "no such script" diagnostic to once per name change.
    bool warnedMissing{false};

    // Values authored per entity in the inspector and read by the script.
    //
    // Without these a script is the same script everywhere it is used: two
    // patrolling crates could not have different speeds without two scripts, or
    // one script reading a component the ABI does not carry. Names rather than
    // indices, matching how the ABI already passes input actions - adding a
    // parameter then needs no ABI change and no recompile of the engine.
    //
    // Serialised, because they are authored data.
    std::vector<std::pair<std::string, float>> parameters;

    // Scratch the script owns between frames.
    //
    // Engine-side on purpose: the plugin is unloaded and reloaded while the
    // process runs, so anything the plugin allocated would dangle. Living here
    // means a script keeps its counters across a reload, which is most of what
    // makes hot reload feel like editing a running game rather than restarting
    // one.
    //
    // NOT serialised: this is where a script is, not what it was authored as.
    // Writing it into the scene would make a save depend on how long the game
    // had been running when it was taken.
    std::vector<std::pair<std::string, float>> state;
};

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f};
    float lifetime{0.0f};
    float maxLifetime{1.0f};
    bool active{false};
};

struct ParticleEmitterComponent {
    uint32_t maxParticles{100};
    float emitRate{10.0f};
    float particleLifetime{2.0f};
    glm::vec4 startColor{1.0f, 0.6f, 0.1f, 1.0f};
    glm::vec4 endColor{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocityRange{0.5f, 2.0f, 0.5f};
    float particleSize{0.08f};

    // Each emitter owns its particles. They used to share one 200-entry static
    // pool, so two emitters silently halved each other's throughput and
    // maxParticles/emitRate were never read at all.
    std::vector<Particle> particles;
    float emitAccumulator{0.0f};
};

// ---------------------------------------------------------------------------
// In-game UI.
//
// ImGui is the editor's UI and only the editor's: a running game had no way to
// draw a score, a health bar, a menu or a single line of text. These components
// are that - authored in the inspector, saved with the scene, carried by a
// prefab, and drawn over the game whether it is running in the viewport or as a
// packaged executable.
//
// Screen-space, so they carry no TransformComponent: an anchor and an offset
// place them, because a HUD has to survive every window size rather than the
// one it was authored at.
// ---------------------------------------------------------------------------

struct UITextComponent {
    std::string text{"Score: 0"};

    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{24.0f, 24.0f};

    // In authored units at the reference height, like every other UI size, so
    // text keeps its proportion of the screen rather than shrinking to nothing
    // on a large display.
    float fontSize{32.0f};

    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    // A drop shadow behind the glyphs. White text over a bright sky is
    // unreadable without one, and every HUD ends up wanting it.
    bool shadow{true};

    bool visible{true};
};

// A rectangle: a backdrop, a bar, a crosshair, a menu panel.
struct UIPanelComponent {
    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{24.0f, 24.0f};
    glm::vec2 size{320.0f, 32.0f};

    glm::vec4 color{0.0f, 0.0f, 0.0f, 0.55f};
    float cornerRadius{6.0f};

    // Fraction of the width actually drawn, which is what turns a rectangle
    // into a health or progress bar. 1 is a plain panel.
    float fill{1.0f};

    // Drawn behind the fill at full width, so a bar reads as "empty" rather
    // than as nothing at all when it runs low.
    bool drawTrack{false};
    glm::vec4 trackColor{0.0f, 0.0f, 0.0f, 0.45f};

    bool visible{true};
};

// A button: a panel that knows it was touched.
//
// Deliberately one component rather than a panel plus a label plus a collider.
// A button is a single thing to the person authoring a menu, and splitting it
// into three entities that have to be kept aligned by hand is how menus end up
// with click targets that do not match what is drawn.
struct UIButtonComponent {
    std::string label{"Play"};

    UIAnchor anchor{UIAnchor::Center};
    glm::vec2 offset{0.0f, 0.0f};
    glm::vec2 size{280.0f, 64.0f};

    float fontSize{28.0f};
    float cornerRadius{10.0f};

    glm::vec4 color{0.16f, 0.17f, 0.21f, 0.96f};
    glm::vec4 hoverColor{0.24f, 0.26f, 0.32f, 0.98f};
    glm::vec4 pressColor{0.10f, 0.11f, 0.14f, 1.0f};
    glm::vec4 disabledColor{0.14f, 0.14f, 0.16f, 0.55f};
    glm::vec4 textColor{0.94f, 0.95f, 0.97f, 1.0f};

    bool visible{true};

    // A disabled button still draws - greyed - because a menu item that
    // vanishes when unavailable moves everything below it.
    bool enabled{true};

    // Rebuilt from the pointer every frame and deliberately NOT serialised: a
    // button saved mid-press would come back stuck, and a click restored from
    // a snapshot would fire an action nobody asked for.
    bool hovered{false};
    bool pressed{false};
    bool clicked{false};
};

} // namespace Supersonic
