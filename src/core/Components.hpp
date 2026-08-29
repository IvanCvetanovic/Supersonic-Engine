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
    // Which projection this camera builds.
    //
    // The engine was perspective-only: getProjectionMatrix called
    // glm::perspective with no branch and no field to select anything else, so
    // a 2D game could not be authored at all. A side-scroller wants parallel
    // projection - two units the same size are the same size on screen wherever
    // they stand on the lane - and under perspective they visibly are not.
    enum class Projection : uint8_t { Perspective = 0, Orthographic = 1 };

    Projection projection{Projection::Perspective};

    // How many WORLD units the viewport spans vertically in orthographic.
    // Width follows from `aspect`, so widening the window shows more of the
    // world rather than stretching it - which is what a side-scroller wants and
    // what fov does for perspective.
    float orthoHeight{10.0f};

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

    // Whether W/A/S/D/Space/Shift and a right-drag fly this camera.
    //
    // THE OFF SWITCH A GAME NEEDS, and there was not one. A packaged game goes
    // straight into Play, and CameraSystem runs in Play, so a shipped game had
    // an editor fly camera bolted to the same keys the default bindings already
    // give a player: MoveX is D/A, MoveY is W/S, Jump is Space, Sprint is Left
    // Shift. Pressing W walked the character forward AND flew the view through
    // the wall behind it, and nothing could turn the second one off.
    //
    // Defaults TRUE so that every scene authored before this existed behaves
    // exactly as it did - the flycam is how the demo scene is meant to be flown,
    // and a silent change to that would be a worse bug than the one being fixed.
    // A game turns it off in the inspector and the scene carries the answer.
    bool flyControlsEnabled{true};

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    // True when the projection has no eye point - every ray is parallel.
    // Picking has to be told, because unprojecting a screen point gives a
    // DIRECTION under perspective and a POSITION under orthographic.
    bool isOrthographic() const { return projection == Projection::Orthographic; }

    glm::mat4 getProjectionMatrix() const {
        // The Y flip is applied ONCE, after either branch. Vulkan's clip space
        // has +Y down where GLM builds for +Y up, and doing it in both branches
        // is two places for it to be forgotten.
        glm::mat4 proj;
        if (projection == Projection::Orthographic) {
            const float halfHeight = orthoHeight * 0.5f;
            const float halfWidth = halfHeight * aspect;
            proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane,
                              farPlane);
        } else {
            proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
        }
        proj[1][1] *= -1.0f;
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

// A 2D affine transform on texture coordinates.
//
// Scrolling rain and a flipbook flame are the same feature: the mesh never
// moves and the texture coordinates do. Without one, a sixteen-frame fire is
// sixteen meshes or sixteen textures, and rain is a scrolling vertex buffer.
//
// PER DRAW rather than per material, and that is the whole reason it is not
// simply three floats on a material asset. Twenty flames share one texture and
// one material and each is on its OWN frame, so the number has to travel with
// the draw. The push constant block is full at 128 bytes - the guaranteed
// minimum - so it travels as an index into a per-frame storage buffer instead,
// exactly as a skinned draw's joint matrices do.
//
// SLOT 0 IS ALWAYS THE IDENTITY and is written every frame whether anything
// asked for a transform or not. That is what lets the shader multiply
// unconditionally with no branch and no bounds check: a draw that never heard
// of this - the particle path builds `PushConstantData push{}` and touches
// nothing - reads slot 0 and gets its texture coordinates back unchanged.
struct UvTransform {
    // The two COLUMNS of the 2x2: xy is where U points, zw is where V points.
    // That order is not arbitrary - it is exactly what GLSL's mat2(x,y,z,w)
    // constructor takes, so the shader needs no transpose and cannot get one
    // wrong.
    glm::vec4 axes{1.0f, 0.0f, 0.0f, 1.0f};

    // xy is the translation. zw are PADDING AND MUST STAY: std430 starts every
    // vec4 on a sixteen-byte boundary, so a shorter entry here would leave the
    // array stride at 32 on the C++ side and 24 on the shader side, and each
    // transform after the first would read half of its neighbour.
    glm::vec4 offset{0.0f, 0.0f, 0.0f, 0.0f};
};

// One layout, two transports: the scene pass reads these out of a storage
// buffer and the cut-out depth pass takes one by value in its push constants.
// The size is asserted because that is the only thing keeping the two in step.
static_assert(sizeof(UvTransform) == 32, "UvTransform must match its std430 stride");

