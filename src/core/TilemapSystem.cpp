#include "core/TilemapSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Log.hpp"
#include "core/SpriteAnimationSystem.hpp"
#include "renderer/MeshRegistry.hpp"

namespace Supersonic {

// The component carries its own copy of the sentinel because Components.hpp
// does not include the renderer. If the two ever disagree, a map that draws
// nothing would be handed to the draw loop as a real id.
static_assert(TilemapComponent::kNoMesh == MeshRegistry::kInvalidMesh,
              "TilemapComponent::kNoMesh must be MeshRegistry::kInvalidMesh");

void TilemapSystem::CellRect(uint32_t column, uint32_t row, glm::vec2& outMin, glm::vec2& outMax) {
    const float x = static_cast<float>(column);
    const float y = static_cast<float>(row);
    outMin = glm::vec2(x, -(y + 1.0f));
    outMax = glm::vec2(x + 1.0f, -y);
}

bool TilemapSystem::CellFromLocal(const TilemapComponent& map, const glm::vec2& local,
                                  uint32_t& outColumn, uint32_t& outRow) {
    // Rows count DOWN from the origin, so the row coordinate is -y. The
    // comparisons are written against the map's extent rather than against a
    // floored index, because floor of a slightly negative number is -1 and a
    // cast of that to unsigned is a very large column.
    const float across = local.x;
    const float down = -local.y;
    if (!(across >= 0.0f) || !(down >= 0.0f)) return false;
    if (across >= static_cast<float>(map.width)) return false;
    if (down >= static_cast<float>(map.height)) return false;

    outColumn = static_cast<uint32_t>(std::floor(across));
    outRow = static_cast<uint32_t>(std::floor(down));
    return true;
}

bool TilemapSystem::CellFromRay(const TilemapComponent& map, const glm::mat4& world, const Ray& ray,
                                uint32_t& outColumn, uint32_t& outRow) {
    const float det = glm::determinant(world);
    if (std::fabs(det) < 1e-12f) return false;

    const glm::mat4 inverse = glm::inverse(world);
    const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 direction = glm::vec3(inverse * glm::vec4(ray.direction, 0.0f));

    // The map lies in its own z = 0 plane. A ray running along that plane
    // never crosses it; a ray that crossed it behind the eye is not pointing
    // at the map.
    if (std::fabs(direction.z) < 1e-8f) return false;
    const float t = -origin.z / direction.z;
    if (t < 0.0f) return false;

    const glm::vec3 hit = origin + direction * t;
    return CellFromLocal(map, glm::vec2(hit.x, hit.y), outColumn, outRow);
}

bool TilemapSystem::Bake(const TilemapComponent& map, MeshData& out, BakeReport* report) {
    out.clear();
    BakeReport local;
    BakeReport& counts = report ? *report : local;
    counts = BakeReport{};

    if (map.atlasColumns == 0 || map.atlasRows == 0) return false;
    if (map.width == 0 || map.height == 0) return false;

    // Refused, not truncated. Half a map is a map with a straight edge
    // through it, which reads as a bug in the level rather than a limit in
    // the engine. The diagnostic names the cap so the author knows which.
    //
    // Reached only by a map whose size was written straight into the fields:
    // Resize, Set and Fill refuse the cap themselves, so nothing that went
    // through them arrives here. Logged once per content change rather than
    // once per frame, because Sync records the hash of a refused bake too.
    const size_t cellCount = map.cellCount();
    if (cellCount > TilemapComponent::kMaxCells) {
        SUPERSONIC_LOG_WARN("Tilemap") << "A " << map.width << "x" << map.height
            << " map is " << cellCount << " cells, over the cap of "
            << TilemapComponent::kMaxCells << "; it will not draw. Split it "
               "across several tilemap entities." << std::endl;
        return false;
    }

    const uint32_t atlasCells = map.atlasColumns * map.atlasRows;
    const size_t present = std::min(map.cells.size(), cellCount);

    uint32_t occupied = 0;
    for (size_t i = 0; i < present; ++i) {
        if (!TilemapComponent::IsEmpty(map.cells[i])) ++occupied;
    }
    if (occupied == 0) return false;

    out.vertices.reserve(static_cast<size_t>(occupied) * 4);
    out.indices.reserve(static_cast<size_t>(occupied) * 6);

    for (uint32_t row = 0; row < map.height; ++row) {
        for (uint32_t column = 0; column < map.width; ++column) {
            const int32_t cell = map.At(column, row);
            if (TilemapComponent::IsEmpty(cell)) continue;

            const uint32_t atlasIndex = TilemapComponent::AtlasIndex(cell);
            if (atlasIndex >= atlasCells) {
                ++counts.outOfRange;
                continue;
            }

            // The SAME arithmetic a flipbook uses to find a frame, so an index
            // means one picture everywhere. CellTransform wraps a frame past
            // the end; the check above has already refused one here.
            glm::vec2 scale(1.0f), offset(0.0f);
            SpriteAnimationSystem::CellTransform(map.atlasColumns, map.atlasRows, atlasIndex,
                                                 scale, offset);

            float u0 = offset.x;
            float u1 = offset.x + scale.x;
            float v0 = offset.y;
            float v1 = offset.y + scale.y;
            if (TilemapComponent::FlipH(cell)) std::swap(u0, u1);
            if (TilemapComponent::FlipV(cell)) std::swap(v0, v1);

            glm::vec2 min, max;
            CellRect(column, row, min, max);

            // Bottom-left, bottom-right, top-right, top-left: GenerateQuad's
            // order, with GenerateQuad's texture corners - v is zero along the
            // TOP edge, because the image's first row is its top and so is the
            // atlas's row zero. WHITE vertex colour, because the shader
            // multiplies the albedo by it and Vertex leaves it uninitialised.
            const auto base = static_cast<uint32_t>(out.vertices.size());
            const glm::vec3 normal(0.0f, 0.0f, 1.0f);
            const glm::vec3 white(1.0f);
            out.vertices.push_back({{min.x, min.y, 0.0f}, normal, white, {u0, v1}});
            out.vertices.push_back({{max.x, min.y, 0.0f}, normal, white, {u1, v1}});
            out.vertices.push_back({{max.x, max.y, 0.0f}, normal, white, {u1, v0}});
            out.vertices.push_back({{min.x, max.y, 0.0f}, normal, white, {u0, v0}});

            out.indices.push_back(base + 0);
            out.indices.push_back(base + 1);
            out.indices.push_back(base + 2);
            out.indices.push_back(base + 2);
            out.indices.push_back(base + 3);
            out.indices.push_back(base + 0);
            ++counts.drawn;
        }
    }

    if (counts.outOfRange > 0) {
        SUPERSONIC_LOG_WARN("Tilemap") << counts.outOfRange << " cell(s) name an atlas index "
            "past a " << map.atlasColumns << "x" << map.atlasRows << " grid and were not drawn."
            << std::endl;
    }

    if (counts.drawn == 0) {
        out.clear();
        return false;
    }

    out.computeBounds();
    out.computeTangents();
    return true;
}

uint64_t TilemapSystem::ContentHash(const TilemapComponent& map) {
    // FNV-1a, the same shape as the resource signature, over the numbers the
    // bake reads and nothing else. The scratch fields are left out on purpose:
    // hashing bakedHash into the hash it is compared against would never
    // match.
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](const void* data, size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
    };

