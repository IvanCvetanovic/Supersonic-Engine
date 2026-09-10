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
//
// The INDICES come too, now that a collider can be several hulls: a
// decomposition splits triangles, and a bag of positions has no triangles in
// it. Each submesh's indices are shifted by the positions already taken, so the
// merged mesh is one index space.
bool loadPoints(const std::string& primitive, const std::string& path,
                std::vector<glm::vec3>& out, std::vector<uint32_t>& outIndices) {
    out.clear();
    outIndices.clear();

    const auto take = [&out, &outIndices](const MeshData& mesh) {
        const auto base = static_cast<uint32_t>(out.size());
        out.reserve(out.size() + mesh.vertices.size());
        for (const Vertex& vertex : mesh.vertices) out.push_back(vertex.pos);
        outIndices.reserve(outIndices.size() + mesh.indices.size());
        for (uint32_t index : mesh.indices) outIndices.push_back(base + index);
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
    } else if (primitive == "Box") {
        ok = ModelLoader::GenerateBox(ModelLoader::kBoxSize, mesh);
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

const ConvexDecomposition* ConvexHullCache::Get(const std::string& primitive,
                                                const std::string& path) {
    Key key{primitive, path};

    const auto existing = m_hulls.find(key);
    if (existing != m_hulls.end()) {
        // An entry that failed to build is left INVALID rather than removed, so
        // a broken reference costs one load instead of one per step forever.
        return existing->second.valid() ? &existing->second : nullptr;
    }

    ConvexDecomposition& decomposition = m_hulls[key];

    std::vector<glm::vec3> points;
    std::vector<uint32_t> indices;
    if (!loadPoints(primitive, path, points, indices)) return nullptr;

    if (!decomposition.Build(points, indices)) {
        SUPERSONIC_LOG_ERROR("ConvexHullCache")
            << (path.empty() ? primitive : path)
            << " does not describe a solid: a hull needs four points that are not all in "
            << "one plane." << std::endl;
        return nullptr;
    }

    size_t vertices = 0;
    float residual = 0.0f;
    for (const ConvexHull& piece : decomposition.pieces()) {
        vertices += piece.vertices().size();
        residual += piece.residual();
    }

    SUPERSONIC_LOG_INFO("ConvexHullCache")
        << "Collider for " << (path.empty() ? primitive : path) << ": "
        << decomposition.pieces().size() << " hull(s), " << vertices << " vertices"
        // The number worth showing: how much SOLID the collider has that the
        // mesh does not. It is exactly what an object will catch on that the
        // model would have let through, and it is zero for a convex shape.
        << ", " << decomposition.invented() << " invented volume"
        << (residual > 0.0f ? ", " + std::to_string(residual) + " outside the vertex cap"
                            : std::string())
        << "." << std::endl;

    return &decomposition;
}

const ConvexDecomposition* ConvexHullCache::Get(entt::registry& registry, entt::entity entity,
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

size_t ConvexHullCache::Invalidate(const std::string& path) {
    if (path.empty()) return 0;

    size_t dropped = 0;
    for (auto it = m_hulls.begin(); it != m_hulls.end();) {
        if (it->first.path == path) {
            it = m_hulls.erase(it);
            ++dropped;
        } else {
            ++it;
        }
    }
    return dropped;
}

void ConvexHullCache::Trim() {
    if (m_hulls.size() >= kMaxHulls) m_hulls.clear();
}

ConvexHullCache& ConvexHullCache::For(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<ConvexHullCache>()) return *existing;
    return registry.ctx().emplace<ConvexHullCache>();
}

} // namespace Supersonic