// Builds one from the three numbers a person authors.
//
// SCALE, then ROTATE, then TRANSLATE - the order KHR_texture_transform
// specifies, and the order Bevy's Affine2::from_scale_angle_translation
// composes in, so a material ported from either arrives looking the same
// instead of looking nearly the same.
//
// The columns are written out rather than built with glm::rotate and a matrix
// multiply because the whole thing is four multiplies, and because a reader
// checking this against the spec should be able to see the four numbers.
inline UvTransform MakeUvTransform(const glm::vec2& scale, float rotation,
                                   const glm::vec2& offset) {
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);

    UvTransform out;
    // Column 0 is where U points, column 1 where V points.
    out.axes = glm::vec4(c * scale.x, s * scale.x, -s * scale.y, c * scale.y);
    out.offset = glm::vec4(offset.x, offset.y, 0.0f, 0.0f);
    return out;
}

// Where a draw's transform slot rides in its flags word.
//
// A deliberate crowding of a field documented as "per-draw switches, one bit
// each": the push constant block is exactly 128 bytes, the guaranteed minimum,
// with no thirteenth byte to put an index in - and a per-draw transform can
// only travel as a per-draw index.
//
// TWELVE BITS, ABOVE THE SWITCHES, and both halves of that matter. Starting at
// 8 leaves the whole low byte to switches, so packing a slot cannot disturb the
// unlit bit - which is exactly the bit both of the materials this feature was
// built for happen to set, and a scrolling surface that quietly became a lit
// one is not a bug anybody reports. Stopping at 12 leaves the sign bit alone,
// because the field is an int32_t and shifting into bit 31 is undefined.
//
// Here rather than beside PushConstantData because it is a protocol between
// three places - the draw loop, the gather and the shader - and only one of
// them can include a Vulkan header.
constexpr int32_t kUvSlotShift = 8;
constexpr int32_t kUvSlotMask = 0xFFF;

constexpr int32_t PackUvSlot(int32_t flags, int32_t slot) {
    return (flags & ~(kUvSlotMask << kUvSlotShift))
         | ((slot & kUvSlotMask) << kUvSlotShift);
}

constexpr int32_t UnpackUvSlot(int32_t flags) {
    return (flags >> kUvSlotShift) & kUvSlotMask;
}

// One surface of a model, re-materialised by the name the file gave it.
//
// A model is authored as several named surfaces and a game addresses them by
// those names: the same chassis is a player unit in teal and an enemy in rust,
// which is ONE model and a pair of lookups rather than two models. Without this
// the only ways to colour a team are to ship the model twice or to tint the
// whole thing, and tinting the whole thing takes the visor and the exhaust
// glow with it.
//
// Matched against MeshMaterial::name, which the importer carries through
// verbatim. A name that matches no surface is inert rather than an error - a
// model is allowed to not have the part being described, and a rig swapped for
// one with fewer pieces should not start logging every frame.
struct SurfaceOverride {
    std::string surface;

    // REPLACE the file's values rather than modulating them. A team colour is
    // not a tint of whatever the artist happened to paint; it is the answer.
    // The entity's own albedoColor still multiplies on top of this, which is
    // what keeps a hit flash working over a team-coloured unit.
    glm::vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness{0.5f};
    float metallic{0.0f};

    glm::vec3 emissiveColor{0.0f};
    float emissiveStrength{0.0f};
};

// The overrides one entity applies to its mesh's surfaces.
//
// A VECTOR AND A LINEAR SCAN, not a map. There are a handful of these per
// entity - four is a lot - and the comparison is against a short string that
// almost always differs in its first character or its length, which
// std::string::operator== checks before it looks at any bytes. A hash map would
// cost an allocation per entity and a hash of the name per section per frame to
// avoid a scan of four.
struct SurfaceOverridesComponent {
    std::vector<SurfaceOverride> overrides;

    const SurfaceOverride* Find(const std::string& surface) const {
        if (surface.empty()) return nullptr;
        for (const SurfaceOverride& entry : overrides) {
            if (entry.surface == surface) return &entry;
        }
        return nullptr;
    }
};

