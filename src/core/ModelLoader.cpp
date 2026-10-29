#include "core/ModelLoader.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace Supersonic {

namespace {

constexpr float kPi = 3.14159265359f;

// An OBJ face vertex references position/uv/normal by independent indices, so
// a unique combination is what becomes a unique vertex in the buffer.
struct ObjKey {
    int p{0};
    int t{0};
    int n{0};
    bool operator==(const ObjKey& o) const { return p == o.p && t == o.t && n == o.n; }
};

struct ObjKeyHash {
    size_t operator()(const ObjKey& k) const {
        return (static_cast<size_t>(k.p) * 73856093u)
             ^ (static_cast<size_t>(k.t) * 19349663u)
             ^ (static_cast<size_t>(k.n) * 83492791u);
    }
};

// OBJ indices are 1-based and may be negative (relative to the end).
int resolveObjIndex(int raw, size_t count) {
    if (raw > 0) return raw - 1;
    if (raw < 0) return static_cast<int>(count) + raw;
    return -1;
}

} // namespace

bool ModelLoader::GenerateSphere(float radius, uint32_t rings, uint32_t sectors, MeshData& out) {
    out.clear();

    // rings/sectors of 0 or 1 used to underflow to ~4.29 billion loop
    // iterations via (rings - 1) on an unsigned type, and rings == 1 made
    // R = 1/0 = inf, producing NaN positions.
    if (rings < 2 || sectors < 3 || radius <= 0.0f) {
        SUPERSONIC_LOG_ERROR("ModelLoader") << "GenerateSphere requires radius > 0, rings >= 2, sectors >= 3 (got "
                  << radius << ", " << rings << ", " << sectors << ")." << std::endl;
        return false;
    }

    const float R = 1.0f / static_cast<float>(rings - 1);
    const float S = 1.0f / static_cast<float>(sectors - 1);

    out.vertices.reserve(static_cast<size_t>(rings) * sectors);
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            const float y = std::sin(-kPi / 2.0f + kPi * r * R);
            const float x = std::cos(2.0f * kPi * s * S) * std::sin(kPi * r * R);
            const float z = std::sin(2.0f * kPi * s * S) * std::sin(kPi * r * R);

            Vertex vertex{};
            vertex.pos = glm::vec3(x * radius, y * radius, z * radius);
            vertex.normal = glm::normalize(glm::vec3(x, y, z));
            vertex.color = glm::vec3(0.9f, 0.9f, 0.95f);
            vertex.texCoord = glm::vec2(s * S, r * R);
            out.vertices.push_back(vertex);
        }
    }

    out.indices.reserve(static_cast<size_t>(rings - 1) * (sectors - 1) * 6);
    for (uint32_t r = 0; r + 1 < rings; ++r) {
        for (uint32_t s = 0; s + 1 < sectors; ++s) {
            const uint32_t i0 = r * sectors + s;
            const uint32_t i1 = r * sectors + (s + 1);
            const uint32_t i2 = (r + 1) * sectors + (s + 1);
            const uint32_t i3 = (r + 1) * sectors + s;

            // Wound so the face normal agrees with the vertex normals. The
            // original order was the reverse, which made every sphere in the
            // engine inside-out: back-face culling removed the near hemisphere
            // and what you saw was the inside of the far one. Smooth and
            // plausible at a glance, and wrong for every lighting term that
            // uses the view direction - a sphere had no specular highlight it
            // could possibly show, because its normals all faced away.
            out.indices.insert(out.indices.end(), { i0, i2, i1, i0, i3, i2 });
        }
    }

    out.computeTangents();
    out.computeBounds();
    return true;
}

