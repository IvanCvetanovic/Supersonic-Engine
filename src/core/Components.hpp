#pragma once

#include <algorithm>
#include <cmath>

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

    // Which call to UpdateWorldTransforms last composed this matrix.
    //
    // Purely a cache marker, and the reason the resolve is memoised rather
    // than merely iterative: a chain walk stops the moment it reaches an
    // ancestor carrying the current stamp, instead of re-deriving that
    // ancestor's matrix for every one of its descendants. Zero is what a fresh
    // component holds and is never a live stamp.
    uint32_t resolvedStamp{0};
};

struct TransformComponent {
    // Local to the parent. With no parent this is world space, which is why the
    // engine behaved correctly before hierarchies existed.
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in radians
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    // T * Rx * Ry * Rz * S, written out.
    //
    // The hottest function in the engine: every entity, twice a frame, plus
    // every physics step, every query and every gather. It used to be five
    // chained 4x4 multiplies - a translate, three rotates and a scale - which
    // is about three hundred and twenty multiply-adds to produce a matrix whose
    // twelve meaningful entries take about thirty.
    //
    // The composition is unchanged and so is the Euler order. Rx*Ry*Rz
    // multiplied out gives the rotation below; the scale multiplies each
    // COLUMN, because it is applied on the right; the translation is the
    // fourth column untouched by either. tests/test_transform.cpp checks every
    // entry against the chained form it replaces, over a spread of angles,
    // which is the only sane way to trust an expansion like this - the
    // derivation is not the kind of thing to check by reading.
    glm::mat4 getModelMatrix() const {
        const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
        const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
        const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);

        glm::mat4 mat(1.0f);

        // Column 0 - the rotated x axis, scaled.
        mat[0][0] = (cy * cz) * scale.x;
        mat[0][1] = (cx * sz + sx * sy * cz) * scale.x;
        mat[0][2] = (sx * sz - cx * sy * cz) * scale.x;
        mat[0][3] = 0.0f;

        // Column 1 - the rotated y axis.
        mat[1][0] = (-cy * sz) * scale.y;
        mat[1][1] = (cx * cz - sx * sy * sz) * scale.y;
        mat[1][2] = (sx * cz + cx * sy * sz) * scale.y;
        mat[1][3] = 0.0f;

        // Column 2 - the rotated z axis.
        mat[2][0] = (sy) * scale.z;
        mat[2][1] = (-sx * cy) * scale.z;
        mat[2][2] = (cx * cy) * scale.z;
        mat[2][3] = 0.0f;

        mat[3][0] = position.x;
        mat[3][1] = position.y;
        mat[3][2] = position.z;
        mat[3][3] = 1.0f;

