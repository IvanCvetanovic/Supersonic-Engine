#include "core/ComponentCodec.hpp"
#include <vector>
#include <sstream>
#include "core/Log.hpp"
#include <limits>
#include <cmath>

#include "core/Components.hpp"
#include "core/AssetDatabase.hpp"
#include "core/TerrainGenerator.hpp"

#include <algorithm>

namespace Supersonic {

namespace ComponentCodec {
namespace {

// Every asset reference is written as a PAIR: the identity, and the path it had
// when it was saved.
//
// Both, not either. The identity is what survives a rename, and the path is
// what a person reading the file needs in order to know what the entry is - a
// scene full of thirty-two hex characters and nothing else is unreadable and
// unmergeable. The path is also the migration story: every scene written before
// identities existed has one and no identity, and goes on working.
//
// Empty identities are not written at all, so a project that has never been
// imported produces exactly the file it always did.
void writeAssetRef(std::ostream& out, const std::string& indent, const std::string& key,
                   const std::string& path, const char* trailing) {
    out << indent << "  \"" << key << "\": \"" << Json::Escape(path) << "\"";

    const std::string guid = AssetDatabase::Instance().GuidForPath(path);
    if (!guid.empty()) {
        out << ",\n" << indent << "  \"" << key << "Guid\": \"" << guid << "\"";
    }
    out << trailing;
}

// Where that reference points NOW.
//
// The identity wins when it resolves. When it does not, the saved path is used
// and the database counts it - see AssetDatabase::Resolve for why that has to
// be loud rather than quietly correct-looking.
std::string readAssetRef(const Json::Value& node, const std::string& key,
                         const std::string& fallback = "") {
    const std::string path = node[key].AsString(fallback);
    const std::string guid = node[key + "Guid"].AsString("");
    if (path.empty() && guid.empty()) return path;
    return AssetDatabase::Instance().Resolve(guid, path);
}

struct RegisteredComponent {
    std::string key;
    ComponentWriter writer;
    ComponentReader reader;
};

// A function-local static rather than a namespace-scope one: this lives in a
// static library linked into several binaries, and a function-local static has
// one definition however many translation units reach it. The same reason the
// profiler's accumulator is shaped this way.
std::vector<RegisteredComponent>& registeredComponents() {
    static std::vector<RegisteredComponent> registry;
    return registry;
}

} // namespace
} // namespace ComponentCodec


namespace {

// JSON has no way to spell inf or nan, and this codec's own reader rejects all
// three - parseNumber scans digits, '.', 'e' and sign, so "inf" is not even a
// number to it. A non-finite float therefore produced a file that could never
// be read back: Serialize reported "Saved 18 entities", and from then on the
// scene would not load, Play/Stop could not restore it, and undo popped the
// snapshot and dropped it.
//
// Reachable without a hostile file. The inspector draws Position and Scale with
// DragFloat's v_min and v_max both 0.0f, which tells ImGui not to clamp typed
// input, so ctrl-clicking a field and typing 1e40 is enough.
//
// Substituting keeps the file loadable, which is the property worth protecting:
// an entity in a strange place can be dragged back, a file that will not parse
// cannot. inf becomes the largest finite float rather than zero so "very far
// away" does not silently become "at the origin", and the caller is told.
float jsonSafe(float v, const char* field) {
    if (std::isfinite(v)) return v;
    const float replacement = std::isnan(v)
        ? 0.0f
        : std::copysign(std::numeric_limits<float>::max(), v);
    SUPERSONIC_LOG_WARN("ComponentCodec")
        << field << " was " << (std::isnan(v) ? "nan" : "infinite")
        << "; wrote " << replacement << " instead so the scene stays loadable";
    return replacement;
}


void writeVec3(std::ostream& os, const glm::vec3& v, const char* field = "a vector") {
    os << "[" << jsonSafe(v.x, field) << ", " << jsonSafe(v.y, field)
       << ", " << jsonSafe(v.z, field) << "]";
}

glm::vec3 readVec3(const Json::Value& value, const glm::vec3& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 3) return fallback;
    return glm::vec3(arr[0].AsFloat(fallback.x),
                     arr[1].AsFloat(fallback.y),
                     arr[2].AsFloat(fallback.z));
}

void writeBool3(std::ostream& os, const glm::bvec3& v) {
    os << "[" << (v.x ? "true" : "false") << ", " << (v.y ? "true" : "false") << ", "
       << (v.z ? "true" : "false") << "]";
}

// All false when the key is missing, which is every scene written before the
// per-axis locks existed: they must load with nothing locked.
glm::bvec3 readBool3(const Json::Value& value) {
    const auto& arr = value.AsArray();
    if (arr.size() != 3) return glm::bvec3(false);
    return glm::bvec3(arr[0].AsBool(false), arr[1].AsBool(false), arr[2].AsBool(false));
}

glm::vec2 readVec2(const Json::Value& value, const glm::vec2& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 2) return fallback;
    return glm::vec2(arr[0].AsFloat(fallback.x),
                     arr[1].AsFloat(fallback.y));
}

// Clamped rather than cast straight through: an out-of-range value from a
// hand-edited scene would index past the anchor table and place the element
// at whatever happened to be in memory after it.
UIAnchor readAnchor(const Json::Value& value, UIAnchor fallback) {
    if (!value.IsNumber()) return fallback;
    const int raw = static_cast<int>(value.AsNumber(0.0));
    return static_cast<UIAnchor>(std::clamp(raw, 0, static_cast<int>(UIAnchor::BottomRight)));
}

glm::vec4 readVec4(const Json::Value& value, const glm::vec4& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 4) return fallback;
    return glm::vec4(arr[0].AsFloat(fallback.x),
                     arr[1].AsFloat(fallback.y),
                     arr[2].AsFloat(fallback.z),
                     arr[3].AsFloat(fallback.w));
}

} // namespace

namespace ComponentCodec {

void Write(entt::registry& registry, entt::entity entity, std::ostream& out,
           const std::string& indent) {
    if (const auto* tag = registry.try_get<TagComponent>(entity)) {
        out << indent << "\"Tag\": \"" << Json::Escape(tag->tag) << "\",\n";
    }

    if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
        out << indent << "\"Transform\": {\n";
        out << indent << "  \"Position\": "; writeVec3(out, transform->position); out << ",\n";
        out << indent << "  \"Rotation\": "; writeVec3(out, transform->rotation); out << ",\n";
        out << indent << "  \"Scale\": ";    writeVec3(out, transform->scale);    out << "\n";
        out << indent << "},\n";
    }