bool ModelLoader::GenerateCube(float size, MeshData& out) {
    out.clear();

    if (size <= 0.0f) {
        SUPERSONIC_LOG_ERROR("ModelLoader") << "GenerateCube requires size > 0 (got " << size << ")." << std::endl;
        return false;
    }

    const float h = size * 0.5f;

    // 24 vertices so every face carries its own normal and UV set.
    out.vertices = {
        // +Z
        {{-h, -h,  h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
        {{ h, -h,  h}, {0.0f, 0.0f, 1.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
        {{ h,  h,  h}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.2f, 1.0f}, {1.0f, 1.0f}},
        {{-h,  h,  h}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 1.0f}},
        // -Z
        {{ h, -h, -h}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.2f, 1.0f}, {0.0f, 0.0f}},
        {{-h, -h, -h}, {0.0f, 0.0f, -1.0f}, {0.2f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-h,  h, -h}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{ h,  h, -h}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
        // -Y (bottom)
        {{-h, -h, -h}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.4f, 0.4f}, {0.0f, 0.0f}},
        {{ h, -h, -h}, {0.0f, -1.0f, 0.0f}, {0.4f, 1.0f, 0.4f}, {1.0f, 0.0f}},
        {{ h, -h,  h}, {0.0f, -1.0f, 0.0f}, {0.4f, 0.4f, 1.0f}, {1.0f, 1.0f}},
        {{-h, -h,  h}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.4f}, {0.0f, 1.0f}},
        // +Y (top)
        {{-h,  h,  h}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.3f, 0.3f}, {0.0f, 0.0f}},
        {{ h,  h,  h}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.8f, 0.3f}, {1.0f, 0.0f}},
        {{ h,  h, -h}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.3f, 0.8f}, {1.0f, 1.0f}},
        {{-h,  h, -h}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.3f}, {0.0f, 1.0f}},
        // +X
        {{ h, -h,  h}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.5f, 0.2f}, {0.0f, 0.0f}},
        {{ h, -h, -h}, {1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.5f}, {1.0f, 0.0f}},
        {{ h,  h, -h}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.2f, 0.9f}, {1.0f, 1.0f}},
        {{ h,  h,  h}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.9f, 0.2f}, {0.0f, 1.0f}},
        // -X
        {{-h, -h, -h}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.6f, 0.9f}, {0.0f, 0.0f}},
        {{-h, -h,  h}, {-1.0f, 0.0f, 0.0f}, {0.9f, 0.2f, 0.6f}, {1.0f, 0.0f}},
        {{-h,  h,  h}, {-1.0f, 0.0f, 0.0f}, {0.6f, 0.9f, 0.2f}, {1.0f, 1.0f}},
        {{-h,  h, -h}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.6f}, {0.0f, 1.0f}}
    };

    out.indices = {
         0,  1,  2,  2,  3,  0, // +Z
         4,  5,  6,  6,  7,  4, // -Z
         8,  9, 10, 10, 11,  8, // -Y
        12, 13, 14, 14, 15, 12, // +Y
        16, 17, 18, 18, 19, 16, // +X
        20, 21, 22, 22, 23, 20  // -X
    };

    out.computeTangents();
    out.computeBounds();
    return true;
}

bool ModelLoader::GenerateBox(float size, MeshData& out) {
    // Built FROM the cube rather than written out beside it, so the two can
    // never disagree about a corner, a normal or a winding - only the colour.
    if (!GenerateCube(size, out)) return false;
    for (Vertex& vertex : out.vertices) vertex.color = glm::vec3(1.0f);
    return true;
}