// The two ticks a drawn frame sits between.
//
// Add it to anything whose motion comes from the simulation and should look
// smooth at a tick rate below the frame rate. Leave it off scenery, off UI, and
// off anything a script moves per frame - those are already drawn where they
// are, and interpolating them would draw them one tick in the past.
//
// EVERYTHING HERE IS RUNTIME STATE and none of it is serialised. A scene that
// stored these would restore a half-finished slide between two ticks that no
// longer exist, and the first tick after a load overwrites all of it anyway.
struct InterpolatedTransformComponent {
    glm::vec3 previousPosition{0.0f};
    glm::vec3 previousRotation{0.0f};
    glm::vec3 previousScale{1.0f};

    // The simulation's authoritative transform, kept here because
    // TransformComponent holds the DRAWN one between ticks. A tick that read the
    // drawn value would make the world depend on the frame rate, which is what
    // the fixed tick exists to prevent.
    glm::vec3 currentPosition{0.0f};
    glm::vec3 currentRotation{0.0f};
    glm::vec3 currentScale{1.0f};

    // False until the entity has run one whole tick. Until then there is no
    // previous state to come from, and interpolating out of an uninitialised
    // one streaks the entity in from wherever that memory happened to point.
    bool captured{false};
};

struct MaterialComponent {
    glm::vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};

    // Emit the authored colour and skip lighting completely.
    //
    // For surfaces that are not surfaces: a 2D sprite, a flat-colour quad, a
    // UI panel in the world. Nothing about a lamp, a shadow or an environment
    // should reach one, and with this set nothing does.
    //
    // albedoColor is NOT clamped on this path, so a value above 1.0 stays above
    // 1.0 and reaches the bright pass. That is deliberate: it is how a flat
    // sprite flashes white when it is hit.
    bool unlit{false};
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

    // --- The texture coordinate transform -----------------------------------
    //
    // Applied to EVERY map this material samples - albedo, normal and the
    // packed ORM - because they describe the same surface and sliding one off
    // the others is never what anybody meant.
    //
    // Scrolling rain and a flipbook flame are one feature seen twice: the mesh
    // stands still and the texture coordinates move. Without it a sixteen-frame
    // fire is sixteen meshes or sixteen textures, and rain is a vertex buffer
    // rewritten every frame.
    //
    // Composed as SCALE, then ROTATION, then OFFSET, which is the order glTF's
    // KHR_texture_transform specifies and the order every tool that authors one
    // uses. Written as three authored numbers rather than as the matrix they
    // become because "half the texture, a sixteenth along" is a thing a person
    // can type and a 2x2 is not.
    //
    // These are PER ENTITY and MaterialSystem deliberately does not copy them
    // off a shared asset: twenty flames share one material and each is on its
    // own frame, so a shared asset overwriting this every frame would lock them
    // together. The look is shared; the position in the animation is not.
    glm::vec2 uvScale{1.0f, 1.0f};

    // Radians, about the texture coordinate origin - the top-left corner, not
    // the middle of the image. Rotating about the centre is offset(0.5) then
    // rotate then offset(-0.5), which this can express and deliberately does
    // not do for you: guessing a pivot is how a decal ends up half a texture
    // away from where it was authored.
    float uvRotation{0.0f};

    glm::vec2 uvOffset{0.0f, 0.0f};

    // Where this material's transform landed in the frame's transform buffer.
    // Renderer scratch, rewritten every frame by MaterialSystem::GatherUvTransforms
    // and meaningless outside one - the same arrangement, for the same reason,
    // as SkinnedMeshComponent::paletteBase.
    //
    // ZERO IS THE IDENTITY SLOT, which is why zero is the default: a material
    // nothing gathered, or one gathered in a frame that ran out of slots, draws
    // untransformed rather than reading whatever the last frame left behind.
    int32_t uvSlot{0};

    // True when the three numbers above are not the identity, which is the only
    // question the gather asks and the only reason a material takes a slot at
    // all. A scene where nothing scrolls uploads one identity entry and stops.
    bool HasUvTransform() const {
        return uvScale != glm::vec2(1.0f, 1.0f) || uvRotation != 0.0f ||
               uvOffset != glm::vec2(0.0f, 0.0f);
    }

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

    // Which bound environment lights this object, resolved once per frame from
    // the scene's reflection probes. Zero is the scene-wide environment, which
    // is what everything gets when there are no probes - so a scene without
    // them behaves exactly as it did.
    int32_t probeSlot{0};

    // Draw order among surfaces the depth buffer cannot separate.
    //
    // The renderer submits opaque geometry in whatever order the registry hands
    // it over and lets depth decide the rest, which is correct for solids and
    // decides NOTHING for coplanar quads - a 2D game is made of those, and
    // Godot orders them by child index. This is that index.
    //
    // Ascending: a higher key is submitted later, and the depth compare is
    // lessOrEqual, so at EQUAL depth the later fragment replaces the earlier
    // one and the higher key ends up on top.
    //
    // The limit, stated rather than discovered: it decides TIES. It cannot pull
    // a surface in front of geometry that is genuinely nearer, because the
    // depth test still runs. For flat quads sharing a plane that is the whole
    // problem; for anything else it is not a layering system.
    //
    // Zero is "no opinion", which is what every scene on disk holds - and when
    // every key is zero the sort is skipped entirely rather than performed and
    // found to be a no-op.
    int32_t sortKey{0};
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
    // A hinge that pulls toward an angle rather than holding one.
    //
    // Authored as a frequency in hertz and a damping RATIO because that pair is
    // mass-independent: "3 Hz, critically damped" behaves the same on a garden
    // gate and on a vault door, where a stiffness that suited one would throw
    // the other across the room.
    //
    // A damping ratio of 1 is critically damped - it returns to rest as fast as
    // it can without ever going past. Below 1 it overshoots and oscillates,
    // above 1 it crawls. A pure damper with no spring is a motor with a target
    // speed of zero, which already exists.
    bool useSpring{false};
    float springFrequency{0.0f};
    float springDamping{1.0f};
    float springRestAngle{0.0f};

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

    // Which joints the solver reaches first. Lower solves earlier.
    //
    // The solve is Gauss-Seidel: joint i reads the velocities joint i-1 just
    // wrote, so order already decides the answer - it was simply whatever order
    // the entity pool happened to be in, which is reverse creation order
    // mutated by every destroy. A long articulated chain converges far faster
    // solved root to tip than tip to root, and had no way to say so.
    //
    // A priority, deliberately, and not a dependency graph. Cycles are ordinary
    // here - a ragdoll closed at the hips, a bridge tied at both ends, a crate
    // both welded and hinged - and a topological sort has no answer for one,
    // while a sort key always terminates. The sort is stable, so ties keep the
    // order they had and a scene that never sets this solves exactly as before.
    int32_t solveOrder{0};

    bool enabled{true};
};

