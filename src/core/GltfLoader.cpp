#include "core/GltfLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// The implementation lives in TinyGltfImplementation.cpp; see the note there
// about why it is a separate translation unit and why stb is disabled.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

namespace fs = std::filesystem;

namespace Supersonic {

namespace {

// Reads one scalar out of an accessor, normalising the component type.
template <typename T>
const T* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t& strideOut) {
    const tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];

    const size_t elementSize = static_cast<size_t>(
        tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType)) *
        tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type)));

    strideOut = view.byteStride != 0 ? view.byteStride : elementSize;
    return reinterpret_cast<const T*>(buffer.data.data() + view.byteOffset + accessor.byteOffset);
}

// glTF node transforms are either a full matrix or TRS components.
glm::mat4 nodeLocalMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 m(1.0f);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                m[c][r] = static_cast<float>(node.matrix[static_cast<size_t>(c * 4 + r)]);
            }
        }
        return m;
    }

    glm::mat4 m(1.0f);
    if (node.translation.size() == 3) {
        m = glm::translate(m, glm::vec3(static_cast<float>(node.translation[0]),
                                        static_cast<float>(node.translation[1]),
                                        static_cast<float>(node.translation[2])));
    }
    if (node.rotation.size() == 4) {
        // glTF stores quaternions xyzw; glm::quat is constructed wxyz.
        const glm::quat q(static_cast<float>(node.rotation[3]),
                          static_cast<float>(node.rotation[0]),
                          static_cast<float>(node.rotation[1]),
                          static_cast<float>(node.rotation[2]));
        m *= glm::mat4_cast(q);
    }
    if (node.scale.size() == 3) {
        m = glm::scale(m, glm::vec3(static_cast<float>(node.scale[0]),
                                    static_cast<float>(node.scale[1]),
                                    static_cast<float>(node.scale[2])));
    }
    return m;
}

void appendPrimitive(const tinygltf::Model& model,
                     const tinygltf::Primitive& primitive,
                     const glm::mat4& worldMatrix,
                     const std::string& baseDir,
                     const std::string& nodeName,
                     std::vector<GltfLoader::Submesh>& out) {

    // Triangles only. Fans, strips and point/line modes are not something the
    // renderer can draw, so they are skipped loudly rather than silently
    // reinterpreted as triangles.
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        std::cerr << "[GltfLoader] Skipping non-triangle primitive (mode "
                  << primitive.mode << ") in '" << nodeName << "'." << std::endl;
        return;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) return;

    const tinygltf::Accessor& posAccessor = model.accessors[static_cast<size_t>(positionIt->second)];
    const size_t vertexCount = posAccessor.count;
    if (vertexCount == 0) return;

    GltfLoader::Submesh submesh;
    submesh.name = nodeName;
    submesh.mesh.vertices.resize(vertexCount);

    size_t posStride = 0;
    const auto* positions = accessorData<float>(model, posAccessor, posStride);

    const float* normals = nullptr;
    size_t normalStride = 0;
    if (const auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end()) {
        normals = accessorData<float>(model, model.accessors[static_cast<size_t>(it->second)], normalStride);
    }

    const float* uvs = nullptr;
    size_t uvStride = 0;
    if (const auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(it->second)];
        // Only float UVs are handled; normalised byte/short variants are rare
        // and would need unpacking.
        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            uvs = accessorData<float>(model, acc, uvStride);
        }
    }

    // Normals must be transformed by the inverse-transpose, not the matrix, or
    // non-uniform scale shears them away from the surface.
    const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(worldMatrix)));

    for (size_t i = 0; i < vertexCount; ++i) {
        Vertex& v = submesh.mesh.vertices[i];

        const auto* p = reinterpret_cast<const float*>(
            reinterpret_cast<const uint8_t*>(positions) + i * posStride);
        v.pos = glm::vec3(worldMatrix * glm::vec4(p[0], p[1], p[2], 1.0f));

        if (normals) {
            const auto* n = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(normals) + i * normalStride);
            v.normal = glm::normalize(normalMatrix * glm::vec3(n[0], n[1], n[2]));
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (uvs) {
            const auto* t = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(uvs) + i * uvStride);
            v.texCoord = glm::vec2(t[0], t[1]);
        } else {
            v.texCoord = glm::vec2(0.0f);
        }

        v.color = glm::vec3(1.0f);
    }

    // Indices. glTF permits ubyte/ushort/uint; all widen to uint32 here.
    if (primitive.indices >= 0) {
        const tinygltf::Accessor& idxAccessor = model.accessors[static_cast<size_t>(primitive.indices)];
        submesh.mesh.indices.reserve(idxAccessor.count);

        size_t idxStride = 0;
        const auto* base = accessorData<uint8_t>(model, idxAccessor, idxStride);

        for (size_t i = 0; i < idxAccessor.count; ++i) {
            const uint8_t* element = base + i * idxStride;
            uint32_t index = 0;
            switch (idxAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    index = *element; break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    index = *reinterpret_cast<const uint16_t*>(element); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    index = *reinterpret_cast<const uint32_t*>(element); break;
                default:
                    continue;
            }
            if (index < vertexCount) {
                submesh.mesh.indices.push_back(index);
            }
        }
    } else {
        // Non-indexed primitive: synthesise a sequential index list.
        submesh.mesh.indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            submesh.mesh.indices[i] = static_cast<uint32_t>(i);
        }
    }

    if (submesh.mesh.indices.empty()) return;

    // Material: base colour factor, and the base colour texture resolved to a
    // path on disk that TextureRegistry can open.
    if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
        const tinygltf::Material& material = model.materials[static_cast<size_t>(primitive.material)];
        const auto& pbr = material.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4) {
            submesh.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3]));
        }
        submesh.roughness = static_cast<float>(pbr.roughnessFactor);
        submesh.metallic = static_cast<float>(pbr.metallicFactor);

        if (pbr.baseColorTexture.index >= 0 &&
            pbr.baseColorTexture.index < static_cast<int>(model.textures.size())) {
            const tinygltf::Texture& tex = model.textures[static_cast<size_t>(pbr.baseColorTexture.index)];
            if (tex.source >= 0 && tex.source < static_cast<int>(model.images.size())) {
                const std::string& uri = model.images[static_cast<size_t>(tex.source)].uri;
                if (!uri.empty()) {
                    submesh.albedoTexturePath = (fs::path(baseDir) / uri).lexically_normal().string();
                } else {
                    // Embedded/GLB image data: not written out to disk, so there
                    // is nothing for the path-based texture cache to open.
                    std::cerr << "[GltfLoader] '" << nodeName
                              << "' uses an embedded image, which is not imported yet." << std::endl;
                }
            }
        }
    }

    submesh.mesh.computeBounds();
    out.push_back(std::move(submesh));
}

void visitNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix,
               const std::string& baseDir, std::vector<GltfLoader::Submesh>& out,
               std::vector<bool>& visited) {

    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) return;

    // Defensive: a malformed file can describe a cycle, which would recurse
    // until the stack runs out.
    if (visited[static_cast<size_t>(nodeIndex)]) return;
    visited[static_cast<size_t>(nodeIndex)] = true;

    const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];
    const glm::mat4 world = parentMatrix * nodeLocalMatrix(node);

    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh& mesh = model.meshes[static_cast<size_t>(node.mesh)];
        const std::string name = !node.name.empty() ? node.name
                               : (!mesh.name.empty() ? mesh.name : "GltfMesh");
        for (const auto& primitive : mesh.primitives) {
            appendPrimitive(model, primitive, world, baseDir, name, out);
        }
    }

    for (const int child : node.children) {
        visitNode(model, child, world, baseDir, out, visited);
    }
}

} // namespace

GltfLoader::Scene GltfLoader::Load(const std::string& path) {
    Scene scene;

    if (!fs::exists(path)) {
        scene.error = "no such file: " + path;
        return scene;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    const std::string extension = fs::path(path).extension().string();
    const bool binary = extension == ".glb" || extension == ".GLB";

    const bool loaded = binary
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) {
        std::cerr << "[GltfLoader] " << path << ": " << warn << std::endl;
    }
    if (!loaded) {
        scene.error = err.empty() ? ("could not parse " + path) : err;
        return scene;
    }

    const std::string baseDir = fs::path(path).parent_path().string();
    std::vector<bool> visited(model.nodes.size(), false);

    // Walk the default scene's node graph so each primitive comes out already
    // placed by its parent chain.
    if (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size())) {
        for (const int root : model.scenes[static_cast<size_t>(model.defaultScene)].nodes) {
            visitNode(model, root, glm::mat4(1.0f), baseDir, scene.submeshes, visited);
        }
    } else if (!model.scenes.empty()) {
        for (const int root : model.scenes[0].nodes) {
            visitNode(model, root, glm::mat4(1.0f), baseDir, scene.submeshes, visited);
        }
    } else {
        // No scene description at all: fall back to every node in the file.
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
            visitNode(model, i, glm::mat4(1.0f), baseDir, scene.submeshes, visited);
        }
    }

    if (scene.submeshes.empty()) {
        scene.error = path + " contains no drawable triangle geometry";
        return scene;
    }

    size_t triangles = 0;
    for (const auto& sub : scene.submeshes) triangles += sub.mesh.indices.size() / 3;

    scene.ok = true;
    std::cout << "[GltfLoader] Loaded " << path << " (" << scene.submeshes.size()
              << " primitives, " << triangles << " triangles)." << std::endl;
    return scene;
}

} // namespace Supersonic