bool ModelLoader::GenerateQuad(float width, float height, MeshData& out) {
    out.clear();

    if (width <= 0.0f || height <= 0.0f) {
        SUPERSONIC_LOG_ERROR("ModelLoader")
            << "GenerateQuad requires positive extents (got " << width << "x" << height << ")."
            << std::endl;
        return false;
    }

    const float hw = width * 0.5f;
    const float hh = height * 0.5f;

    // XY plane, facing +Z, wound counter-clockwise when seen from +Z.
    //
    // WHITE vertex colours. The shader multiplies albedo by the vertex colour,
    // so anything else here turns an authored flat colour into a gradient -
    // which is exactly what happens if you build a 2D quad out of GenerateCube,
    // whose corners are deliberately rainbow.
    out.vertices = {
        {{-hw, -hh, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{ hw, -hh, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{ hw,  hh, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-hw,  hh, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
    };

    out.indices = {0, 1, 2, 2, 3, 0};

    // BOTH OF THESE, and the bounds are not optional.
    //
    // This function returned here, having called neither. MeshData's bounds
    // default to (0,0,0)..(0,0,0) and clear() resets them to that, so every
    // quad this engine ever generated carried a DEGENERATE point for its
    // local AABB - which createGpuMesh copies onto the GpuMesh and RenderSystem
    // copies onto the renderable. Frustum culling then tested each quad against
    // its own centre: a sprite disappeared the instant its centre left the
    // view, while the quad itself was still metres on screen. The bigger the
    // sprite the earlier it went, which is why a level's backgrounds vanished
    // first and small things near the middle survived.
    //
    // It took a game made almost entirely of quads to surface it, and it hid
    // from every test written to find it, because those tests read the same
    // bounds the renderer did and agreed with it. GenerateCube and
    // GeneratePlane both call these; this one is the odd one out.
    out.computeTangents();
    out.computeBounds();
    return true;
}

bool ModelLoader::GeneratePlane(float width, float height, MeshData& out) {
    out.clear();

    if (width <= 0.0f || height <= 0.0f) {
        SUPERSONIC_LOG_ERROR("ModelLoader") << "GeneratePlane requires positive extents (got "
                  << width << "x" << height << ")." << std::endl;
        return false;
    }

    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;

    out.vertices = {
        {{-halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {0.0f, 0.0f}},
        {{ halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {1.0f, 0.0f}},
        {{ halfW, 0.0f,  halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {1.0f, 1.0f}},
        {{-halfW, 0.0f,  halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {0.0f, 1.0f}}
    };

    // Wound so the geometric normal matches the +Y vertex normal.
    //
    // The original order (0,1,2, 2,3,0) produced cross(b-a, c-a) = -Y, i.e. a
    // plane facing DOWN, which back-face culling then discarded when viewed
    // from above. It went unnoticed because RenderSystem used to draw a cube for
    // every entity, so this mesh was never actually rasterised.
    out.indices = { 0, 3, 2, 0, 2, 1 };

    out.computeTangents();
    out.computeBounds();
    return true;
}

bool ModelLoader::LoadOBJ(const std::string& filepath, MeshData& out) {
    out.clear();

    std::ifstream file(filepath);
    if (!file.is_open()) {
        SUPERSONIC_LOG_ERROR("ModelLoader") << "Failed to open OBJ file: " << filepath << std::endl;
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;

    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> unique;
    bool sawFace = false;

    std::string line;
    size_t lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            // A truncated line such as "v 1.0" used to leave y and z
            // indeterminate: the failed extraction does not zero them.
            glm::vec3 p{0.0f};
            if (!(ss >> p.x >> p.y >> p.z)) {
                SUPERSONIC_LOG_ERROR("ModelLoader") << filepath << ":" << lineNumber
                          << " malformed vertex, skipped." << std::endl;
                continue;
            }
            positions.push_back(p);
        } else if (prefix == "vn") {
            glm::vec3 n{0.0f, 1.0f, 0.0f};
            if (!(ss >> n.x >> n.y >> n.z)) continue;
            normals.push_back(n);
        } else if (prefix == "vt") {
            glm::vec2 t{0.0f};
            if (!(ss >> t.x >> t.y)) continue;
            uvs.push_back(t);
        } else if (prefix == "f") {
            sawFace = true;

            std::vector<uint32_t> face;
            std::string token;
            while (ss >> token) {
                // Accepted forms: v, v/vt, v//vn, v/vt/vn.
                // Split on '/' keeping empty fields, so "1//2" yields
                // {"1", "", "2"} rather than collapsing to {"1", "2"} and
                // mistaking the normal index for a texture index.
                int slot[3] = {0, 0, 0};
                size_t field = 0;
                size_t start = 0;
                while (field < 3) {
                    const size_t sep = token.find('/', start);
                    const std::string part = token.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
                    if (!part.empty()) {
                        try {
                            slot[field] = std::stoi(part);
                        } catch (const std::exception&) {
                            slot[field] = 0;
                        }
                    }
                    ++field;
                    if (sep == std::string::npos) break;
                    start = sep + 1;
                }
                const int vi = slot[0];
                const int ti = slot[1];
                const int ni = slot[2];

                const int pIdx = resolveObjIndex(vi, positions.size());
                if (pIdx < 0 || static_cast<size_t>(pIdx) >= positions.size()) {
                    SUPERSONIC_LOG_ERROR("ModelLoader") << filepath << ":" << lineNumber
                              << " face references out-of-range vertex " << vi << ", skipped." << std::endl;
                    face.clear();
                    break;
                }
                const int tIdx = ti != 0 ? resolveObjIndex(ti, uvs.size()) : -1;
                const int nIdx = ni != 0 ? resolveObjIndex(ni, normals.size()) : -1;

                const ObjKey key{ pIdx, tIdx, nIdx };
                auto it = unique.find(key);
                if (it == unique.end()) {
                    Vertex vertex{};
                    vertex.pos = positions[static_cast<size_t>(pIdx)];
                    vertex.normal = (nIdx >= 0 && static_cast<size_t>(nIdx) < normals.size())
                                    ? normals[static_cast<size_t>(nIdx)]
                                    : glm::vec3(0.0f, 1.0f, 0.0f);
                    vertex.color = glm::vec3(1.0f);
                    vertex.texCoord = (tIdx >= 0 && static_cast<size_t>(tIdx) < uvs.size())
                                      ? uvs[static_cast<size_t>(tIdx)]
                                      : glm::vec2(0.0f);

                    const auto newIndex = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(vertex);
                    unique.emplace(key, newIndex);
                    face.push_back(newIndex);
                } else {
                    face.push_back(it->second);
                }
            }

            // Triangulate as a fan; handles quads and larger n-gons.
            for (size_t i = 2; i < face.size(); ++i) {
                out.indices.push_back(face[0]);
                out.indices.push_back(face[i - 1]);
                out.indices.push_back(face[i]);
            }
        }
    }

    if (!sawFace) {
        // Without face data there is no topology to build. The old code
        // fabricated a sequential index list over raw positions, which turned
        // any real model into unrelated disjoint triangles.
        SUPERSONIC_LOG_ERROR("ModelLoader") << filepath
                  << " contains no face (f) records; cannot build a mesh." << std::endl;
        return false;
    }

    if (out.empty()) {
        SUPERSONIC_LOG_ERROR("ModelLoader") << filepath << " produced no usable geometry." << std::endl;
        return false;
    }

    // Supply flat normals when the file carried none.
    if (normals.empty()) {
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            Vertex& a = out.vertices[out.indices[i]];
            Vertex& b = out.vertices[out.indices[i + 1]];
            Vertex& c = out.vertices[out.indices[i + 2]];
            const glm::vec3 area = glm::cross(b.pos - a.pos, c.pos - a.pos);

            // A triangle whose corners are in a line has no normal. glm::normalize
            // of the zero vector is NaN, and writing it onto the three vertices
            // overwrote the good normal of any vertex a sound triangle shares with
            // it. Such a triangle is skipped; a vertex nothing sound reached gets the
            // fallback below.
            if (!(glm::dot(area, area) > 1e-30f) || !std::isfinite(glm::dot(area, area))) continue;
            a.normal = b.normal = c.normal = glm::normalize(area);
        }
        for (Vertex& vertex : out.vertices) {
            // Essentially zero only: every normal set above is a unit vector.
            if (glm::dot(vertex.normal, vertex.normal) < 1e-12f) vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    out.computeTangents();
    out.computeBounds();
    SUPERSONIC_LOG_INFO("ModelLoader") << "Loaded " << filepath << " (" << out.vertices.size()
              << " vertices, " << out.indices.size() / 3 << " triangles)." << std::endl;
    return true;
}

} // namespace Supersonic