    if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
        out << indent << "\"Mesh\": {\n";
        out << indent << "  \"Primitive\": \"" << Json::Escape(mesh->primitiveType) << "\",\n";
        writeAssetRef(out, indent, "Path", mesh->filePath, "\n");
        out << indent << "},\n";
    }

    if (const auto* light = registry.try_get<LightComponent>(entity)) {
        out << indent << "\"Light\": {\n";
        out << indent << "  \"Type\": " << light->type << ",\n";
        out << indent << "  \"Direction\": "; writeVec3(out, light->direction); out << ",\n";
        out << indent << "  \"Color\": ";     writeVec3(out, light->color);     out << ",\n";
        out << indent << "  \"Ambient\": ";       writeVec3(out, light->ambient);       out << ",\n";
        out << indent << "  \"AmbientGround\": "; writeVec3(out, light->ambientGround); out << ",\n";
        out << indent << "  \"Intensity\": " << light->intensity << ",\n";
        out << indent << "  \"Range\": " << light->range << ",\n";
        out << indent << "  \"InnerAngle\": " << light->innerAngle << ",\n";
        out << indent << "  \"OuterAngle\": " << light->outerAngle << ",\n";
        out << indent << "  \"CastsShadow\": " << (light->castsShadow ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    // Its own key rather than a Type of "Light": a 2D light shares no field with
    // a scene light but a colour, and a reader that met one inside "Light" would
    // make a PBR point light of it.
    if (const auto* light = registry.try_get<Light2DComponent>(entity)) {
        out << indent << "\"Light2D\": {\n";
        out << indent << "  \"Color\": ";
        writeVec3(out, light->color, "Light2DComponent.color");
        out << ",\n";
        out << indent << "  \"Intensity\": " << jsonSafe(light->intensity, "Light2DComponent.intensity") << ",\n";
        out << indent << "  \"Range\": " << jsonSafe(light->range, "Light2DComponent.range") << ",\n";
        out << indent << "  \"Height\": " << jsonSafe(light->height, "Light2DComponent.height") << ",\n";
        out << indent << "  \"Layers\": " << static_cast<int>(light->layers) << ",\n";
        out << indent << "  \"Enabled\": " << (light->enabled ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* camera = registry.try_get<CameraComponent>(entity)) {
        out << indent << "\"Camera\": {\n";
        out << indent << "  \"Orthographic\": "
            << (camera->projection == CameraComponent::Projection::Orthographic
                    ? "true" : "false")
            << ",\n";
        out << indent << "  \"OrthoHeight\": " << camera->orthoHeight << ",\n";
        out << indent << "  \"FOV\": " << camera->fov << ",\n";
        out << indent << "  \"NearPlane\": " << camera->nearPlane << ",\n";
        out << indent << "  \"FarPlane\": " << camera->farPlane << ",\n";
        out << indent << "  \"Position\": "; writeVec3(out, camera->position); out << ",\n";
        out << indent << "  \"Yaw\": " << camera->yaw << ",\n";
        out << indent << "  \"Pitch\": " << camera->pitch << ",\n";
        out << indent << "  \"MovementSpeed\": " << camera->movementSpeed << ",\n";
        out << indent << "  \"MouseSensitivity\": " << camera->mouseSensitivity << ",\n";
        out << indent << "  \"FlyControls\": "
            << (camera->flyControlsEnabled ? "true" : "false") << ",\n";
        // aspect is not persisted: it is recomputed from the viewport panel
        // every frame, so a stored value would be wrong on any other layout.
        out << indent << "  \"IsPrimary\": " << (camera->isPrimary ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* mat = registry.try_get<MaterialComponent>(entity)) {
        out << indent << "\"Material\": {\n";
        out << indent << "  \"Unlit\": " << (mat->unlit ? "true" : "false") << ",\n";
        out << indent << "  \"Albedo\": [" << mat->albedoColor.x << ", " << mat->albedoColor.y << ", "
             << mat->albedoColor.z << ", " << mat->albedoColor.w << "],\n";
        writeAssetRef(out, indent, "AlbedoTexture", mat->albedoTexturePath, ",\n");
        writeAssetRef(out, indent, "NormalTexture", mat->normalTexturePath, ",\n");
        writeAssetRef(out, indent, "OrmTexture", mat->ormTexturePath, ",\n");
        // Written only when named, and the 2D block below only when it differs
        // from the default: a scene that never used either saves to the bytes it
        // saved before they existed.
        if (!mat->overlayTexturePath.empty()) {
            writeAssetRef(out, indent, "OverlayTexture", mat->overlayTexturePath, ",\n");
        }
        if (!mat->glossTexturePath.empty()) {
            writeAssetRef(out, indent, "GlossTexture", mat->glossTexturePath, ",\n");
        }
        if (mat->sprite2D != MaterialComponent::Sprite2DLight{}) {
            const MaterialComponent::Sprite2DLight& sprite = mat->sprite2D;
            out << indent << "  \"Sprite2D\": {\n";
            out << indent << "    \"Enabled\": " << (sprite.enabled ? "true" : "false") << ",\n";
            out << indent << "    \"Ambient\": ";
            writeVec3(out, sprite.ambient, "MaterialComponent.sprite2D.ambient");
            out << ",\n";
            out << indent << "    \"Height\": " << jsonSafe(sprite.height, "sprite2D.height") << ",\n";
            out << indent << "    \"LightMask\": " << static_cast<int>(sprite.lightMask) << ",\n";
            out << indent << "    \"NormalYDown\": " << (sprite.normalYDown ? "true" : "false") << ",\n";
            out << indent << "    \"OverlayStrength\": "
                << jsonSafe(sprite.overlayStrength, "sprite2D.overlayStrength");
            // The stand-up and the highlight only when either differs from
            // its default, for the reason the block itself is conditional: a
            // sprite that uses neither saves to the text it saved before them.
            const MaterialComponent::Sprite2DLight defaults{};
            if (sprite.vertical != defaults.vertical || sprite.verticalBaseY != defaults.verticalBaseY) {
                out << ",\n" << indent << "    \"Vertical\": " << (sprite.vertical ? "true" : "false") << ",\n";
                out << indent << "    \"VerticalBaseY\": "
                    << jsonSafe(sprite.verticalBaseY, "sprite2D.verticalBaseY");
            }
            if (sprite.specularStrength != defaults.specularStrength ||
                sprite.specularPower != defaults.specularPower) {
                out << ",\n" << indent << "    \"SpecularStrength\": "
                    << jsonSafe(sprite.specularStrength, "sprite2D.specularStrength") << ",\n";
                out << indent << "    \"SpecularPower\": "
                    << jsonSafe(sprite.specularPower, "sprite2D.specularPower");
            }
            out << "\n";
            out << indent << "  },\n";
        }
        out << indent << "  \"OcclusionStrength\": "
            << jsonSafe(mat->occlusionStrength, "occlusionStrength") << ",\n";
        out << indent << "  \"Roughness\": " << mat->roughness << ",\n";
        out << indent << "  \"Metallic\": " << mat->metallic << ",\n";
        out << indent << "  \"AO\": " << mat->ao << ",\n";
        // The asset link, not just the values it resolves to. A scene that
        // stored only the resolved numbers would silently detach every
        // entity from its shared material the first time it was saved.
        out << indent << "  \"Emissive\": ";
        writeVec3(out, mat->emissiveColor, "MaterialComponent.emissiveColor");
        out << ",\n";
        out << indent << "  \"EmissiveStrength\": " << jsonSafe(mat->emissiveStrength, "emissiveStrength") << ",\n";
        out << indent << "  \"Transparent\": " << (mat->transparent ? "true" : "false") << ",\n";
        out << indent << "  \"Blend\": \""
            << (mat->blend == MaterialComponent::BlendMode::Additive        ? "Additive"
                : mat->blend == MaterialComponent::BlendMode::Premultiplied ? "Premultiplied"
                                                                            : "Alpha")
            << "\",\n";
        out << indent << "  \"AlphaCutoff\": " << jsonSafe(mat->alphaCutoff, "alphaCutoff") << ",\n";
        // The three authored numbers, and not uvSlot - that is renderer scratch
        // and means nothing outside the frame that wrote it.
        out << indent << "  \"UvScale\": [" << jsonSafe(mat->uvScale.x, "uvScale.x")
            << ", " << jsonSafe(mat->uvScale.y, "uvScale.y") << "],\n";
        out << indent << "  \"UvRotation\": " << jsonSafe(mat->uvRotation, "uvRotation") << ",\n";
        out << indent << "  \"UvOffset\": [" << jsonSafe(mat->uvOffset.x, "uvOffset.x")
            << ", " << jsonSafe(mat->uvOffset.y, "uvOffset.y") << "],\n";
        writeAssetRef(out, indent, "Asset", mat->materialPath, "\n");
        out << indent << "},\n";
    }

    if (const auto* surfaces = registry.try_get<SurfaceOverridesComponent>(entity);
        surfaces && !surfaces->overrides.empty()) {
        out << indent << "\"SurfaceOverrides\": [\n";
        for (size_t i = 0; i < surfaces->overrides.size(); ++i) {
            const SurfaceOverride& entry = surfaces->overrides[i];
            out << indent << "  {\n";
            out << indent << "    \"Surface\": \"" << Json::Escape(entry.surface) << "\",\n";
            out << indent << "    \"Albedo\": [" << jsonSafe(entry.albedoColor.x, "override albedo")
                << ", " << jsonSafe(entry.albedoColor.y, "override albedo")
                << ", " << jsonSafe(entry.albedoColor.z, "override albedo")
                << ", " << jsonSafe(entry.albedoColor.w, "override albedo") << "],\n";
            out << indent << "    \"Roughness\": " << jsonSafe(entry.roughness, "override roughness") << ",\n";
            out << indent << "    \"Metallic\": " << jsonSafe(entry.metallic, "override metallic") << ",\n";
            out << indent << "    \"Emissive\": ";
            writeVec3(out, entry.emissiveColor, "SurfaceOverride.emissiveColor");
            out << ",\n";
            out << indent << "    \"EmissiveStrength\": "
                << jsonSafe(entry.emissiveStrength, "override emissiveStrength") << "\n";
            out << indent << "  }" << (i + 1 < surfaces->overrides.size() ? "," : "") << "\n";
        }
        out << indent << "],\n";
    }

    if (const auto* body = registry.try_get<RigidBodyComponent>(entity)) {
        out << indent << "\"RigidBody\": {\n";
        out << indent << "  \"Velocity\": "; writeVec3(out, body->velocity); out << ",\n";
        out << indent << "  \"Mass\": " << body->mass << ",\n";
        out << indent << "  \"UseGravity\": " << (body->useGravity ? "true" : "false") << ",\n";
        out << indent << "  \"Restitution\": " << body->restitution << ",\n";
        out << indent << "  \"Friction\": " << body->friction << ",\n";
        out << indent << "  \"LinearDamping\": " << body->linearDamping << ",\n";
        out << indent << "  \"AngularVelocity\": "; writeVec3(out, body->angularVelocity); out << ",\n";
        out << indent << "  \"AngularDamping\": " << body->angularDamping << ",\n";
        out << indent << "  \"FreezeRotation\": " << (body->freezeRotation ? "true" : "false") << ",\n";
        out << indent << "  \"LockPosition\": "; writeBool3(out, body->lockPosition); out << ",\n";
        out << indent << "  \"LockRotation\": "; writeBool3(out, body->lockRotation); out << ",\n";
        // Only the authored half. isSleeping, the timer and the position it
        // slept at are re-derived within half a second of the scene loading,
        // and writing them would let a scene be saved with a body asleep in
        // mid-air - which would then never fall.
        out << indent << "  \"AllowSleep\": " << (body->allowSleep ? "true" : "false") << ",\n";
        out << indent << "  \"IsKinematic\": " << (body->isKinematic ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* surface = registry.try_get<PhysicsMaterialComponent>(entity)) {
        out << indent << "\"PhysicsMaterial\": { \"Friction\": " << surface->friction
            << ", \"Restitution\": " << surface->restitution << " },\n";
    }

    if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
        out << indent << "\"BoxCollider\": { \"Size\": ";
        writeVec3(out, box->size);
        out << ", \"Center\": ";
        writeVec3(out, box->center, "BoxCollider.center");
        out << ", \"IsTrigger\": " << (box->isTrigger ? "true" : "false")
            << ", \"Layer\": " << box->layer
            << ", \"CollidesWith\": " << box->collidesWith << " },\n";
    }

    // Neither of these was written at all. A sphere collider therefore
    // vanished on Play, on Stop, on undo and on save - silently, because a
    // missing collider looks exactly like a body that was never given one.
    if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
        out << indent << "\"SphereCollider\": { \"Radius\": " << sphere->radius
             << ", \"Center\": ";
        writeVec3(out, sphere->center, "SphereCollider.center");
        out << ", \"IsTrigger\": " << (sphere->isTrigger ? "true" : "false")
            << ", \"Layer\": " << sphere->layer
            << ", \"CollidesWith\": " << sphere->collidesWith << " },\n";
    }

    if (const auto* capsule = registry.try_get<CapsuleColliderComponent>(entity)) {
        out << indent << "\"CapsuleCollider\": { \"Radius\": " << capsule->radius
             << ", \"Height\": " << capsule->height
             << ", \"Center\": ";
        writeVec3(out, capsule->center, "CapsuleCollider.center");
        out << ", \"IsTrigger\": " << (capsule->isTrigger ? "true" : "false")
            << ", \"Layer\": " << capsule->layer
            << ", \"CollidesWith\": " << capsule->collidesWith << " },\n";
    }

    if (const auto* joint = registry.try_get<JointComponent>(entity)) {
        // connectedBody is NOT here. An entt handle carries a version and is
        // recycled, so persisting one reattaches to whatever occupies that slot
        // next time - which is why SceneSerializer writes parent links as an
        // index into its own entity array, and why it writes this one too.
        //
        // The consequence, and it is the right one: a PREFAB carrying a joint
        // keeps its shape and loses its other end, because the entity it was
        // pointing at is not part of the prefab.
        out << indent << "\"Joint\": { \"Type\": " << static_cast<uint32_t>(joint->type)
            << ", \"Anchor\": ";
        writeVec3(out, joint->anchor, "Joint.anchor");
        out << ", \"ConnectedAnchor\": ";
        writeVec3(out, joint->connectedAnchor, "Joint.connectedAnchor");
        out << ", \"Axis\": ";
        writeVec3(out, joint->axis, "Joint.axis");
        out << ", \"ConnectedAxis\": ";
        writeVec3(out, joint->connectedAxis, "Joint.connectedAxis");
        out << ", \"Distance\": " << joint->distance
            << ", \"Rope\": " << (joint->rope ? "true" : "false")
            << ", \"UseLimit\": " << (joint->useLimit ? "true" : "false")
            << ", \"MinAngle\": " << jsonSafe(joint->minAngle, "Joint.minAngle")
            << ", \"MaxAngle\": " << jsonSafe(joint->maxAngle, "Joint.maxAngle")
            << ", \"UseSpring\": " << (joint->useSpring ? "true" : "false")
            << ", \"SpringFrequency\": "
            << jsonSafe(joint->springFrequency, "Joint.springFrequency")
            << ", \"SpringDamping\": " << jsonSafe(joint->springDamping, "Joint.springDamping")
            << ", \"SpringRestAngle\": "
            << jsonSafe(joint->springRestAngle, "Joint.springRestAngle")
            << ", \"UseMotor\": " << (joint->useMotor ? "true" : "false")
            << ", \"MotorSpeed\": " << jsonSafe(joint->motorSpeed, "Joint.motorSpeed")
            << ", \"MaxMotorTorque\": "
            << jsonSafe(joint->maxMotorTorque, "Joint.maxMotorTorque")
            << ", \"BreakForce\": " << jsonSafe(joint->breakForce, "Joint.breakForce")
            << ", \"BreakTorque\": " << jsonSafe(joint->breakTorque, "Joint.breakTorque")
            << ", \"SolveOrder\": " << joint->solveOrder
            << ", \"Stiffness\": " << joint->stiffness
            << ", \"Enabled\": " << (joint->enabled ? "true" : "false") << " },\n";
        // `broken` is deliberately not written. A scene that reloaded with its
        // joints already snapped would be a level you could only play once.
    }

    if (const auto* hull = registry.try_get<ConvexHullColliderComponent>(entity)) {
        // The source is an ASSET reference like any other, so it carries its
        // identity beside its path - see AssetDatabase. A collider whose source
        // was renamed would otherwise fall back to a shape it can no longer
        // find, and a rock you can walk through is not an obvious symptom of a
        // rename.
        out << indent << "\"ConvexHullCollider\": {\n";
        writeAssetRef(out, indent, "Source", hull->sourcePath, ",\n");
        out << indent << "  \"Primitive\": \"" << Json::Escape(hull->sourcePrimitive) << "\",\n"
            << indent << "  \"IsTrigger\": " << (hull->isTrigger ? "true" : "false") << ",\n"
            << indent << "  \"Layer\": " << hull->layer << ",\n"
            << indent << "  \"CollidesWith\": " << hull->collidesWith << "\n"
            << indent << "},\n";
    }

    if (const auto* terrain = registry.try_get<HeightfieldColliderComponent>(entity)) {
        out << indent << "\"HeightfieldCollider\": { \"Width\": " << terrain->width
            << ", \"Depth\": " << terrain->depth
            << ", \"HeightScale\": " << terrain->heightScale
            << ", \"Thickness\": " << terrain->thickness
            << ", \"IsTrigger\": " << (terrain->isTrigger ? "true" : "false")
            << ", \"Layer\": " << terrain->layer
            << ", \"CollidesWith\": " << terrain->collidesWith << " },\n";
    }

    if (const auto* listener = registry.try_get<AudioListenerComponent>(entity)) {
        out << indent << "\"AudioListener\": { \"IsPrimary\": "
             << (listener->isPrimary ? "true" : "false") << " },\n";
    }

    if (const auto* audio = registry.try_get<AudioSourceComponent>(entity)) {
        // voice/failedToLoad are runtime state owned by AudioSystem and are
        // deliberately not persisted.
        out << indent << "\"AudioSource\": {\n";
        writeAssetRef(out, indent, "Clip", audio->soundFile, ",\n");
        out << indent << "  \"Volume\": " << audio->volume << ",\n";
        out << indent << "  \"Pitch\": " << audio->pitch << ",\n";
        out << indent << "  \"Playing\": " << (audio->isPlaying ? "true" : "false") << ",\n";
        out << indent << "  \"Loop\": " << (audio->loop ? "true" : "false") << ",\n";
        out << indent << "  \"ReferenceDistance\": " << audio->referenceDistance << ",\n";
        out << indent << "  \"MaxDistance\": " << audio->maxDistance << "\n";
        out << indent << "},\n";
    }

    if (const auto* probe = registry.try_get<ReflectionProbeComponent>(entity)) {
        // resolvedSlot is runtime state owned by the renderer - which
        // descriptor this probe happened to be given - and is not persisted.
        out << indent << "\"ReflectionProbe\": {\n";
        out << indent << "  \"HalfExtent\": [" << probe->halfExtent.x << ", "
            << probe->halfExtent.y << ", " << probe->halfExtent.z << "],\n";
        out << indent << "  \"Hdri\": \"" << Json::Escape(probe->hdriPath) << "\",\n";
        out << indent << "  \"Intensity\": " << probe->intensity << "\n";
        out << indent << "},\n";
    }

    if (const auto* text = registry.try_get<UITextComponent>(entity)) {
        out << indent << "\"UIText\": {\n";
        out << indent << "  \"Text\": \"" << Json::Escape(text->text) << "\",\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(text->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << text->offset.x << ", " << text->offset.y << "],\n";
        out << indent << "  \"FontSize\": " << text->fontSize << ",\n";
        out << indent << "  \"Color\": [" << text->color.x << ", " << text->color.y << ", "
             << text->color.z << ", " << text->color.w << "],\n";
        out << indent << "  \"WrapWidth\": " << text->wrapWidth << ",\n";
        out << indent << "  \"Shadow\": " << (text->shadow ? "true" : "false") << ",\n";
        out << indent << "  \"Visible\": " << (text->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* button = registry.try_get<UIButtonComponent>(entity)) {
        // hovered/pressed/clicked are rebuilt from the pointer every frame and
        // are deliberately absent: a button saved mid-press would come back
        // stuck, and a restored click would fire an action nobody asked for.
        //
        // So are clickPending and clickedThisTick, and more so. Those hold a
        // click until a tick takes it, so a snapshot carrying one would fire it
        // on the first tick after the snapshot was restored - which is
        // precisely a Play-mode Stop and Start, so the button pressed just
        // before Stop would press itself again on the next Play.
        out << indent << "\"UIButton\": {\n";
        out << indent << "  \"Label\": \"" << Json::Escape(button->label) << "\",\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(button->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << button->offset.x << ", " << button->offset.y << "],\n";
        out << indent << "  \"Size\": [" << button->size.x << ", " << button->size.y << "],\n";
        out << indent << "  \"FontSize\": " << button->fontSize << ",\n";
        out << indent << "  \"CornerRadius\": " << button->cornerRadius << ",\n";
        out << indent << "  \"Color\": [" << button->color.x << ", " << button->color.y << ", "
             << button->color.z << ", " << button->color.w << "],\n";
        out << indent << "  \"HoverColor\": [" << button->hoverColor.x << ", " << button->hoverColor.y << ", "
             << button->hoverColor.z << ", " << button->hoverColor.w << "],\n";
        out << indent << "  \"PressColor\": [" << button->pressColor.x << ", " << button->pressColor.y << ", "
             << button->pressColor.z << ", " << button->pressColor.w << "],\n";
        out << indent << "  \"DisabledColor\": [" << button->disabledColor.x << ", " << button->disabledColor.y << ", "
             << button->disabledColor.z << ", " << button->disabledColor.w << "],\n";
        out << indent << "  \"TextColor\": [" << button->textColor.x << ", " << button->textColor.y << ", "
             << button->textColor.z << ", " << button->textColor.w << "],\n";
        out << indent << "  \"Enabled\": " << (button->enabled ? "true" : "false") << ",\n";
        out << indent << "  \"Visible\": " << (button->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* field = registry.try_get<UITextFieldComponent>(entity)) {
        // focused, submitted and the caret are absent for the same reason the
        // button's press flags are: a field saved mid-edit would come back
        // focused with a caret in the middle of a name nobody is typing, and a
        // restored `submitted` would answer a question never asked.
        //
        // The TEXT is kept, because it is the answer - a scene that authored a
        // default in the box should come back with it.
        out << indent << "\"UITextField\": {\n";
        out << indent << "  \"Text\": \"" << Json::Escape(field->text) << "\",\n";
        out << indent << "  \"Placeholder\": \"" << Json::Escape(field->placeholder) << "\",\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(field->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << field->offset.x << ", " << field->offset.y << "],\n";
        out << indent << "  \"Size\": [" << field->size.x << ", " << field->size.y << "],\n";
        out << indent << "  \"FontSize\": " << field->fontSize << ",\n";
        out << indent << "  \"CornerRadius\": " << field->cornerRadius << ",\n";
        out << indent << "  \"MaxLength\": " << field->maxLength << ",\n";
        out << indent << "  \"Color\": [" << field->color.x << ", " << field->color.y << ", "
             << field->color.z << ", " << field->color.w << "],\n";
        out << indent << "  \"FocusColor\": [" << field->focusColor.x << ", " << field->focusColor.y << ", "
             << field->focusColor.z << ", " << field->focusColor.w << "],\n";
        out << indent << "  \"BorderColor\": [" << field->borderColor.x << ", " << field->borderColor.y << ", "
             << field->borderColor.z << ", " << field->borderColor.w << "],\n";
        out << indent << "  \"FocusBorderColor\": [" << field->focusBorderColor.x << ", "
             << field->focusBorderColor.y << ", " << field->focusBorderColor.z << ", "
             << field->focusBorderColor.w << "],\n";
        out << indent << "  \"TextColor\": [" << field->textColor.x << ", " << field->textColor.y << ", "
             << field->textColor.z << ", " << field->textColor.w << "],\n";
        out << indent << "  \"PlaceholderColor\": [" << field->placeholderColor.x << ", "
             << field->placeholderColor.y << ", " << field->placeholderColor.z << ", "
             << field->placeholderColor.w << "],\n";
        out << indent << "  \"Enabled\": " << (field->enabled ? "true" : "false") << ",\n";
        out << indent << "  \"Visible\": " << (field->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* panel = registry.try_get<UIPanelComponent>(entity)) {
        out << indent << "\"UIPanel\": {\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(panel->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << panel->offset.x << ", " << panel->offset.y << "],\n";
        out << indent << "  \"Size\": [" << panel->size.x << ", " << panel->size.y << "],\n";
        out << indent << "  \"Color\": [" << panel->color.x << ", " << panel->color.y << ", "
             << panel->color.z << ", " << panel->color.w << "],\n";
        out << indent << "  \"CornerRadius\": " << panel->cornerRadius << ",\n";
        out << indent << "  \"Fill\": " << panel->fill << ",\n";
        out << indent << "  \"DrawTrack\": " << (panel->drawTrack ? "true" : "false") << ",\n";
        out << indent << "  \"TrackColor\": [" << panel->trackColor.x << ", " << panel->trackColor.y << ", "
             << panel->trackColor.z << ", " << panel->trackColor.w << "],\n";
        out << indent << "  \"FillWidth\": " << (panel->fillWidth ? "true" : "false") << ",\n";
        out << indent << "  \"FillHeight\": " << (panel->fillHeight ? "true" : "false") << ",\n";
        out << indent << "  \"ClipsChildren\": "
             << (panel->clipsChildren ? "true" : "false") << ",\n";
        out << indent << "  \"Visible\": " << (panel->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* shape = registry.try_get<UIShapeComponent>(entity)) {
        out << indent << "\"UIShape\": {\n";
        out << indent << "  \"Kind\": " << static_cast<int>(shape->kind) << ",\n";
        out << indent << "  \"WorldSpace\": " << (shape->worldSpace ? "true" : "false") << ",\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(shape->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << shape->offset.x << ", " << shape->offset.y << "],\n";
        out << indent << "  \"Radius\": " << shape->radius << ",\n";
        out << indent << "  \"Endpoint\": [" << shape->endpoint.x << ", " << shape->endpoint.y
             << ", " << shape->endpoint.z << "],\n";
        out << indent << "  \"Thickness\": " << shape->thickness << ",\n";
        out << indent << "  \"Segments\": " << shape->segments << ",\n";
        out << indent << "  \"Color\": [" << shape->color.x << ", " << shape->color.y << ", "
             << shape->color.z << ", " << shape->color.w << "],\n";
        out << indent << "  \"Visible\": " << (shape->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* stack = registry.try_get<UIStackComponent>(entity)) {
        out << indent << "\"UIStack\": {\n";
        out << indent << "  \"Horizontal\": " << (stack->horizontal ? "true" : "false") << ",\n";
        out << indent << "  \"Anchor\": " << static_cast<int>(stack->anchor) << ",\n";
        out << indent << "  \"Offset\": [" << stack->offset.x << ", " << stack->offset.y << "],\n";
        out << indent << "  \"Spacing\": " << stack->spacing << ",\n";
        out << indent << "  \"Visible\": " << (stack->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    // Written whenever it is present, including at zero. Zero is the default,
    // but an author who set it back to zero on purpose meant that, and dropping
    // it would silently re-rank the element behind anything that kept its own.
    if (const auto* ordering = registry.try_get<UIOrderComponent>(entity)) {
        out << indent << "\"UIOrder\": {\n";
        out << indent << "  \"Order\": " << ordering->order << ",\n";
        out << indent << "  \"Layer\": " << ordering->layer << "\n";
        out << indent << "},\n";
    }

    if (const auto* script = registry.try_get<ScriptComponent>(entity)) {
        out << indent << "\"Script\": {\n";
        out << indent << "  \"Name\": \"" << Json::Escape(script->scriptName) << "\",\n";
        out << indent << "  \"Enabled\": " << (script->isEnabled ? "true" : "false") << ",\n";
        // Parameters are authored data and are saved. state is NOT: it is
        // where a script has got to, not what it was set up as, and writing it
        // would make a scene depend on how long the game had been running.
        out << indent << "  \"Parameters\": {";
        for (size_t i = 0; i < script->parameters.size(); ++i) {
            if (i > 0) out << ",";
            out << "\"" << Json::Escape(script->parameters[i].first)
                << "\": " << script->parameters[i].second;
        }
        out << "}\n";
        out << indent << "},\n";
    }

    if (const auto* emitter = registry.try_get<ParticleEmitterComponent>(entity)) {
        // Previously written as a bare `true`, so every emitter setting the
        // user had authored was reset by the next load, Play, Stop or undo -
        // and because the text never changed when those fields were edited,
        // the edit was not undoable in the first place.
        //
        // particles/emitAccumulator are runtime state and stay unpersisted,
        // like AudioSourceComponent::voice.
        out << indent << "\"ParticleEmitter\": {\n";
        out << indent << "  \"MaxParticles\": " << emitter->maxParticles << ",\n";
        out << indent << "  \"EmitRate\": " << emitter->emitRate << ",\n";
        out << indent << "  \"ParticleLifetime\": " << emitter->particleLifetime << ",\n";
        out << indent << "  \"StartColor\": [" << emitter->startColor.x << ", " << emitter->startColor.y << ", "
             << emitter->startColor.z << ", " << emitter->startColor.w << "],\n";
        out << indent << "  \"EndColor\": [" << emitter->endColor.x << ", " << emitter->endColor.y << ", "
             << emitter->endColor.z << ", " << emitter->endColor.w << "],\n";
        out << indent << "  \"VelocityRange\": "; writeVec3(out, emitter->velocityRange); out << ",\n";
        out << indent << "  \"ParticleSize\": " << emitter->particleSize << "\n";
        out << indent << "},\n";
    }

    if (const auto* sprite = registry.try_get<SpriteAnimationComponent>(entity)) {
        // The frame and the accumulator ARE persisted, unlike the particle pool
        // above and for the reason AnimatorComponent::time is: where a flipbook
        // has got to is part of the scene, so stopping and starting play mode
        // does not snap every animation in the level back to its first cell.
        // The pool is not, because it is a cache of a thousand particles and
        // reproducing it exactly is not what anybody means by saving a scene.
        out << indent << "\"SpriteAnimation\": {\n";
        out << indent << "  \"Columns\": " << sprite->columns << ",\n";
        out << indent << "  \"Rows\": " << sprite->rows << ",\n";
        out << indent << "  \"FirstFrame\": " << sprite->firstFrame << ",\n";
        out << indent << "  \"FrameCount\": " << sprite->frameCount << ",\n";
        out << indent << "  \"FramesPerSecond\": " << sprite->framesPerSecond << ",\n";
        out << indent << "  \"Loop\": " << (sprite->loop ? "true" : "false") << ",\n";
        out << indent << "  \"Playing\": " << (sprite->playing ? "true" : "false") << ",\n";
        out << indent << "  \"Frame\": " << sprite->frame << ",\n";
        out << indent << "  \"Elapsed\": " << sprite->elapsed << "\n";
        out << indent << "},\n";
    }

    if (const auto* tilemap = registry.try_get<TilemapComponent>(entity)) {
        // The cells are written ONE MAP ROW PER LINE, so the scene file shows
        // the map the way the screen does and a diff of one changed tile is
        // one changed line. Every cell the map describes is written, empties
        // included: a reader that was handed fewer cells than the map has
        // pads with empties, so leaving them out would round-trip - but a
        // file where the shape of the array is the shape of the map is one a
        // person can read.
        //
        // The scratch fields - the baked hash, the mesh id - are not written,
        // for the reason RenderableComponent's ids are not: they are answers
        // about THIS process's registries and mean nothing in another.
        out << indent << "\"Tilemap\": {\n";
        out << indent << "  \"AtlasColumns\": " << tilemap->atlasColumns << ",\n";
        out << indent << "  \"AtlasRows\": " << tilemap->atlasRows << ",\n";
        out << indent << "  \"Width\": " << tilemap->width << ",\n";
        out << indent << "  \"Height\": " << tilemap->height << ",\n";
        out << indent << "  \"Cells\": [";
        for (uint32_t row = 0; row < tilemap->height; ++row) {
            out << "\n" << indent << "    ";
            for (uint32_t column = 0; column < tilemap->width; ++column) {
                out << tilemap->At(column, row);
                if (row + 1 < tilemap->height || column + 1 < tilemap->width) out << ", ";
            }
        }
        if (tilemap->cellCount() > 0) out << "\n" << indent << "  ";
        out << "]\n";
        out << indent << "},\n";
    }

    if (const auto* animator = registry.try_get<AnimatorComponent>(entity)) {
        // SkinnedMeshComponent is deliberately NOT persisted: it is entirely
        // derived from the mesh path and rebuilt every frame, so persisting
        // it could only ever let it go stale. Time IS persisted, unlike
        // ScriptComponent::elapsed, so Play/Stop and undo restore the pose
        // that was on screen.
        out << indent << "\"Animator\": {\n";
        out << indent << "  \"Clip\": \"" << Json::Escape(animator->clipName) << "\",\n";
        out << indent << "  \"Time\": " << animator->time << ",\n";
        out << indent << "  \"Speed\": " << animator->speed << ",\n";
        out << indent << "  \"Loop\": " << (animator->loop ? "true" : "false") << ",\n";
        out << indent << "  \"BlendDuration\": " << animator->blendDuration << ",\n";
        out << indent << "  \"Playing\": " << (animator->playing ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* renderable = registry.try_get<RenderableComponent>(entity)) {
        out << indent << "\"Renderable\": { \"Visible\": " << (renderable->isVisible ? "true" : "false")
             << ", \"CastsShadow\": " << (renderable->castsShadow ? "true" : "false")
             << ", \"SortKey\": " << renderable->sortKey << " },\n";
    }

    // A game's own components, under one member of their own. Written before
    // HasRenderable so the engine's last member keeps its no-trailing-comma
    // contract untouched.
    {
        std::ostringstream members;
        bool any = false;
        for (const auto& extension : registeredComponents()) {
            std::ostringstream value;
            if (!extension.writer(registry, entity, value)) continue;
            if (any) members << ",\n";
            members << indent << "  \"" << Json::Escape(extension.key)
                    << "\": " << value.str();
            any = true;
        }
        if (any) {
            out << indent << "\"Game\": {\n" << members.str()
                << "\n" << indent << "},\n";
        }
    }

    out << indent << "\"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false") << "\n";
}

void Read(entt::registry& registry, entt::entity entity, const Json::Value& node) {
    // A game's own components first, so anything the engine writes afterwards
    // takes precedence on a key a game somehow shares - it cannot, being
    // namespaced, but the ordering costs nothing and the invariant is worth
    // being true by construction rather than by argument.
    if (node.Has("Game")) {
        const Json::Value& game = node["Game"];
        for (const auto& extension : registeredComponents()) {
            if (game.Has(extension.key)) {
                extension.reader(registry, entity, game[extension.key]);
            }
        }
    }

    if (node.Has("Tag")) {
        registry.emplace_or_replace<TagComponent>(entity, node["Tag"].AsString("Entity"));
    }

    if (node.Has("Transform")) {
        const auto& t = node["Transform"];
        auto& transform = registry.emplace_or_replace<TransformComponent>(entity);
        transform.position = readVec3(t["Position"], glm::vec3(0.0f));
        transform.rotation = readVec3(t["Rotation"], glm::vec3(0.0f));
        transform.scale = readVec3(t["Scale"], glm::vec3(1.0f));
    }

    if (node.Has("Mesh")) {
        const auto& m = node["Mesh"];
        registry.emplace_or_replace<MeshComponent>(entity,
            m["Primitive"].AsString("Cube"), readAssetRef(m, "Path"), 0u, 0u);
    }

    if (node.Has("Light")) {
        const auto& l = node["Light"];
        auto& light = registry.emplace_or_replace<LightComponent>(entity);
        light.type = static_cast<int>(l["Type"].AsNumber(0.0));
        light.direction = readVec3(l["Direction"], glm::vec3(0.6f, 1.0f, 0.5f));
        light.color = readVec3(l["Color"], glm::vec3(1.0f));
        light.ambient = readVec3(l["Ambient"], glm::vec3(0.12f));
        light.ambientGround = readVec3(l["AmbientGround"], glm::vec3(0.10f, 0.09f, 0.08f));
        light.intensity = l["Intensity"].AsFloat(1.5f);
        light.range = l["Range"].AsFloat(25.0f);
        light.innerAngle = l["InnerAngle"].AsFloat(0.35f);
        light.outerAngle = l["OuterAngle"].AsFloat(0.52f);
        light.castsShadow = l["CastsShadow"].AsBool(true);
    }

    // Each field falls back to the component's own default, so a block missing
    // a key reads as the light it would have been.
    if (node.Has("Light2D")) {
        const auto& l = node["Light2D"];
        const Light2DComponent defaults{};
        auto& light = registry.emplace_or_replace<Light2DComponent>(entity);
        light.color = readVec3(l["Color"], defaults.color);
        light.intensity = l["Intensity"].AsFloat(defaults.intensity);
        light.range = l["Range"].AsFloat(defaults.range);
        light.height = l["Height"].AsFloat(defaults.height);
        light.layers = static_cast<uint8_t>(
            std::clamp(l["Layers"].AsNumber(defaults.layers), 0.0, 255.0));
        light.enabled = l["Enabled"].AsBool(defaults.enabled);
    }

    if (node.Has("Camera")) {
        const auto& c = node["Camera"];
        auto& camera = registry.emplace_or_replace<CameraComponent>(entity);
        // Defaults to perspective, so every scene written before orthographic
        // existed reads back exactly as it did.
        camera.projection = c["Orthographic"].AsBool(false)
                                ? CameraComponent::Projection::Orthographic
                                : CameraComponent::Projection::Perspective;
        camera.orthoHeight = c["OrthoHeight"].AsFloat(10.0f);
        camera.fov = c["FOV"].AsFloat(45.0f);
        camera.nearPlane = c["NearPlane"].AsFloat(0.1f);
        camera.farPlane = c["FarPlane"].AsFloat(100.0f);
        camera.position = readVec3(c["Position"], glm::vec3(0.0f, 1.2f, 4.0f));
        camera.yaw = c["Yaw"].AsFloat(-90.0f);
        camera.pitch = c["Pitch"].AsFloat(-10.0f);
        camera.movementSpeed = c["MovementSpeed"].AsFloat(3.5f);
        camera.mouseSensitivity = c["MouseSensitivity"].AsFloat(0.1f);
        // Defaults true, matching the component, so a scene authored before
        // the flag existed still yields a usable camera.
        camera.isPrimary = c["IsPrimary"].AsBool(true);

        // Also true, and the default is the whole compatibility story: this
        // component is emplace_or_replace'd above, so the struct's own default
        // is overwritten by whatever the reader writes. AsBool(false) here would
        // turn every camera in every existing scene to stone with no error
        // message anywhere.
        camera.flyControlsEnabled = c["FlyControls"].AsBool(true);
        camera.updateCameraVectors();
    }

    if (node.Has("Material")) {
        const auto& m = node["Material"];
        auto& material = registry.emplace_or_replace<MaterialComponent>(entity);
        material.albedoColor = readVec4(m["Albedo"], glm::vec4(1.0f));
        // Defaults to false, so every scene written before this reads back lit.
        material.unlit = m["Unlit"].AsBool(false);
        material.albedoTexturePath = readAssetRef(m, "AlbedoTexture");
        material.normalTexturePath = readAssetRef(m, "NormalTexture");
        // Absent from every scene written before packed maps existed, which
        // reads as no map and so as exactly the surface it was.
        material.ormTexturePath = readAssetRef(m, "OrmTexture");
        // Absent from every scene written before overlays, and from every one
        // that names none since: no overlay, which adds nothing.
        material.overlayTexturePath = readAssetRef(m, "OverlayTexture");
        // The same for the gloss map: absent is none, a uniform gloss of one.
        material.glossTexturePath = readAssetRef(m, "GlossTexture");
        // Absent likewise, and each field on its own falls back to the default,
        // so a block missing a key reads as the sprite it would have been.
        if (m.Has("Sprite2D")) {
            const auto& sprite = m["Sprite2D"];
            const MaterialComponent::Sprite2DLight defaults{};
            material.sprite2D.enabled = sprite["Enabled"].AsBool(defaults.enabled);
            material.sprite2D.ambient = readVec3(sprite["Ambient"], defaults.ambient);
            material.sprite2D.height = sprite["Height"].AsFloat(defaults.height);
            material.sprite2D.lightMask = static_cast<uint8_t>(
                std::clamp(sprite["LightMask"].AsNumber(defaults.lightMask), 0.0, 255.0));
            material.sprite2D.normalYDown = sprite["NormalYDown"].AsBool(defaults.normalYDown);
            material.sprite2D.overlayStrength = sprite["OverlayStrength"].AsFloat(defaults.overlayStrength);
            material.sprite2D.vertical = sprite["Vertical"].AsBool(defaults.vertical);
            material.sprite2D.verticalBaseY = sprite["VerticalBaseY"].AsFloat(defaults.verticalBaseY);
            material.sprite2D.specularStrength = sprite["SpecularStrength"].AsFloat(defaults.specularStrength);
            material.sprite2D.specularPower = sprite["SpecularPower"].AsFloat(defaults.specularPower);
        }
        material.occlusionStrength = m["OcclusionStrength"].AsFloat(1.0f);
        material.roughness = m["Roughness"].AsFloat(0.4f);
        material.metallic = m["Metallic"].AsFloat(0.1f);
        material.ao = m["AO"].AsFloat(1.0f);
        material.materialPath = readAssetRef(m, "Asset");
        // Absent in every scene written before the blended pass existed, and
        // false is what those scenes rendered as.
        material.transparent = m["Transparent"].AsBool(false);
        // Absent in every scene written before a surface could be added, and
        // mixing is what those scenes blended with.
        // Any other word reads as Alpha too, as an unknown Background reads as
        // the sky: a scene naming a blend this build does not know draws mixing.
        const std::string blendWord = m["Blend"].AsString("Alpha");
        material.blend = blendWord == "Additive"        ? MaterialComponent::BlendMode::Additive
                       : blendWord == "Premultiplied" ? MaterialComponent::BlendMode::Premultiplied
                                                      : MaterialComponent::BlendMode::Alpha;
        // Absent means zero means no cutout, so a scene written before this
        // existed loads as the opaque material it was.
        material.alphaCutoff = m["AlphaCutoff"].AsFloat(0.0f);
        material.emissiveColor = readVec3(m["Emissive"], glm::vec3(0.0f));
        material.emissiveStrength = m["EmissiveStrength"].AsFloat(0.0f);
        // Absent from every scene written before texture coordinates could be
        // transformed, and the fallbacks here are the identity - so those
        // scenes load as exactly the surfaces they were.
        material.uvScale = readVec2(m["UvScale"], glm::vec2(1.0f, 1.0f));
        material.uvRotation = m["UvRotation"].AsFloat(0.0f);
        material.uvOffset = readVec2(m["UvOffset"], glm::vec2(0.0f, 0.0f));
    }

    // Absent from every scene written before a surface could be re-materialised,
    // and an empty list draws exactly what the file says - so those scenes load
    // as the models they already were.
    if (node.Has("SurfaceOverrides")) {
        auto& surfaces = registry.emplace_or_replace<SurfaceOverridesComponent>(entity);
        for (const auto& item : node["SurfaceOverrides"].AsArray()) {
            SurfaceOverride entry;
            entry.surface = item["Surface"].AsString("");
            // A nameless override can never match a surface, so it is dropped
            // here rather than sitting in the list being scanned every frame.
            if (entry.surface.empty()) continue;
            entry.albedoColor = readVec4(item["Albedo"], glm::vec4(1.0f));
            entry.roughness = item["Roughness"].AsFloat(0.5f);
            entry.metallic = item["Metallic"].AsFloat(0.0f);
            entry.emissiveColor = readVec3(item["Emissive"], glm::vec3(0.0f));
            entry.emissiveStrength = item["EmissiveStrength"].AsFloat(0.0f);
            surfaces.overrides.push_back(std::move(entry));
        }
    }

    if (node.Has("RigidBody")) {
        const auto& r = node["RigidBody"];
        auto& body = registry.emplace_or_replace<RigidBodyComponent>(entity);
        body.velocity = readVec3(r["Velocity"], glm::vec3(0.0f));
        body.mass = r["Mass"].AsFloat(1.0f);
        body.useGravity = r["UseGravity"].AsBool(true);
        body.isKinematic = r["IsKinematic"].AsBool(false);
        body.restitution = r["Restitution"].AsFloat(0.3f);
        body.friction = r["Friction"].AsFloat(0.4f);
        body.linearDamping = r["LinearDamping"].AsFloat(0.0f);
        body.angularVelocity = readVec3(r["AngularVelocity"], glm::vec3(0.0f));
        body.angularDamping = r["AngularDamping"].AsFloat(0.05f);
        body.freezeRotation = r["FreezeRotation"].AsBool(false);
        body.lockPosition = readBool3(r["LockPosition"]);
        body.lockRotation = readBool3(r["LockRotation"]);
        // A scene written before sleeping existed has no key and must keep
        // being allowed to sleep, which is the default for a new body too.
        body.allowSleep = r["AllowSleep"].AsBool(true);
    }

    if (node.Has("PhysicsMaterial")) {
        const auto& m = node["PhysicsMaterial"];
        auto& surface = registry.emplace_or_replace<PhysicsMaterialComponent>(entity);
        surface.friction = m["Friction"].AsFloat(0.4f);
        surface.restitution = m["Restitution"].AsFloat(0.3f);
    }

    if (node.Has("BoxCollider")) {
        auto& box = registry.emplace_or_replace<BoxColliderComponent>(entity);
        box.size = readVec3(node["BoxCollider"]["Size"], glm::vec3(1.0f));
        box.center = readVec3(node["BoxCollider"]["Center"], glm::vec3(0.0f));
        box.isTrigger = node["BoxCollider"]["IsTrigger"].AsBool(false);
        // The defaults matter: a scene written before layers existed has
        // neither key, and must keep colliding with everything.
        box.layer = static_cast<uint32_t>(
            node["BoxCollider"]["Layer"].AsNumber(CollisionLayer::kDefault));
        box.collidesWith = static_cast<uint32_t>(
            node["BoxCollider"]["CollidesWith"].AsNumber(CollisionLayer::kAll));
    }

    if (node.Has("SphereCollider")) {
        auto& sphere = registry.emplace_or_replace<SphereColliderComponent>(entity);
        sphere.radius = node["SphereCollider"]["Radius"].AsFloat(0.5f);
        sphere.center = readVec3(node["SphereCollider"]["Center"], glm::vec3(0.0f));
        sphere.isTrigger = node["SphereCollider"]["IsTrigger"].AsBool(false);
        sphere.layer = static_cast<uint32_t>(
            node["SphereCollider"]["Layer"].AsNumber(CollisionLayer::kDefault));
        sphere.collidesWith = static_cast<uint32_t>(
            node["SphereCollider"]["CollidesWith"].AsNumber(CollisionLayer::kAll));
    }

    if (node.Has("CapsuleCollider")) {
        auto& capsule = registry.emplace_or_replace<CapsuleColliderComponent>(entity);
        capsule.radius = node["CapsuleCollider"]["Radius"].AsFloat(0.5f);
        capsule.height = node["CapsuleCollider"]["Height"].AsFloat(2.0f);
        capsule.center = readVec3(node["CapsuleCollider"]["Center"], glm::vec3(0.0f));
        capsule.isTrigger = node["CapsuleCollider"]["IsTrigger"].AsBool(false);
        capsule.layer = static_cast<uint32_t>(
            node["CapsuleCollider"]["Layer"].AsNumber(CollisionLayer::kDefault));
        capsule.collidesWith = static_cast<uint32_t>(
            node["CapsuleCollider"]["CollidesWith"].AsNumber(CollisionLayer::kAll));
    }

    if (node.Has("Joint")) {
        auto& joint = registry.emplace_or_replace<JointComponent>(entity);
        const auto type = static_cast<uint32_t>(node["Joint"]["Type"].AsNumber(0.0));
        // Clamped rather than cast blindly: a scene from a later build naming a
        // joint type this one has never heard of should be a point joint, not a
        // switch that falls through to whatever the enum happens to hold.
        joint.type = (type == 1) ? JointComponent::Type::Distance
                   : (type == 2) ? JointComponent::Type::Hinge
                   : (type == 3) ? JointComponent::Type::Weld
                                 : JointComponent::Type::Point;
        joint.anchor = readVec3(node["Joint"]["Anchor"], glm::vec3(0.0f));
        joint.connectedAnchor = readVec3(node["Joint"]["ConnectedAnchor"], glm::vec3(0.0f));
        joint.axis = readVec3(node["Joint"]["Axis"], glm::vec3(0.0f, 1.0f, 0.0f));
        joint.connectedAxis =
            readVec3(node["Joint"]["ConnectedAxis"], glm::vec3(0.0f, 1.0f, 0.0f));
        joint.distance = node["Joint"]["Distance"].AsFloat(2.0f);
        joint.rope = node["Joint"]["Rope"].AsBool(false);
        // Absent from every scene written before limits existed, which reads
        // as a hinge that swings freely - exactly what those scenes had.
        joint.useLimit = node["Joint"]["UseLimit"].AsBool(false);
        joint.minAngle = node["Joint"]["MinAngle"].AsFloat(-1.5707963f);
        joint.maxAngle = node["Joint"]["MaxAngle"].AsFloat(1.5707963f);
        joint.solveOrder = static_cast<int32_t>(node["Joint"]["SolveOrder"].AsFloat(0.0f));
        joint.useSpring = node["Joint"]["UseSpring"].AsBool(false);
        joint.springFrequency = node["Joint"]["SpringFrequency"].AsFloat(0.0f);
        // Defaulting to critical rather than to zero: a scene written before
        // springs existed that somehow has one switched on should not read back
        // as an undamped oscillator.
        joint.springDamping = node["Joint"]["SpringDamping"].AsFloat(1.0f);
        joint.springRestAngle = node["Joint"]["SpringRestAngle"].AsFloat(0.0f);

        joint.useMotor = node["Joint"]["UseMotor"].AsBool(false);
        joint.motorSpeed = node["Joint"]["MotorSpeed"].AsFloat(0.0f);
        joint.maxMotorTorque = node["Joint"]["MaxMotorTorque"].AsFloat(10.0f);
        joint.breakForce = node["Joint"]["BreakForce"].AsFloat(0.0f);
        joint.breakTorque = node["Joint"]["BreakTorque"].AsFloat(0.0f);
        joint.stiffness = node["Joint"]["Stiffness"].AsFloat(0.8f);
        joint.enabled = node["Joint"]["Enabled"].AsBool(true);
        // connectedBody stays null here and is filled in by SceneSerializer,
        // which is the only thing that knows what an index into its entity
        // array means.
    }

    if (node.Has("ConvexHullCollider")) {
        const auto& h = node["ConvexHullCollider"];
        auto& hull = registry.emplace_or_replace<ConvexHullColliderComponent>(entity);
        hull.sourcePath = readAssetRef(h, "Source");
        hull.sourcePrimitive = h["Primitive"].AsString("");
        hull.isTrigger = h["IsTrigger"].AsBool(false);
        hull.layer = static_cast<uint32_t>(h["Layer"].AsNumber(CollisionLayer::kDefault));
        hull.collidesWith = static_cast<uint32_t>(h["CollidesWith"].AsNumber(CollisionLayer::kAll));
    }

    if (node.Has("HeightfieldCollider")) {
        auto& terrain = registry.emplace_or_replace<HeightfieldColliderComponent>(entity);
        // The defaults are TerrainGenerator::kPrimitive*, so a scene file that
        // only says the collider exists gets one that matches the "Terrain"
        // mesh primitive rather than a 0 x 0 grid that silently has no surface.
        terrain.width = static_cast<uint32_t>(
            node["HeightfieldCollider"]["Width"].AsNumber(TerrainGenerator::kPrimitiveWidth));
        terrain.depth = static_cast<uint32_t>(
            node["HeightfieldCollider"]["Depth"].AsNumber(TerrainGenerator::kPrimitiveDepth));
        terrain.heightScale = node["HeightfieldCollider"]["HeightScale"].AsFloat(
            TerrainGenerator::kPrimitiveHeightScale);
        terrain.thickness = node["HeightfieldCollider"]["Thickness"].AsFloat(4.0f);
        terrain.isTrigger = node["HeightfieldCollider"]["IsTrigger"].AsBool(false);
        terrain.layer = static_cast<uint32_t>(
            node["HeightfieldCollider"]["Layer"].AsNumber(CollisionLayer::kDefault));
        terrain.collidesWith = static_cast<uint32_t>(
            node["HeightfieldCollider"]["CollidesWith"].AsNumber(CollisionLayer::kAll));
    }

    if (node.Has("AudioListener")) {
        auto& listener = registry.emplace_or_replace<AudioListenerComponent>(entity);
        listener.isPrimary = node["AudioListener"]["IsPrimary"].AsBool(true);
    }

    if (node.Has("ReflectionProbe")) {
        const auto& p = node["ReflectionProbe"];
        auto& probe = registry.emplace_or_replace<ReflectionProbeComponent>(entity);
        probe.halfExtent = readVec3(p["HalfExtent"], glm::vec3(5.0f));
        probe.hdriPath = p["Hdri"].AsString("");
        probe.intensity = p["Intensity"].AsFloat(1.0f);
    }

    if (node.Has("AudioSource")) {
        const auto& a = node["AudioSource"];
        auto& audio = registry.emplace_or_replace<AudioSourceComponent>(entity);
        audio.soundFile = readAssetRef(a, "Clip", "assets/audio/ambient.wav");
        audio.volume = a["Volume"].AsFloat(0.8f);
        audio.pitch = a["Pitch"].AsFloat(1.0f);
        audio.isPlaying = a["Playing"].AsBool(true);
        audio.loop = a["Loop"].AsBool(true);
        audio.referenceDistance = a["ReferenceDistance"].AsFloat(1.5f);
        audio.maxDistance = a["MaxDistance"].AsFloat(40.0f);
    }

    if (node.Has("UIText")) {
        const auto& t = node["UIText"];
        auto& text = registry.emplace_or_replace<UITextComponent>(entity);
        text.text = t["Text"].AsString("Score: 0");
        text.anchor = readAnchor(t["Anchor"], text.anchor);
        text.offset = readVec2(t["Offset"], text.offset);
        text.fontSize = t["FontSize"].AsFloat(32.0f);
        text.color = readVec4(t["Color"], text.color);

        // Zero for a scene written before wrapping existed, which is the value
        // that means "do not wrap" - so every label in every saved scene keeps
        // running on one line exactly as it always has.
        text.wrapWidth = t["WrapWidth"].AsFloat(0.0f);
        text.shadow = t["Shadow"].AsBool(true);
        text.visible = t["Visible"].AsBool(true);
    }

    if (node.Has("UIButton")) {
        const auto& b = node["UIButton"];
        auto& button = registry.emplace_or_replace<UIButtonComponent>(entity);
        button.label = b["Label"].AsString("Play");
        button.anchor = readAnchor(b["Anchor"], button.anchor);
        button.offset = readVec2(b["Offset"], button.offset);
        button.size = readVec2(b["Size"], button.size);
        button.fontSize = b["FontSize"].AsFloat(28.0f);
        button.cornerRadius = b["CornerRadius"].AsFloat(10.0f);
        button.color = readVec4(b["Color"], button.color);
        button.hoverColor = readVec4(b["HoverColor"], button.hoverColor);
        button.pressColor = readVec4(b["PressColor"], button.pressColor);
        button.disabledColor = readVec4(b["DisabledColor"], button.disabledColor);
        button.textColor = readVec4(b["TextColor"], button.textColor);
        button.enabled = b["Enabled"].AsBool(true);
        button.visible = b["Visible"].AsBool(true);
    }

    if (node.Has("UITextField")) {
        const auto& f = node["UITextField"];
        auto& field = registry.emplace_or_replace<UITextFieldComponent>(entity);
        field.text = f["Text"].AsString("");
        field.placeholder = f["Placeholder"].AsString("Enter your name");
        field.anchor = readAnchor(f["Anchor"], field.anchor);
        field.offset = readVec2(f["Offset"], field.offset);
        field.size = readVec2(f["Size"], field.size);
        field.fontSize = f["FontSize"].AsFloat(26.0f);
        field.cornerRadius = f["CornerRadius"].AsFloat(8.0f);
        field.maxLength = static_cast<int>(f["MaxLength"].AsFloat(24.0f));
        field.color = readVec4(f["Color"], field.color);
        field.focusColor = readVec4(f["FocusColor"], field.focusColor);
        field.borderColor = readVec4(f["BorderColor"], field.borderColor);
        field.focusBorderColor = readVec4(f["FocusBorderColor"], field.focusBorderColor);
        field.textColor = readVec4(f["TextColor"], field.textColor);
        field.placeholderColor = readVec4(f["PlaceholderColor"], field.placeholderColor);
        field.enabled = f["Enabled"].AsBool(true);
        field.visible = f["Visible"].AsBool(true);

        // The caret follows the text rather than being restored: a scene that
        // authored a default in the box should open with the caret after it,
        // which is where someone about to edit it would want to start.
        field.caret = static_cast<int>(field.text.size());
    }

    if (node.Has("UIPanel")) {
        const auto& p = node["UIPanel"];
        auto& panel = registry.emplace_or_replace<UIPanelComponent>(entity);
        panel.anchor = readAnchor(p["Anchor"], panel.anchor);
        panel.offset = readVec2(p["Offset"], panel.offset);
        panel.size = readVec2(p["Size"], panel.size);
        panel.color = readVec4(p["Color"], panel.color);
        panel.cornerRadius = p["CornerRadius"].AsFloat(6.0f);
        panel.fill = p["Fill"].AsFloat(1.0f);
        panel.drawTrack = p["DrawTrack"].AsBool(false);
        panel.trackColor = readVec4(p["TrackColor"], panel.trackColor);
        panel.fillWidth = p["FillWidth"].AsBool(false);
        panel.fillHeight = p["FillHeight"].AsBool(false);

        // False for every scene written before clipping existed, which is the
        // value that confines nothing - so no saved HUD loses an element to a
        // clip it never asked for.
        panel.clipsChildren = p["ClipsChildren"].AsBool(false);
        panel.visible = p["Visible"].AsBool(true);
    }

    if (node.Has("UIShape")) {
        const auto& sh = node["UIShape"];
        auto& shape = registry.emplace_or_replace<UIShapeComponent>(entity);

        // Clamped rather than trusted. An out-of-range kind read straight into
        // the enum is a switch that falls through to nothing, which draws an
        // empty marker and looks like the entity was never reached.
        const int kind = static_cast<int>(sh["Kind"].AsFloat(0.0f));
        shape.kind = (kind >= 0 && kind <= 2) ? static_cast<UIShapeComponent::Kind>(kind)
                                              : UIShapeComponent::Kind::Ring;

        shape.worldSpace = sh["WorldSpace"].AsBool(true);
        shape.anchor = readAnchor(sh["Anchor"], shape.anchor);
        shape.offset = readVec2(sh["Offset"], shape.offset);
        shape.radius = sh["Radius"].AsFloat(24.0f);
        shape.endpoint = readVec3(sh["Endpoint"], shape.endpoint);
        shape.thickness = sh["Thickness"].AsFloat(3.0f);
        shape.segments = static_cast<int32_t>(sh["Segments"].AsFloat(40.0f));
        shape.color = readVec4(sh["Color"], shape.color);
        shape.visible = sh["Visible"].AsBool(true);
    }

    if (node.Has("UIStack")) {
        const auto& st = node["UIStack"];
        auto& stack = registry.emplace_or_replace<UIStackComponent>(entity);
        stack.horizontal = st["Horizontal"].AsBool(false);
        stack.anchor = readAnchor(st["Anchor"], stack.anchor);
        stack.offset = readVec2(st["Offset"], stack.offset);
        stack.spacing = st["Spacing"].AsFloat(8.0f);
        stack.visible = st["Visible"].AsBool(true);
    }

    if (node.Has("UIOrder")) {
        auto& ordering = registry.emplace_or_replace<UIOrderComponent>(entity);
        ordering.order = static_cast<int32_t>(node["UIOrder"]["Order"].AsFloat(0.0f));
        ordering.layer = static_cast<int32_t>(node["UIOrder"]["Layer"].AsFloat(0.0f));
    }

    if (node.Has("Script")) {
        const auto& s = node["Script"];
        auto& script = registry.emplace_or_replace<ScriptComponent>(entity,
            s["Name"].AsString("RotatorScript"), s["Enabled"].AsBool(true));

        // Absent in scenes written before parameters existed, which is the
        // ordinary case and needs no migration: a script with no authored
        // parameters reads its own fallbacks, which is what it did before.
        if (s.Has("Parameters") && s["Parameters"].IsObject()) {
            for (const auto& [name, value] : s["Parameters"].AsObject()) {
                script.parameters.emplace_back(name, value.AsFloat(0.0f));
            }
        }
    }

    if (node.Has("ParticleEmitter")) {
        auto& emitter = registry.emplace_or_replace<ParticleEmitterComponent>(entity);
        const auto& e = node["ParticleEmitter"];
        // Older scenes wrote a bare `true` here. AsBool on an object returns
        // the fallback, so those still load - they just get the defaults,
        // which is exactly what they stored.
        if (e.IsObject()) {
            emitter.maxParticles = static_cast<uint32_t>(e["MaxParticles"].AsNumber(100.0));
            emitter.emitRate = e["EmitRate"].AsFloat(10.0f);
            emitter.particleLifetime = e["ParticleLifetime"].AsFloat(2.0f);
            emitter.startColor = readVec4(e["StartColor"], glm::vec4(1.0f, 0.6f, 0.1f, 1.0f));
            emitter.endColor = readVec4(e["EndColor"], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            emitter.velocityRange = readVec3(e["VelocityRange"], glm::vec3(0.5f, 2.0f, 0.5f));
            emitter.particleSize = e["ParticleSize"].AsFloat(0.08f);
        }
    }

    if (node.Has("SpriteAnimation")) {
        auto& sprite = registry.emplace_or_replace<SpriteAnimationComponent>(entity);
        const auto& s = node["SpriteAnimation"];
        if (s.IsObject()) {
            // Columns and rows default to ONE, not zero: one cell is the whole
            // texture, which is what a sheet that says nothing about its grid
            // has to mean. Zero would be a division, and a key missing from an
            // older scene must never be the difference between a picture and a
            // crash.
            sprite.columns = static_cast<uint32_t>(s["Columns"].AsNumber(1.0));
            sprite.rows = static_cast<uint32_t>(s["Rows"].AsNumber(1.0));
            sprite.firstFrame = static_cast<uint32_t>(s["FirstFrame"].AsNumber(0.0));
            sprite.frameCount = static_cast<uint32_t>(s["FrameCount"].AsNumber(0.0));
            sprite.framesPerSecond = s["FramesPerSecond"].AsFloat(12.0f);
            sprite.loop = s["Loop"].AsBool(true);
            sprite.playing = s["Playing"].AsBool(true);
            sprite.frame = static_cast<uint32_t>(s["Frame"].AsNumber(0.0));
            sprite.elapsed = s["Elapsed"].AsFloat(0.0f);
        }
    }

    if (node.Has("Tilemap")) {
        auto& tilemap = registry.emplace_or_replace<TilemapComponent>(entity);
        const auto& t = node["Tilemap"];
        if (t.IsObject()) {
            // The atlas defaults to one cell, as a sprite sheet does: the whole
            // texture, which is what a grid the file says nothing about must
            // mean. The map defaults to the component's own default size.
            tilemap.atlasColumns = static_cast<uint32_t>(t["AtlasColumns"].AsNumber(1.0));
            tilemap.atlasRows = static_cast<uint32_t>(t["AtlasRows"].AsNumber(1.0));

            const TilemapComponent defaults;
            const auto width = static_cast<uint32_t>(t["Width"].AsNumber(defaults.width));
            const auto height = static_cast<uint32_t>(t["Height"].AsNumber(defaults.height));

            // A file can say any size, and the cells are allocated by the
            // size it says, so the cap is checked HERE and not left to the
            // bake - by then a hundred-thousand-square map has asked for forty
            // gigabytes from inside a scene load. A refused size leaves the
            // map at its default size and empty, with the entity intact and
            // a line saying why, which is what a load that cannot honour a
            // number is meant to do.
            if (!tilemap.Resize(width, height)) {
                SUPERSONIC_LOG_WARN("ComponentCodec")
                    << "A " << width << "x" << height << " tilemap is over the cap of "
                    << TilemapComponent::kMaxCells << " cells; loading it at "
                    << tilemap.width << "x" << tilemap.height << ", empty." << std::endl;
            } else {
                // As many cells as the array has and the map can hold,
                // whichever is fewer. Too few - a file written by hand, or by
                // a writer that trimmed trailing empties - leaves the rest
                // empty; too many is a map that was shrunk in a text editor
                // without its cells being cut, and the cells past the end are
                // dropped rather than wrapped onto the next row. A cell that
                // is not a number is empty.
                const auto& cells = t["Cells"].AsArray();
                const size_t count = std::min(cells.size(), tilemap.cellCount());
                for (size_t i = 0; i < count; ++i) {
                    tilemap.cells[i] = static_cast<int32_t>(
                        cells[i].AsNumber(static_cast<double>(TilemapComponent::kEmpty)));
                }
            }
        }
    }

    if (node.Has("Animator")) {
        const auto& a = node["Animator"];
        auto& animator = registry.emplace_or_replace<AnimatorComponent>(entity);
        animator.clipName = a["Clip"].AsString("");
        animator.time = a["Time"].AsFloat(0.0f);
        animator.speed = a["Speed"].AsFloat(1.0f);
        animator.loop = a["Loop"].AsBool(true);
        animator.blendDuration = a["BlendDuration"].AsFloat(0.25f);
        animator.playing = a["Playing"].AsBool(true);
    }

    if (node["HasRenderable"].AsBool(false)) {
        auto& renderable = registry.emplace_or_replace<RenderableComponent>(entity);
        if (node.Has("Renderable")) {
            renderable.isVisible = node["Renderable"]["Visible"].AsBool(true);
            renderable.castsShadow = node["Renderable"]["CastsShadow"].AsBool(true);
            // Zero is "no opinion", which is what every scene written before
            // draw order existed holds - and what makes the sort a no-op for them.
            renderable.sortKey = static_cast<int32_t>(
                node["Renderable"]["SortKey"].AsFloat(0.0f));
        }
    }
}


bool RegisterComponent(std::string key, ComponentWriter writer, ComponentReader reader) {
    // A component with no name, no writer or no reader is a registration that
    // would fail later and further away - at save time, or worse at load time
    // against a file that has already been written.
    if (key.empty() || !writer || !reader) return false;

    for (const auto& existing : registeredComponents()) {
        if (existing.key == key) return false;
    }

    registeredComponents().push_back(RegisteredComponent{std::move(key), std::move(writer),
                                                         std::move(reader)});
    return true;
}

void ClearRegisteredComponents() { registeredComponents().clear(); }

std::size_t RegisteredComponentCount() { return registeredComponents().size(); }

} // namespace ComponentCodec

} // namespace Supersonic