        return mat;
    }

    // The rotation alone, with the scale divided out.
    //
    // Built by asking getModelMatrix for an unscaled copy rather than by
    // writing the nine entries again. They would start identical and drift the
    // first time either was touched, and the symptom - a body that renders one
    // way and spins another - is the exact bug this pair of functions exists to
    // fix.
    glm::mat3 getRotationMatrix() const {
        TransformComponent unscaled = *this;
        unscaled.scale = glm::vec3(1.0f);
        return glm::mat3(unscaled.getModelMatrix());
    }

    // The Euler triple that getModelMatrix would turn back into `rotation`.
    //
    // This exists because glm::quat(vec3) is NOT the same convention: it
    // composes the three angles in the opposite order, so for any orientation
    // with more than one non-zero angle it is a DIFFERENT rotation. Measured at
    // 0.33 on a matrix entry for (0.5, 0.7, 0.3), which is not a rounding
    // difference - it is a different orientation entirely.
    //
    // The physics integrator used it to turn `rotation` into a quaternion,
    // apply the step's spin, and write the result back. Both halves used the
    // same wrong convention, so the round trip was self-consistent and every
    // test passed - but the spin was applied about the wrong axes relative to
    // the matrix that renders and collides the body. Nothing caught it because
    // a body turning about ONE axis has one non-zero angle, and the two
    // conventions agree exactly there; a hinged door swinging past ninety
    // degrees picks up a second, and the error compounds until the body
    // explodes.
    //
    // Inverted from the entries getModelMatrix writes: sy is [2][0], and the
    // other two come out of ratios that cancel cy.
    static glm::vec3 EulerFromRotation(const glm::mat3& rotation) {
        const float sy = std::clamp(rotation[2][0], -1.0f, 1.0f);
        const float y = std::asin(sy);

        // Gimbal lock: cy is zero, so x and z stop being separable - every
        // (x, z) with the same sum describes the same orientation. Pinning z
        // at zero and putting the whole turn into x is the standard choice and
        // the only one that is continuous as the pole is approached.
        if (std::fabs(sy) > 0.99999f) {
            const float xz = std::atan2(rotation[0][1], rotation[1][1]);
            return glm::vec3(sy > 0.0f ? xz : -xz, y, 0.0f);
        }

        return glm::vec3(std::atan2(-rotation[2][1], rotation[2][2]),
                         y,
                         std::atan2(-rotation[1][0], rotation[0][0]));
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

    // Occlusion, roughness and metallic, packed into one image the way glTF
    // packs them: R is ambient occlusion, G roughness, B metallic.
    //
    // One map rather than three because that is what an exporter writes and
    // what a material author paints - and because three bindings would be three
    // samplers and three descriptors for data that is one byte each. The
    // channels MULTIPLY the constants above rather than replacing them, so a
    // material can be authored rough overall and worn smooth in places, and a
    // material with no map here reads exactly as it did before it existed.
    //
    // Sampled as data, never as colour: these are numbers, and an sRGB decode
    // would bend every one of them.
    std::string ormTexturePath;

    // How much of the map's RED channel is believed, 0 to 1.
    //
    // Not decoration: glTF says of a metallic-roughness texture that "the red
    // and alpha channels are not specified and their values are ignored", and
    // exporters do write zero there. Read as occlusion, that zeroes the ambient
    // term for the whole surface - a valid file rendering pitch black wherever
    // no light directly reaches it.
    //
    // So the importer says whether the red channel came from an occlusion
    // texture or is just whatever was left in the image, and this is where it
    // says it. glTF's own formula, 1 + strength * (sampled - 1), makes zero
    // mean "ignore it" as the SAME arithmetic rather than a branch, the way a
    // fog density or an alpha cutoff of zero already does here.
    float occlusionStrength{1.0f};

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

    // Discard any fragment whose alpha falls below this, and zero means do not.
    //
    // The other half of transparency, and the half most world content actually
    // wants: a leaf card, a chain-link fence, a grate is mostly holes with hard
    // edges, not a pane of glass. Without it those had to be marked
    // `transparent` and pushed through the blended pass, where they sort
    // against themselves - one leaf card in front of another composites in
    // whichever order the distance sort picked, and the result flickers as the
    // camera moves.
    //
    // A cutout surface stays OPAQUE: it writes depth, it needs no sorting, and
    // it costs one compare in the shader. Setting both this and `transparent`
    // is allowed and means what it says - blend what survives the cut - but it
    // is not what foliage wants.
    //
    // Zero as the off switch rather than a separate bool, the same way a fog
    // density of zero is how fog is turned off: the disabled path is then the
    // same arithmetic rather than a branch that can disagree with it.
    float alphaCutoff{0.0f};

    bool warnedMissingAsset{false};
};

struct MeshComponent {
    std::string primitiveType{"Cube"};
    std::string filePath;
    uint32_t vertexCount{0};
    uint32_t indexCount{0};

    // Set when a user ASSIGNS a model, cleared as soon as the material behind
    // it has been copied onto this entity's MaterialComponent. Runtime state,
    // deliberately unpersisted - like AnimatorComponent::warnedMissing and the
    // particle pool.
    //
    // It exists because the two halves happen a frame apart: the assignment is
    // a drag in the inspector, and the file is not parsed until the next
    // SyncResources, so there is nothing to copy at the moment of the drop.
    //
    // And it is a one-shot rather than a standing rule, which is the important
    // part. Importing on every resolve would overwrite an edited material every
    // time the asset hot-reloaded, and re-importing on scene load would undo
    // every material anyone had ever tuned. A file's material is a starting
    // point offered once, not an authority.
    bool importMaterialOnResolve{false};
};