// The shape you can see, as something you can hit.
//
// The last collider the narrowphase was missing. It took a box, a sphere, a
// capsule and a heightfield, so a ramp with a bevel, a rock, a wedge or a wing
// had to be approximated by a group of those - each one convex solid built from
// three or four boxes that never quite fit.
//
// The hull is the CONVEX hull of the asset's vertices, which is exact for
// anything convex and an over-estimate for anything else: a doughnut collides
// as a disc and a chair as the block it sits in. Concave collision means
// decomposing the shape into several hulls, which is a different feature with a
// different failure mode.
//
// Unlike every other collider here, a NON-UNIFORM scale is exact: a sphere has
// to collapse its three extents to one radius and a capsule its two, but a
// linear transform of a convex set is still convex, so a hull is simply the
// shape it is drawn as.
struct ConvexHullColliderComponent {
    // Where the points come from. BOTH empty means the entity's own
    // MeshComponent, which is what an author wants nine times in ten: the
    // collider is the shape you can see, and it follows when the mesh changes.
    //
    // Naming a source separately is for the case that matters at scale - a
    // detailed model with a simple collision proxy beside it.
    std::string sourcePath;
    std::string sourcePrimitive;

    bool isTrigger{false};
    uint32_t layer{CollisionLayer::kDefault};
    uint32_t collidesWith{CollisionLayer::kAll};
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

    // Which version of that rig this was built from.
    //
    // A hot reload replaces a rig's contents in place, so the id does not
    // change and everything derived below - the joint palette, the bind bounds -
    // would go on describing the rig from before the export. A joint count that
    // happened to stay the same made that invisible.
    uint32_t skeletonGeneration{0};

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

// A local environment, for objects inside a box.
//
// There was one environment for a whole scene, so a room and the outdoors it
// opens onto lit identically - the interior reflected the sky it could not see.
//
// A BOX rather than a sphere with a falloff, and a hard choice rather than a
// blend between two. Both are deliberate and both are about the push constant:
// it is 120 of a guaranteed 128 bytes, so there are exactly two 4-byte slots
// left. One holds an index. Blending needs a second index and a weight, which
// means bit-packing, and it doubles the cube fetches per fragment. The index is
// already per-object, so blending remains open rather than being designed out.
//
// Position comes from the entity's TransformComponent, like everything else.
struct ReflectionProbeComponent {
    // Half the box's size, in world units, about the entity's position.
    glm::vec3 halfExtent{5.0f};

