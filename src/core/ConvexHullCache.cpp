#include "core/ConvexHullCache.hpp"

#include <algorithm>

#include "core/Components.hpp"
#include "core/GltfLoader.hpp"
#include "core/Log.hpp"
#include "core/MeshData.hpp"
#include "core/ModelLoader.hpp"

namespace Supersonic {

namespace {

bool endsWith(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

// Every position the asset occupies.
//
// Positions only: a hull is of the OUTSIDE, and normals, texture coordinates
// and skin weights say nothing about where that is. A glTF's submeshes are all
// merged, because one asset is one collider - a rock with two materials is
// still one rock.
bool loadPoints(const std::string& primitive, const std::string& path,
                std::vector<glm::vec3>& out) {
    out.clear();

    const auto take = [&out](const MeshData& mesh) {
        out.reserve(out.size() + mesh.vertices.size());
        for (const Vertex& vertex : mesh.vertices) out.push_back(vertex.pos);
    };

    if (!path.empty()) {
        if (endsWith(path, ".gltf") || endsWith(path, ".glb")) {
            const GltfLoader::Scene scene = GltfLoader::Load(path);
            if (!scene.ok) {
                SUPERSONIC_LOG_ERROR("ConvexHullCache") << scene.error << std::endl;
                return false;
            }
            for (const auto& submesh : scene.submeshes) take(submesh.mesh);
            return !out.empty();
        }

        MeshData mesh;
        if (!ModelLoader::LoadOBJ(path, mesh)) return false;
        take(mesh);
        return !out.empty();
    }

    // The same numbers MeshRegistry generates the visible mesh from, named in
    // ModelLoader so the collider and the mesh cannot drift apart.
    MeshData mesh;
    bool ok = false;
    if (primitive == "Cube") {
        ok = ModelLoader::GenerateCube(ModelLoader::kCubeSize, mesh);
    } else if (primitive == "Sphere") {
        ok = ModelLoader::GenerateSphere(ModelLoader::kSphereRadius, ModelLoader::kSphereRings,
                                         ModelLoader::kSphereSectors, mesh);
    } else if (primitive == "Plane") {
        // A plane is flat, so its hull is not a solid and Build says so. Loaded
        // anyway rather than special-cased, because "this asset cannot be a
        // hull" is one answer arrived at in one place.
        ok = ModelLoader::GeneratePlane(ModelLoader::kPlaneWidth, ModelLoader::kPlaneHeight, mesh);
    } else {
        SUPERSONIC_LOG_ERROR("ConvexHullCache")
            << "No hull source: '" << primitive << "' is not a primitive this can build."
            << std::endl;
        return false;
    }

    if (!ok) return false;
    take(mesh);
    return !out.empty();
}

} // namespace

const ConvexHull* ConvexHullCache::Get(const std::string& primitive, const std::string& path) {
    Key key{primitive, path};

    const auto existing = m_hulls.find(key);
    if (existing != m_hulls.end()) {
        // An entry that failed to build is left INVALID rather than removed, so
        // a broken reference costs one load instead of one per step forever.
        return existing->second.valid() ? &existing->second : nullptr;
    }

    ConvexHull& hull = m_hulls[key];

    std::vector<glm::vec3> points;
    if (!loadPoints(primitive, path, points)) return nullptr;

    if (!hull.Build(points)) {
        SUPERSONIC_LOG_ERROR("ConvexHullCache")
            << (path.empty() ? primitive : path)
            << " does not describe a solid: a hull needs four points that are not all in "
            << "one plane." << std::endl;
        return nullptr;
    }

    SUPERSONIC_LOG_INFO("ConvexHullCache")
        << "Hull of " << (path.empty() ? primitive : path) << ": " << hull.vertices().size()
        << " vertices, " << hull.faces().size() << " faces"
        << (hull.residual() > 0.0f
                ? ", " + std::to_string(hull.residual()) + " outside the vertex cap"
                : std::string())
        << "." << std::endl;

    return &hull;
}

const ConvexHull* ConvexHullCache::Get(entt::registry& registry, entt::entity entity,
                                       const ConvexHullColliderComponent& collider) {
    if (!collider.sourcePath.empty() || !collider.sourcePrimitive.empty()) {
        return Get(collider.sourcePrimitive, collider.sourcePath);
    }

    // Nothing named, so the collider is whatever the entity is drawn as. An
    // entity with no mesh has nothing to be the hull of.
    const auto* mesh = registry.try_get<MeshComponent>(entity);
    if (!mesh) return nullptr;
    return Get(mesh->primitiveType, mesh->filePath);
}

void ConvexHullCache::Trim() {
    if (m_hulls.size() >= kMaxHulls) m_hulls.clear();
}

ConvexHullCache& ConvexHullCache::For(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<ConvexHullCache>()) return *existing;
    return registry.ctx().emplace<ConvexHullCache>();
}

} // namespace Supersonic