    mix(&map.atlasColumns, sizeof(map.atlasColumns));
    mix(&map.atlasRows, sizeof(map.atlasRows));
    mix(&map.width, sizeof(map.width));
    mix(&map.height, sizeof(map.height));

    // Only the cells the map describes. A vector longer than the map, left
    // over from a size written directly, holds entries no bake reads and no
    // hash should either - or shrinking a map would rebake it every frame
    // against a hash that never settles.
    const size_t cellCount = map.cellCount();
    const size_t present = std::min(map.cells.size(), cellCount);
    if (present > 0) mix(map.cells.data(), present * sizeof(int32_t));

    // And every cell the map describes beyond what the vector holds, as the
    // value At answers for it. A map that has never been written and the
    // same map filled with empties read alike and draw alike, so they hash
    // alike: the LOGICAL content is what is hashed, not the vector's length.
    // Bounded by the cap, past which nothing allocates or draws and the size
    // above already tells the maps apart.
    if (map.withinCap()) {
        const int32_t empty = TilemapComponent::kEmpty;
        for (size_t i = present; i < cellCount; ++i) mix(&empty, sizeof(empty));
    }

    return hash == 0 ? 1ull : hash;
}

std::string TilemapSystem::MeshKey(entt::entity entity) {
    return "tilemap:" + std::to_string(static_cast<uint32_t>(entt::to_entity(entity)));
}

} // namespace Supersonic