    // The environment objects inside the box light from. Empty means the probe
    // is authored but does nothing, which is a state worth being able to reach
    // while placing one.
    std::string hdriPath;
    float intensity{1.0f};

    // Filled in by the renderer, which owns the slots. Not serialised: it is
    // whichever descriptor the probe happened to be given this run.
    int32_t resolvedSlot{-1};
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

// A ring, a disc or a line - the shapes that are drawn rather than composed.
//
// Some overlays are not made of rectangles and text. A selection marker is a
// circle around a unit; an order line runs from the unit to where it was sent.
// Godot draws those in _draw() with draw_arc and draw_line, and this engine had
// no way to say either: the 3D pipeline hardcodes triangle topology, so nothing
// draws a line in the world at all.
//
// ImGui's draw list already has AddCircle, AddCircleFilled and AddLine, and
// AddCircle takes the same arguments Godot's draw_arc does, segment count and
// all. What was missing was never the shape - it was a way to put one where a
// point in the WORLD is. So that is what this adds, on the same path the HUD
// already draws through, which is also why it composites with the HUD instead
// of fighting it.
//
// Sizes are in authored units at the reference height, like fontSize and
// everything else here, NOT in world units. A marker therefore keeps its
// proportion of the screen rather than growing as the camera closes in. For a
// 2D game with a fixed camera zoom - which is the case this exists for - those
// are the same thing, and where they are not, a marker that stays legible is
// the more useful of the two.
struct UIShapeComponent {
    enum class Kind : uint8_t {
        Ring,   // draw_arc(centre, r, 0, TAU, segments, colour, width)
        Disc,   // draw_circle
        Line,   // draw_line, from this entity to `endpoint`
    };

    Kind kind{Kind::Ring};

    // Follow a point in the world instead of an edge of the screen, and read
    // exactly as UITextComponent::worldSpace does - including the cull when the
    // point is behind the camera, without which a marker behind the viewer
    // lands mirrored in front of it and reads as a selection nobody made.
    //
    // Defaulted ON, unlike text: a screen-space rectangle is already a
    // UIPanelComponent, so a shape that is not following something in the world
    // is the unusual one.
    bool worldSpace{true};

    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{0.0f, 0.0f};

    // Ring and Disc.
    float radius{24.0f};

    // Line only: the far end. A world position when worldSpace, and otherwise
    // an offset in authored units from the start, using x and y - measured the
    // same way the start is, so the two ends never mean different things.
    glm::vec3 endpoint{0.0f, 0.0f, 0.0f};

    // Outline width for a Ring or a Line, in authored units, and never allowed
    // to round down to nothing: a marker that disappears on a small window is
    // indistinguishable from one that was never drawn.
    float thickness{3.0f};

    // How many straight edges a circle is made of. Godot's order marker asks
    // for 40, and ImGui takes the same number in the same place.
    int32_t segments{40};

    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    bool visible{true};
};

// Lays its CHILDREN out in a row or a column instead of each one placing itself.
//
// Every UI element in this engine positions itself from an anchor and an
// offset, which means an author computes every position by hand and re-computes
// them all whenever anything changes size. A menu of five buttons is five
// offsets that have to agree; a label whose text grew moves nothing.
//
// A child is any UI entity whose HierarchyComponent names this one. The stack
// measures each of them, sizes itself to the result, places itself by its own
// anchor, and hands each child a rectangle - so the child's own anchor and
// offset are ignored while it is in a stack. That is the point: it is no longer
// deciding where it goes.
struct UIStackComponent {
    // Along x rather than down y. Godot spells these HBoxContainer and
    // VBoxContainer; the difference is one axis, so it is one bool.
    bool horizontal{false};

    // Where the whole BLOCK attaches, once its size is known. Setting this to
    // Center is Godot's CenterContainer - the group is centred, not each child
    // on the same point.
    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{0.0f, 0.0f};

    // Between children, in authored units. Between, not after: n children have
    // n-1 gaps, and a stack that trails one is half a gap off centre.
    float spacing{8.0f};