struct RenderableComponent {
    // What the ids below were last resolved from: the mesh and texture paths,
    // and the registries' generation counters.
    //
    // Resolving them means three hash-map lookups, each of which builds its key
    // by concatenating strings - a heap allocation per entity per lookup, every
    // frame, to re-derive an answer that changes when someone edits a path and
    // at no other time. At a thousand entities that was three thousand
    // allocations a frame and five milliseconds.
    //
    // Zero means never resolved. That is a reserved value the signature is
    // guaranteed not to produce rather than one it is merely unlikely to, so
    // there is no flag beside it.
    uint64_t resourceSignature{0};

    uint32_t meshID{0};
    uint32_t materialID{0};

    // Resolved from MaterialComponent's texture paths by
    // RenderSystem::SyncResources. Default to the built-in white albedo, flat
    // normal and neutral ORM, so an untextured material needs no special case.
    //
    // The literals match the order TextureRegistry uploads its built-ins in,
    // which is fragile and is why SyncResources overwrites all three from the
    // registry on the first resolve rather than trusting them.
    uint32_t albedoTextureID{0};
    uint32_t normalTextureID{1};
    uint32_t ormTextureID{2};

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

    // ---- Sleeping ----
    //
    // A body that has been still for long enough stops being simulated: no
    // gravity, no integration, no impulses, and it stands in as immovable for
    // whatever is still awake. Every settled crate in a level was costing a
    // full solve per step to compute the same answer it computed last step, and
    // in a scene made mostly of settled crates that is nearly all of the cost.
    //
    // Turn it off for a body that must keep responding to something the solver
    // cannot see - a script reading its position every frame, say - though a
    // script that WRITES velocity or position wakes it anyway.
    bool allowSleep{true};

    // Runtime state, deliberately not serialised. A scene loads with everything
    // awake and settles again within kSleepTime; storing the flag would mean a
    // scene could be saved with a body asleep in mid-air.
    bool isSleeping{false};

    // How long this body has been below the sleep thresholds.
    float sleepTimer{0.0f};

    // Where it was when it fell asleep, in the SAME space the transform stores
    // (local to the parent). Compared each step so that moving a sleeping body
    // - an editor gizmo, a script, a debugger scrub - wakes it instead of
    // leaving it hanging wherever it was put.
    glm::vec3 sleepPosition{0.0f};
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

// A segment with a radius, standing along the entity's local Y.
//
// The shape a character wants, and the reason it is worth a third collider
// rather than a tuned one of the first two. A box catches on every seam it walks
// over, because the corner of a box meeting the corner of the floor tile in
// front of it is a real collision and the solver is right to stop it. A sphere
// solves that and rolls off everything instead. A capsule slides up small steps
// and stands where it is put, and neither of the other two can be made to do
// that by tuning.
//
// Also, and less obviously: a sphere of zero segment length IS a capsule, so
// giving the solver capsules means sphere-versus-sphere and
// sphere-versus-capsule stop being separate tests that can disagree.
struct CapsuleColliderComponent {
    float radius{0.5f};

    // TOTAL height, caps included, so a capsule of height 2 and radius 0.5 is
    // two units tall - the number an author measures against a doorway. The
    // straight section is height - 2 * radius, and a height below twice the
    // radius is simply a sphere rather than an error.
    float height{2.0f};

    glm::vec3 center{0.0f};
    bool isTrigger{false};
    uint32_t layer{CollisionLayer::kDefault};
    uint32_t collidesWith{CollisionLayer::kAll};
};

// A constraint holding this body to another, or to a point in the world.
//
// The gap this closes was named in the README: nothing held one body to
// another, so a door, a rope bridge, a ragdoll limb and a suspension arm all
// had to be faked by a script writing transforms - which is not a physical
// object, it is a body that ignores everything it touches.
//
// ONE joint per entity, which is what EnTT gives and is also the right shape
// for the things people build: a rope of N links is N entities each held to the
// one before it, and a ragdoll is a bone held to its parent. A mechanism that
// genuinely needs two constraints on one body needs a second entity, and that
// is a limitation rather than a design.
//
struct JointComponent {
    enum class Type : uint32_t {
        // The two anchors must coincide, and nothing rotational is constrained:
        // a ragdoll shoulder, a pendulum free to spin as it swings.
        Point = 0,

