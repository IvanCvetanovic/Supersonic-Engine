#include "core/ComponentCodec.hpp"
#include "core/Log.hpp"
#include <limits>
#include <cmath>

#include "core/Components.hpp"

#include <algorithm>

namespace Supersonic {

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
        out << indent << "  \"Path\": \"" << Json::Escape(mesh->filePath) << "\"\n";
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

    if (const auto* camera = registry.try_get<CameraComponent>(entity)) {
        out << indent << "\"Camera\": {\n";
        out << indent << "  \"FOV\": " << camera->fov << ",\n";
        out << indent << "  \"NearPlane\": " << camera->nearPlane << ",\n";
        out << indent << "  \"FarPlane\": " << camera->farPlane << ",\n";
        out << indent << "  \"Position\": "; writeVec3(out, camera->position); out << ",\n";
        out << indent << "  \"Yaw\": " << camera->yaw << ",\n";
        out << indent << "  \"Pitch\": " << camera->pitch << ",\n";
        out << indent << "  \"MovementSpeed\": " << camera->movementSpeed << ",\n";
        out << indent << "  \"MouseSensitivity\": " << camera->mouseSensitivity << ",\n";
        // aspect is not persisted: it is recomputed from the viewport panel
        // every frame, so a stored value would be wrong on any other layout.
        out << indent << "  \"IsPrimary\": " << (camera->isPrimary ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* mat = registry.try_get<MaterialComponent>(entity)) {
        out << indent << "\"Material\": {\n";
        out << indent << "  \"Albedo\": [" << mat->albedoColor.x << ", " << mat->albedoColor.y << ", "
             << mat->albedoColor.z << ", " << mat->albedoColor.w << "],\n";
        out << indent << "  \"AlbedoTexture\": \"" << Json::Escape(mat->albedoTexturePath) << "\",\n";
        out << indent << "  \"NormalTexture\": \"" << Json::Escape(mat->normalTexturePath) << "\",\n";
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
        out << indent << "  \"Asset\": \"" << Json::Escape(mat->materialPath) << "\"\n";
        out << indent << "},\n";
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
        // Only the authored half. isSleeping, the timer and the position it
        // slept at are re-derived within half a second of the scene loading,
        // and writing them would let a scene be saved with a body asleep in
        // mid-air - which would then never fall.
        out << indent << "  \"AllowSleep\": " << (body->allowSleep ? "true" : "false") << ",\n";
        out << indent << "  \"IsKinematic\": " << (body->isKinematic ? "true" : "false") << "\n";
        out << indent << "},\n";
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

    if (const auto* listener = registry.try_get<AudioListenerComponent>(entity)) {
        out << indent << "\"AudioListener\": { \"IsPrimary\": "
             << (listener->isPrimary ? "true" : "false") << " },\n";
    }

    if (const auto* audio = registry.try_get<AudioSourceComponent>(entity)) {
        // voice/failedToLoad are runtime state owned by AudioSystem and are
        // deliberately not persisted.
        out << indent << "\"AudioSource\": {\n";
        out << indent << "  \"Clip\": \"" << Json::Escape(audio->soundFile) << "\",\n";
        out << indent << "  \"Volume\": " << audio->volume << ",\n";
        out << indent << "  \"Pitch\": " << audio->pitch << ",\n";
        out << indent << "  \"Playing\": " << (audio->isPlaying ? "true" : "false") << ",\n";
        out << indent << "  \"Loop\": " << (audio->loop ? "true" : "false") << ",\n";
        out << indent << "  \"ReferenceDistance\": " << audio->referenceDistance << ",\n";
        out << indent << "  \"MaxDistance\": " << audio->maxDistance << "\n";
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
        out << indent << "  \"Shadow\": " << (text->shadow ? "true" : "false") << ",\n";
        out << indent << "  \"Visible\": " << (text->visible ? "true" : "false") << "\n";
        out << indent << "},\n";
    }

    if (const auto* button = registry.try_get<UIButtonComponent>(entity)) {
        // hovered/pressed/clicked are rebuilt from the pointer every frame and
        // are deliberately absent: a button saved mid-press would come back
        // stuck, and a restored click would fire an action nobody asked for.
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
        out << indent << "  \"Visible\": " << (panel->visible ? "true" : "false") << "\n";
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
             << ", \"CastsShadow\": " << (renderable->castsShadow ? "true" : "false") << " },\n";
    }

    out << indent << "\"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false") << "\n";
}

void Read(entt::registry& registry, entt::entity entity, const Json::Value& node) {
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
            m["Primitive"].AsString("Cube"), m["Path"].AsString(""), 0u, 0u);
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

    if (node.Has("Camera")) {
        const auto& c = node["Camera"];
        auto& camera = registry.emplace_or_replace<CameraComponent>(entity);
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
        camera.updateCameraVectors();
    }

    if (node.Has("Material")) {
        const auto& m = node["Material"];
        auto& material = registry.emplace_or_replace<MaterialComponent>(entity);
        material.albedoColor = readVec4(m["Albedo"], glm::vec4(1.0f));
        material.albedoTexturePath = m["AlbedoTexture"].AsString("");
        material.normalTexturePath = m["NormalTexture"].AsString("");
        material.roughness = m["Roughness"].AsFloat(0.4f);
        material.metallic = m["Metallic"].AsFloat(0.1f);
        material.ao = m["AO"].AsFloat(1.0f);
        material.materialPath = m["Asset"].AsString("");
        // Absent in every scene written before the blended pass existed, and
        // false is what those scenes rendered as.
        material.transparent = m["Transparent"].AsBool(false);
        material.emissiveColor = readVec3(m["Emissive"], glm::vec3(0.0f));
        material.emissiveStrength = m["EmissiveStrength"].AsFloat(0.0f);
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
        // A scene written before sleeping existed has no key and must keep
        // being allowed to sleep, which is the default for a new body too.
        body.allowSleep = r["AllowSleep"].AsBool(true);
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

    if (node.Has("AudioListener")) {
        auto& listener = registry.emplace_or_replace<AudioListenerComponent>(entity);
        listener.isPrimary = node["AudioListener"]["IsPrimary"].AsBool(true);
    }

    if (node.Has("AudioSource")) {
        const auto& a = node["AudioSource"];
        auto& audio = registry.emplace_or_replace<AudioSourceComponent>(entity);
        audio.soundFile = a["Clip"].AsString("assets/audio/ambient.wav");
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
        panel.visible = p["Visible"].AsBool(true);
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
        }
    }
}

} // namespace ComponentCodec

} // namespace Supersonic