    bool visible{true};
};

// Where an element sits among its siblings in a stack.
//
// entt's iteration order is not a contract - it depends on pool sizes and on
// what was created when - so "the order they were created in" is not something
// a menu can be built on. This is the same answer RenderableComponent::sortKey
// gives for draw order, and for the same reason.
//
// Absent means zero, and equal orders keep whatever order the view produced,
// which is stable within a run and is fine for children nobody ranked.
struct UIOrderComponent {
    int32_t order{0};

    // WHICH LAYER the element draws and hit-tests on. Godot spells this
    // CanvasLayer.layer.
    //
    // A separate field from `order` above, and it has to be, though it was one
    // field to begin with on the argument that a layer is "the same question at
    // a different scope". It is not, and a menu shows why: its buttons need
    // 0..3 to stack top to bottom, and they need one shared raised value to sit
    // above the backdrop that dims the game behind them. One integer cannot
    // carry both, and the failure is not subtle - the buttons take their
    // position in the column as their layer, lose the topmost test to their own
    // backdrop, and the menu cannot be clicked at all.
    //
    // Within a layer the type order still holds - shapes, panels, buttons,
    // fields, text - so a label still reads on top of its own backdrop without
    // anybody ranking it.
    int32_t layer{0};
};

struct UITextComponent {
    std::string text{"Score: 0"};

    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{24.0f, 24.0f};

    // Follow a point in the WORLD instead of an edge of the screen.
    //
    // A name plate over a unit, a damage number where the hit landed, a build
    // prompt above a foundation - none of those are anchored to a corner, and
    // there was no way to express them at all. With this set, the entity's own
    // world position is projected through the camera and `offset` is measured
    // from THAT, in the same authored units, with the text centred horizontally
    // on it rather than hung off an anchor.
    //
    // The entity therefore needs a transform, and normally is a child of the
    // thing it labels - so it follows for free through the hierarchy rather
    // than by anybody copying a position each frame.
    //
    // Culled when the point is behind the camera or outside the depth range,
    // which matters more than it sounds: without that check a label behind the
    // viewer projects to a mirrored position in front of it and reads as a
    // second unit that is not there.
    bool worldSpace{false};

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
// A picture in the UI.
//
// The widget set was text, panel, button and field - four things, none of which
// can show an image. That is a minimap, a fog overlay, a portrait, an item
// icon, a logo and a crosshair, all absent for the same reason.
//
// The handle is OPAQUE, and deliberately a plain integer rather than a texture
// type. Components.hpp is included by gameplay code that must not need Vulkan
// or ImGui in its translation unit, and the UI draw path needs an ImTextureID -
// so the value is passed through untouched and interpreted only at the point of
// drawing. Zero means "nothing to draw", which is what an image whose texture
// has not finished uploading is.
//
// Get one from the renderer's UI image service, published in the registry
// context; it is the thing that owns the pixels and the device resources.
struct UIImageComponent {
    UIAnchor anchor{UIAnchor::TopLeft};
    glm::vec2 offset{24.0f, 24.0f};
    glm::vec2 size{128.0f, 128.0f};

    // An ImTextureID, carried as an integer. See the note above.
    uint64_t texture{0};

    // The sub-rectangle of the texture to show, 0..1. The whole thing by
    // default. An atlas is the reason this exists - one upload, many icons -
    // and a minimap that shows only the explored region is the other.
    glm::vec2 uvMin{0.0f, 0.0f};
    glm::vec2 uvMax{1.0f, 1.0f};

    // Multiplied into the image. White leaves it alone; alpha is what lets a
    // fog layer sit over a minimap, and what lets an unavailable icon grey out
    // without a second asset.
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};

    float cornerRadius{0.0f};

    bool visible{true};
};

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

    // THE CLICK, LATCHED FOR THE TICK, which is not the frame.
    //
    // `clicked` above is true for exactly one FRAME, and scripts run on the
    // tick. Those are different periods and the mismatch is wrong in both
    // directions at once, which is the same bug the keyboard had and was fixed
    // for: a frame that runs no tick loses the click entirely - at 20 Hz on a
    // 144 Hz display that is six frames out of seven - and a frame that runs
    // three ticks reports the same click to all three, so one press of Buy
    // buys three.
    //
    // So the click is held from the frame that produced it until a tick takes
    // it, and exactly one tick sees it. `clicked` is left alone because the
    // renderer and the editor genuinely do want the frame's answer.
    //
    // Not serialised, for the reason above and more so: a latched click
    // restored from a snapshot would fire on the first tick after loading.
    bool clickPending{false};
    bool clickedThisTick{false};
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