        // The two anchors must stay `distance` apart. Everything perpendicular
        // to the line is left alone, which is what lets a pendulum swing rather
        // than hang rigid.
        Distance = 1,

        // Point, plus the two rotational degrees of freedom that are not the
        // axis. A door, a wheel, a lid.
        Hinge = 2,

        // Point, plus ALL THREE rotational degrees of freedom: two bodies
        // rigidly fixed to each other.
        //
        // Not the same as parenting, which is what people reach for instead. A
        // parented child integrates in its parent's space and inherits that
        // motion on top of its own, so it is CARRIED rather than held - it
        // never pushes back, and the pair has no shared response to being hit.
        Weld = 3,
    };

    Type type{Type::Point};

    // The other end. `entt::null` anchors this body to a fixed point in the
    // WORLD at `connectedAnchor` - which is what a pendulum, a swinging sign
    // and the end posts of a rope bridge all need, and which would otherwise
    // require an invisible immovable entity per joint.
    entt::entity connectedBody{entt::null};

    // Local to this entity.
    glm::vec3 anchor{0.0f};

    // Local to the other entity, or WORLD space when there is no other entity.
    glm::vec3 connectedAnchor{0.0f};

    // Distance only.
    float distance{2.0f};

    // A rope resists STRETCHING and nothing else, so the two ends may drift
    // together freely and are caught only when the line goes taut. Without it
    // every chain is a set of rigid rods and a hanging one cannot fold.
    bool rope{false};

    // Hinge only, local to this entity. Normalised when it is used, so an
    // author may type (0, 2, 0).
    glm::vec3 axis{0.0f, 1.0f, 0.0f};

    // The SAME axis, seen from the other end: local to the connected entity, or
    // WORLD space when there is none - the rule connectedAnchor follows.
    //
    // A second field rather than reusing `axis`, because a hinge constrains one
    // body's axis to the OTHER body's, and a joint whose two axes are the same
    // vector by construction measures nothing at all: it would compare the door
    // to itself and let it flop in any direction. Two bodies that start aligned
    // want the same numbers in both; two that do not, do not.
    glm::vec3 connectedAxis{0.0f, 1.0f, 0.0f};

    // ---- Hinge only ------------------------------------------------------

    // How far it may turn, in radians, measured as THIS entity relative to the
    // connected one - which for a door hinged to the world is the door's own
    // angle, because the world is the other end and does not move.
    //
    // Zero is where the two ends' reference directions coincide, which is an
    // arbitrary configuration rather than a meaningful one. That is why the
    // inspector shows the live angle and offers to set the limits around it:
    // these are authored by looking at the number, not by predicting it.
    bool useLimit{false};
    float minAngle{-1.5707963f};   // -90 degrees
    float maxAngle{1.5707963f};

    // Drive the joint at a speed rather than let it swing. A powered hinge is
    // a wheel, a winch, a lift, a turret.
    //
    // maxMotorTorque is what stops a motor being infinitely strong: without a
    // cap it drives whatever is in the way straight through a wall instead of
    // stalling against it.
    bool useMotor{false};
    float motorSpeed{0.0f};
    float maxMotorTorque{10.0f};

    // ---- Breaking --------------------------------------------------------

    // What the joint can take before it lets go. Zero means unbreakable, which
    // is the default and what every joint written before this did.
    //
    // Two numbers rather than one, because a force and a torque are not the
    // same quantity: a rope that snaps under load and a hinge that shears off
    // its frame are different failures, and adding their magnitudes together
    // would compare metres per second to radians per second.
    float breakForce{0.0f};
    float breakTorque{0.0f};

    // Set by the solver when either threshold is passed, and NOT serialised: a
    // scene that reloaded with its joints already broken would be a level that
    // could only be played once.
    bool broken{false};

    // How much of the joint's current error to take out per step, 0 to 1.
    //
    // Not "stiffness" in the spring sense - there is no spring here, the
    // constraint is exact. This is the same knob the contact solver's
    // positional correction has, and for the same reason: pulling all the way
    // to zero every step makes a loaded joint vibrate, because floating-point
    // error re-creates the error immediately.
    float stiffness{0.8f};

    bool enabled{true};
};

// The terrain, as something you can stand on.
//
// The gap this closes was in the README for a long time: the procedurally
// generated terrain was scenery you fell through. It is not a box and it is not
// a sphere, and approximating it with a stack of either takes thousands.
//
// Described by the SAME three numbers the "Terrain" mesh primitive is, and
// deliberately not by a stored grid of heights. Both sides resolve through
// TerrainGenerator::SampleHeight, so the collider is the surface you can see by
// construction rather than by a scene file staying in step with it - and a
// scene file holding four thousand floats would be unreadable and would go
// stale the first time anyone tuned the terrain anyway.
//
// The consequence, stated so nobody has to discover it: these must match the
// mesh's. The defaults are what MeshRegistry generates for the "Terrain"
// primitive, and TerrainGenerator::kPrimitive* is the one place both read.
struct HeightfieldColliderComponent {
    // VERTEX counts per axis, so a 64 x 64 field has 63 x 63 cells.
    uint32_t width{64};
    uint32_t depth{64};
    float heightScale{0.6f};

    // How far the solid extends BELOW the surface.
    //
    // Not cosmetic. A body that has ended up under the terrain - spawned there,
    // dragged there by a gizmo, put there by a script - has to be pushed out of
    // the top, and without a bottom there is no way to say when it has left.
    // Past this depth it is through and falls, which is a better answer than
    // being shot up through the whole hill.
    float thickness{4.0f};

    // No `center` offset, unlike the other three colliders. A heightfield's
    // position is the entity's transform and nothing else: the grid is centred
    // the way the mesh is, and an offset here would put the collider somewhere
    // the mesh is not.
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

    // Where this script's clock started, in SIMULATED seconds. Kept so a script
    // enabled part way through a run begins at zero rather than at whatever the
    // world had reached - and so `elapsed` can be derived from the simulation
    // clock rather than accumulated, which is what makes it reproducible.
    float clockOrigin{0.0f};
    bool clockStarted{false};
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

// A field: somewhere a player can type their name.
//
// The canvas could be read and not written to. It had text, panels and buttons,
// so a game could show a score and offer a menu, and could not ask a single
// question - which is why "nothing can accept a typed name" was a roadmap item
// rather than a missing widget.
//
// Built like the button, one component rather than a panel plus a label, for
// the same reason: a field is one thing to whoever is authoring the screen.
struct UITextFieldComponent {
    std::string text;

    // Shown, dimmed, while the field is empty. A blank box does not say what
    // it wants.
    std::string placeholder{"Enter your name"};

    UIAnchor anchor{UIAnchor::Center};
    glm::vec2 offset{0.0f, 0.0f};
    glm::vec2 size{360.0f, 56.0f};

    float fontSize{26.0f};
    float cornerRadius{8.0f};

    // CHARACTERS, not bytes, because that is what the author counting them can
    // see. Zero means no limit.
    int maxLength{24};

    glm::vec4 color{0.10f, 0.11f, 0.14f, 0.96f};
    glm::vec4 focusColor{0.13f, 0.15f, 0.20f, 0.98f};
    glm::vec4 borderColor{0.30f, 0.33f, 0.40f, 1.0f};
    glm::vec4 focusBorderColor{1.0f, 0.48f, 0.24f, 1.0f};
    glm::vec4 textColor{0.94f, 0.95f, 0.97f, 1.0f};
    glm::vec4 placeholderColor{0.55f, 0.58f, 0.64f, 1.0f};

    bool visible{true};
    bool enabled{true};

    // Runtime, and absent from the scene format for the same reason a button's
    // press flags are: a field saved mid-edit would come back focused, with a
    // caret in the middle of a name nobody is typing, and a `submitted` restored
    // from a Play snapshot would answer a question that was never asked.
    bool hovered{false};
    bool focused{false};

    // True for exactly one frame, when Enter was pressed while focused.
    bool submitted{false};

    // Byte offset of the caret into `text`, always on a character boundary.
    int caret{0};
};

} // namespace Supersonic
