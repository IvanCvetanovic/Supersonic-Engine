#include "core/GltfLoader.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <iostream>
#include <unordered_map>

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

GltfLoader::PackedMap GltfLoader::ChoosePackedMap(const std::string& metallicRoughnessPath,
                                                  const std::string& occlusionPath,
                                                  float gltfOcclusionStrength) {
    PackedMap packed{};

    if (!metallicRoughnessPath.empty()) {
        packed.path = metallicRoughnessPath;

        // The red channel is occlusion only when an occlusion texture vouched
        // for it, and vouching means naming the SAME image. Otherwise red is
        // whatever the exporter happened to leave there - legally anything,
        // routinely zero - and believing it turns the ambient term off for the
        // whole surface.
        if (occlusionPath == metallicRoughnessPath) {
            packed.occlusionStrength = gltfOcclusionStrength;
        }
        return packed;
    }

    // Occlusion on its own cannot be packed here, and taking it anyway would be
    // worse than taking nothing: an AO bake is greyscale, so its green and blue
    // would drive roughness and metallic too, and every crevice would come out
    // smoother and less dielectric than the surface around it. On metal that is
    // not a subtle wrongness. The material keeps its constants instead.
    return packed;
}

namespace {

// Matches the palette slice a single draw can address. Vertex::jointIndices is
// 8-bit, so 255 is the hard ceiling; 128 leaves headroom and keeps a single
// rig's slice small enough that several fit in one frame's palette.
constexpr uint32_t jointLimit = 128;

// An image carried inside the file, written out beside the cache so the rest of
// the engine can go on treating every texture as a path.
//
// A .glb keeps its images as bytes in the binary chunk, and the texture
// registry opens files: it is keyed by path, it hot-reloads by watching a path,
// and MaterialComponent serialises a path. So the usual single-file export -
// which is what most exporters produce by default - arrived with no textures at
// all, and the importer said so and moved on.
//
// The bytes are copied VERBATIM rather than decoded and re-encoded. They are
// already a PNG or a JPEG, tinygltf is built with TINYGLTF_NO_STB_IMAGE and so
// never decodes them, and stb_image is going to decode them again on the way to
// the GPU regardless - so a decode here would cost time to produce a
// byte-for-byte worse copy of a file that already exists.
//
// Into cache/ because that is where per-machine build artefacts already live
// and it is already in .gitignore. Extracting next to the model would put
// generated files in the user's asset folder, and extracting to a temp
// directory would defeat the hot reload that watching a real path buys.
std::string extractEmbeddedImage(const tinygltf::Model& model, int imageIndex,
                                 const std::string& sourcePath, const char* slot) {
    if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size())) return {};
    const tinygltf::Image& image = model.images[static_cast<size_t>(imageIndex)];

    if (!image.uri.empty()) {
        // A data: URI is embedded too, but base64 inside the JSON rather than
        // bytes in the binary chunk. Not handled - and reported, because the
        // path-joining branch this used to fall into produced a "texture path"
        // several kilobytes long that could only ever fail to open.
        if (image.uri.rfind("data:", 0) == 0) {
            SUPERSONIC_LOG_ERROR("GltfLoader")
                << "A " << slot << " image is a base64 data URI, which is not imported yet."
                << std::endl;
        }
        return {};
    }

    if (image.bufferView < 0 || image.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return {};
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(image.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return {};

    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];
    if (view.byteOffset + view.byteLength > buffer.data.size() || view.byteLength == 0) {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "The " << slot << " image names bytes outside its buffer; skipping it."
            << std::endl;
        return {};
    }

    const char* extension = nullptr;
    if (image.mimeType == "image/png") extension = ".png";
    else if (image.mimeType == "image/jpeg") extension = ".jpg";
    else {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "The " << slot << " image is '" << image.mimeType
            << "', which stb_image cannot open; skipping it." << std::endl;
        return {};
    }

    std::error_code ec;
    const fs::path outDir = fs::path("cache") / "gltf";
    fs::create_directories(outDir, ec);
    if (ec) {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "Could not create " << outDir.string() << ": " << ec.message() << std::endl;
        return {};
    }

    // The stem alone is not a key. assets/models/enemies/character.glb and
    // assets/models/npcs/character.glb share one, so the second would extract
    // over the first - and with the freshness check below that is worse than
    // last-write-wins: load the OLDER model second, find a cache file newer
    // than its own source, reuse it, and render one model with the other's
    // texture, silently. The within-file collision was designed against from
    // the start; this is the across-file one.
    //
    // FNV-1a over the path AS ADDRESSED - relative to the asset root, with
    // forward slashes - and deliberately NOT over the absolute path.
    //
    // This is not a detail. A material serialises the extracted texture's path,
    // so that name has to come out the same on the machine that authored the
    // scene and in the folder the game ships to. Hashing the canonical absolute
    // path made it a function of where the project happened to sit: the editor
    // wrote cache/gltf/model-81a8d58a-image0.png into the scene, the packaged
    // copy of the same model extracted itself to ...-997b03a7-image0.png, and
    // the game rendered the missing-texture checkerboard. Verified by packaging
    // it and running the result, which is the only way that shows up.
    //
    // Not a hash anyone should rely on for anything but telling two asset paths
    // apart, which is all this is.
    const std::string key = fs::path(sourcePath).lexically_normal().generic_string();
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : key) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }

    char discriminator[17];
    std::snprintf(discriminator, sizeof(discriminator), "%016llx",
                  static_cast<unsigned long long>(hash));

    const std::string stem = fs::path(sourcePath).stem().string();
    const fs::path outPath = outDir / (stem + "-" + std::string(discriminator, 8) +
                                       "-image" + std::to_string(imageIndex) + extension);

    // Reuse an extraction that is at least as new as the model it came from.
    // Re-exporting the .glb makes it older and the bytes are written again, so
    // hot reload still reaches an embedded texture.
    if (fs::exists(outPath, ec)) {
        std::error_code srcEc, dstEc;
        const auto sourceTime = fs::last_write_time(sourcePath, srcEc);
        const auto cachedTime = fs::last_write_time(outPath, dstEc);
        if (!srcEc && !dstEc && cachedTime >= sourceTime) {
            return outPath.lexically_normal().string();
        }
    }

    std::ofstream file(outPath, std::ios::binary);
    if (!file.is_open()) {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "Could not write " << outPath.string() << std::endl;
        return {};
    }
    file.write(reinterpret_cast<const char*>(buffer.data.data() + view.byteOffset),
               static_cast<std::streamsize>(view.byteLength));
    if (!file) {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "Failed writing " << outPath.string() << std::endl;
        return {};
    }
    file.close();

    SUPERSONIC_LOG_INFO("GltfLoader")
        << "Extracted embedded " << slot << " image to " << outPath.string()
        << " (" << view.byteLength << " bytes)." << std::endl;
    return outPath.lexically_normal().string();
}

// Reads one scalar out of an accessor, normalising the component type.
//
// Returns null for an accessor with no buffer view. tinygltf defaults
// Accessor::bufferView to -1, and the cast to size_t made that index element
// SIZE_MAX - latent while only positions and UVs came through here, and reached
// the moment inverse bind matrices and four animation samplers did too.
template <typename T>
const T* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t& strideOut) {
    strideOut = 0;
    if (accessor.bufferView < 0 ||
        static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        return nullptr;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
        return nullptr;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];

    const size_t elementSize = static_cast<size_t>(
        tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType)) *
        tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type)));

    const size_t offset = view.byteOffset + accessor.byteOffset;
    if (offset > buffer.data.size()) return nullptr;

    strideOut = view.byteStride != 0 ? view.byteStride : elementSize;
    return reinterpret_cast<const T*>(buffer.data.data() + offset);
}

// One component, widened to float and de-normalised where the spec says it is
// normalised. Animation outputs and vertex weights are both allowed to be
// normalised integers, and reading them as raw floats yields garbage.
float componentAsFloat(const uint8_t* element, int componentType, size_t component, bool normalized) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return reinterpret_cast<const float*>(element)[component];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const float v = static_cast<float>(element[component]);
            return normalized ? v / 255.0f : v;
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            const float v = static_cast<float>(reinterpret_cast<const int8_t*>(element)[component]);
            return normalized ? std::max(v / 127.0f, -1.0f) : v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const float v = static_cast<float>(reinterpret_cast<const uint16_t*>(element)[component]);
            return normalized ? v / 65535.0f : v;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            const float v = static_cast<float>(reinterpret_cast<const int16_t*>(element)[component]);
            return normalized ? std::max(v / 32767.0f, -1.0f) : v;
        }
        default:
            return 0.0f;
    }
}

// One component read as an integer, for joint indices. Never float in practice -
// glTF says JOINTS_0 is UNSIGNED_BYTE or UNSIGNED_SHORT - which is why copying
// the float-only guard used for UVs would leave every joint index at zero, every
// weight summing to zero, and every vertex collapsed onto the origin.
uint32_t componentAsIndex(const uint8_t* element, int componentType, size_t component) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return element[component];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return reinterpret_cast<const uint16_t*>(element)[component];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return reinterpret_cast<const uint32_t*>(element)[component];
        default:
            return 0;
    }
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
                     const std::string& sourcePath,
                     const std::string& nodeName,
                     std::vector<GltfLoader::Submesh>& out,
                    int32_t skinIndex) {

    // Triangles only. Fans, strips and point/line modes are not something the
    // renderer can draw, so they are skipped loudly rather than silently
    // reinterpreted as triangles.
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "Skipping non-triangle primitive (mode "
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
    submesh.skinIndex = skinIndex;
    submesh.mesh.vertices.resize(vertexCount);

    size_t posStride = 0;
    const auto* positions = accessorData<float>(model, posAccessor, posStride);
    if (!positions) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "'" << nodeName << "' has an unreadable POSITION accessor." << std::endl;
        return;
    }

    const float* normals = nullptr;
    size_t normalStride = 0;
    if (const auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end()) {
        normals = accessorData<float>(model, model.accessors[static_cast<size_t>(it->second)], normalStride);
    }

    const float* tangents = nullptr;
    size_t tangentStride = 0;
    if (const auto it = primitive.attributes.find("TANGENT"); it != primitive.attributes.end()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(it->second)];
        // glTF TANGENT is vec4: xyz plus a handedness sign in w, which is
        // exactly the layout Vertex::tangent uses.
        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc.type == TINYGLTF_TYPE_VEC4) {
            tangents = accessorData<float>(model, acc, tangentStride);
        }
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

    // Skinning influences. Deliberately NOT gated on the component type being
    // float: JOINTS_0 never is, and WEIGHTS_0 frequently is not either.
    const uint8_t* jointBytes = nullptr;
    size_t jointStride = 0;
    int jointComponentType = 0;
    if (const auto it = primitive.attributes.find("JOINTS_0"); it != primitive.attributes.end()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(it->second)];
        jointBytes = accessorData<uint8_t>(model, acc, jointStride);
        jointComponentType = acc.componentType;
    }

    const uint8_t* weightBytes = nullptr;
    size_t weightStride = 0;
    int weightComponentType = 0;
    bool weightNormalized = false;
    if (const auto it = primitive.attributes.find("WEIGHTS_0"); it != primitive.attributes.end()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(it->second)];
        weightBytes = accessorData<uint8_t>(model, acc, weightStride);
        weightComponentType = acc.componentType;
        weightNormalized = acc.normalized;
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

        if (tangents) {
            const auto* t = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(tangents) + i * tangentStride);
            // The tangent is a direction, so it takes the world matrix; the
            // handedness in w is a sign and must pass through untouched.
            const glm::vec3 worldTangent = glm::vec3(worldMatrix * glm::vec4(t[0], t[1], t[2], 0.0f));
            const float length = glm::length(worldTangent);
            v.tangent = glm::vec4(length > 1e-8f ? worldTangent / length : glm::vec3(1.0f, 0.0f, 0.0f),
                                  t[3] < 0.0f ? -1.0f : 1.0f);
        }

        if (jointBytes && weightBytes) {
            const uint8_t* j = jointBytes + i * jointStride;
            const uint8_t* w = weightBytes + i * weightStride;

            glm::vec4 weights(0.0f);
            for (size_t c = 0; c < 4; ++c) {
                const uint32_t index = componentAsIndex(j, jointComponentType, c);
                // Clamped, not trusted. robustBufferAccess is not enabled on
                // this device, so an out-of-range palette read is undefined
                // behaviour - a device loss, not a zeroed lookup.
                v.jointIndices[static_cast<glm::length_t>(c)] =
                    static_cast<uint8_t>(index < jointLimit ? index : 0u);
                weights[static_cast<glm::length_t>(c)] =
                    componentAsFloat(w, weightComponentType, c, weightNormalized);
            }

            // Renormalise. Quantised weights rarely sum to exactly one, and the
            // error shows up as a mesh that subtly inflates or shrinks.
            const float sum = weights.x + weights.y + weights.z + weights.w;
            v.jointWeights = sum > 1e-6f ? weights / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }

        v.color = glm::vec3(1.0f);
    }

    // Indices. glTF permits ubyte/ushort/uint; all widen to uint32 here.
    if (primitive.indices >= 0) {
        const tinygltf::Accessor& idxAccessor = model.accessors[static_cast<size_t>(primitive.indices)];
        submesh.mesh.indices.reserve(idxAccessor.count);

        size_t idxStride = 0;
        const auto* base = accessorData<uint8_t>(model, idxAccessor, idxStride);
        if (!base) return;

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

    // Material.
    //
    // Everything here used to be resolved and then thrown away one caller up -
    // MeshRegistry copied the geometry out of the submesh and dropped the rest -
    // so it was worth reading only the base colour. Now that it survives, the
    // rest of what the format actually says is worth reading too: the normal
    // map, the emissive term, and whether the surface is meant to blend.
    if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
        const tinygltf::Material& material = model.materials[static_cast<size_t>(primitive.material)];
        const auto& pbr = material.pbrMetallicRoughness;

        submesh.material.present = true;

        if (pbr.baseColorFactor.size() == 4) {
            submesh.material.baseColor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3]));
        }
        submesh.material.roughness = static_cast<float>(pbr.roughnessFactor);
        submesh.material.metallic = static_cast<float>(pbr.metallicFactor);

        // One resolver for both maps. It used to exist once, inline, for the
        // base colour only - which is how the normal map came to be the thing
        // the renderer had a descriptor slot for and the importer never filled.
        const auto resolveTexture = [&](int textureIndex, const char* slot) -> std::string {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
                return {};
            }
            const tinygltf::Texture& tex = model.textures[static_cast<size_t>(textureIndex)];
            if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) return {};

            const std::string& uri = model.images[static_cast<size_t>(tex.source)].uri;
            if (uri.empty() || uri.rfind("data:", 0) == 0) {
                // Carried inside the file rather than beside it, which is what
                // a .glb always does. Written out to the cache so the rest of
                // the engine can go on treating a texture as a path.
                return extractEmbeddedImage(model, tex.source, sourcePath, slot);
            }
            return (fs::path(sourcePath).parent_path() / uri).lexically_normal().string();
        };

        submesh.material.albedoTexturePath =
            resolveTexture(pbr.baseColorTexture.index, "base colour");
        submesh.material.normalTexturePath =
            resolveTexture(material.normalTexture.index, "normal");

        // glTF keeps occlusion and metallic-roughness as two SLOTS and expects
        // them packed into one image: occlusion in R, roughness in G, metallic
        // in B. Almost every exporter points both slots at that same image,
        // which is the arrangement this engine's single packed binding is for.
        //
        // Compared as RESOLVED PATHS, not as texture indices. Two texture
        // entries can name one image and differ only in their sampler, and the
        // question here is whether the same PIXELS carry both.
        const std::string metallicRoughnessPath =
            resolveTexture(pbr.metallicRoughnessTexture.index, "metallic-roughness");
        const std::string occlusionPath =
            resolveTexture(material.occlusionTexture.index, "occlusion");

        const GltfLoader::PackedMap packed = GltfLoader::ChoosePackedMap(
            metallicRoughnessPath, occlusionPath,
            static_cast<float>(material.occlusionTexture.strength));
        submesh.material.ormTexturePath = packed.path;
        submesh.material.occlusionStrength = packed.occlusionStrength;

        // Said out loud in the two cases where something the file asked for is
        // not going to happen. The decision itself is in ChoosePackedMap, which
        // has no opinion about logging and can therefore be tested.
        if (!metallicRoughnessPath.empty() && !occlusionPath.empty() &&
            occlusionPath != metallicRoughnessPath) {
            SUPERSONIC_LOG_WARN("GltfLoader")
                << sourcePath << ": material '" << material.name
                << "' puts occlusion in a different image from metallic-roughness. "
                << "This engine packs all three channels into one map, so the "
                << "metallic-roughness image is used and the occlusion is dropped "
                << "rather than read out of a channel that does not hold it.";
        } else if (metallicRoughnessPath.empty() && !occlusionPath.empty()) {
            SUPERSONIC_LOG_WARN("GltfLoader")
                << sourcePath << ": material '" << material.name
                << "' has an occlusion texture but no metallic-roughness one. "
                << "This engine packs all three into a single map, and a greyscale "
                << "occlusion image would drive roughness and metallic as well, so "
                << "it is not imported; the material keeps its constants.";
        }

        if (material.emissiveFactor.size() == 3) {
            submesh.material.emissiveColor = glm::vec3(
                static_cast<float>(material.emissiveFactor[0]),
                static_cast<float>(material.emissiveFactor[1]),
                static_cast<float>(material.emissiveFactor[2]));
            // The engine splits emission into a colour and a strength, so a
            // non-black factor means "emitting, at unit strength" unless
            // KHR_materials_emissive_strength says otherwise below.
            if (glm::dot(submesh.material.emissiveColor, submesh.material.emissiveColor) > 0.0f) {
                submesh.material.emissiveStrength = 1.0f;
            }
        }

        // The one extension worth honouring here: without it an emissive factor
        // is clamped to 1.0 and can never trip the bloom threshold, which is
        // the entire point of authoring one.
        if (const auto it = material.extensions.find("KHR_materials_emissive_strength");
            it != material.extensions.end() && it->second.Has("emissiveStrength")) {
            const auto& value = it->second.Get("emissiveStrength");
            if (value.IsNumber()) {
                submesh.material.emissiveStrength = static_cast<float>(value.GetNumberAsDouble());
            }
        }

        // BLEND sorts; MASK cuts. They are different requests and mapping the
        // second onto the first is what makes foliage sort against itself.
        submesh.material.transparent = material.alphaMode == "BLEND";
        if (material.alphaMode == "MASK") {
            // The spec's default when the material omits it. A cutoff of zero
            // would mean "no cutout" here, so a file asking for MASK with an
            // explicit 0.0 gets the smallest cut that is still a cut.
            const float cutoff = static_cast<float>(material.alphaCutoff);
            submesh.material.alphaCutoff = cutoff > 0.0f ? cutoff : 0.5f;
        }
    }

    // A file that supplies tangents keeps them; one that does not gets a basis
    // derived from its UVs, so normal mapping works either way.
    if (!tangents) {
        submesh.mesh.computeTangents();
    }
    submesh.mesh.computeBounds();
    out.push_back(std::move(submesh));
}


// ---------------------------------------------------------------------------
// Skins and animations
// ---------------------------------------------------------------------------

// Rest transform of a node, as separate components so an animation can drive
// one of them and leave the others alone.
void nodeRestTrs(const tinygltf::Node& node, glm::vec3& translation, glm::quat& rotation,
                 glm::vec3& scale) {
    translation = glm::vec3(0.0f);
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    scale = glm::vec3(1.0f);

    if (node.matrix.size() == 16) {
        // A node given as a matrix still has to yield TRS, because a rotation
        // channel replaces only the rotation.
        const glm::mat4 m = nodeLocalMatrix(node);
        translation = glm::vec3(m[3]);

        glm::vec3 columns[3] = { glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2]) };
        for (int i = 0; i < 3; ++i) {
            scale[i] = glm::length(columns[i]);
            if (scale[i] > 1e-8f) columns[i] /= scale[i];
        }
        rotation = glm::quat_cast(glm::mat3(columns[0], columns[1], columns[2]));
        return;
    }

    if (node.translation.size() == 3) {
        translation = glm::vec3(static_cast<float>(node.translation[0]),
                                static_cast<float>(node.translation[1]),
                                static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        // glTF stores quaternions xyzw; glm::quat is constructed wxyz.
        rotation = glm::quat(static_cast<float>(node.rotation[3]),
                             static_cast<float>(node.rotation[0]),
                             static_cast<float>(node.rotation[1]),
                             static_cast<float>(node.rotation[2]));
    }
    if (node.scale.size() == 3) {
        scale = glm::vec3(static_cast<float>(node.scale[0]),
                          static_cast<float>(node.scale[1]),
                          static_cast<float>(node.scale[2]));
    }
}

// node index -> its parent node index, or -1.
std::vector<int> buildParentTable(const tinygltf::Model& model) {
    std::vector<int> parents(model.nodes.size(), -1);
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        for (const int child : model.nodes[i].children) {
            if (child >= 0 && static_cast<size_t>(child) < parents.size()) {
                parents[static_cast<size_t>(child)] = static_cast<int>(i);
            }
        }
    }
    return parents;
}

// One skin -> one Skeleton, with joints reordered parent-before-child and the
// parent indices remapped to match.
//
// skin.joints comes in whatever order the exporter felt like, so evaluating a
// pose as a single forward pass over it reads an uninitialised parent for any
// file that lists a child first. Sorting by depth in the node hierarchy fixes
// that for every valid file, because a node is always deeper than its parent.
Skeleton buildSkeleton(const tinygltf::Model& model, const tinygltf::Skin& skin,
                       const std::vector<int>& parents,
                       std::unordered_map<int, int32_t>& outNodeToJoint) {
    Skeleton skeleton;
    outNodeToJoint.clear();
    if (skin.joints.empty()) return skeleton;

    std::vector<int> jointNodes;
    jointNodes.reserve(skin.joints.size());
    for (const int node : skin.joints) {
        if (node < 0 || static_cast<size_t>(node) >= model.nodes.size()) continue;
        if (jointNodes.size() >= jointLimit) {
            SUPERSONIC_LOG_ERROR("GltfLoader") << "Skin '" << skin.name << "' has more than " << jointLimit
                      << " joints; the rest are ignored." << std::endl;
            break;
        }
        jointNodes.push_back(node);
    }
    if (jointNodes.empty()) return skeleton;

    std::unordered_map<int, size_t> originalIndexOf;
    for (size_t i = 0; i < jointNodes.size(); ++i) originalIndexOf.emplace(jointNodes[i], i);

    const auto depthOf = [&](int node) {
        int depth = 0;
        int current = node;
        while (current >= 0 && depth < 1024) {
            current = parents[static_cast<size_t>(current)];
            ++depth;
        }
        return depth;
    };

    std::vector<int> ordered = jointNodes;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](int a, int b) { return depthOf(a) < depthOf(b); });

    std::unordered_map<int, int32_t> jointOf;
    for (size_t i = 0; i < ordered.size(); ++i) {
        jointOf.emplace(ordered[i], static_cast<int32_t>(i));
    }

    // Inverse bind matrices are indexed by the ORIGINAL skin.joints order, so
    // they have to be permuted alongside the sort rather than read positionally.
    const float* inverseBinds = nullptr;
    size_t inverseBindStride = 0;
    if (skin.inverseBindMatrices >= 0 &&
        static_cast<size_t>(skin.inverseBindMatrices) < model.accessors.size()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(skin.inverseBindMatrices)];
        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc.type == TINYGLTF_TYPE_MAT4) {
            inverseBinds = accessorData<float>(model, acc, inverseBindStride);
        }
    }

    skeleton.joints.resize(ordered.size());
    for (size_t i = 0; i < ordered.size(); ++i) {
        const int nodeIndex = ordered[i];
        const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];

        Joint& joint = skeleton.joints[i];
        joint.name = node.name;
        nodeRestTrs(node, joint.restTranslation, joint.restRotation, joint.restScale);

        // Nearest ancestor that is itself a joint. Everything between the two is
        // folded into preTransform: glTF does not require a joint's parent node
        // to be a joint, and dropping those nodes puts the whole rig in the
        // wrong place.
        int ancestor = parents[static_cast<size_t>(nodeIndex)];
        glm::mat4 preTransform(1.0f);
        int guard = 0;
        while (ancestor >= 0 && guard++ < 1024) {
            if (const auto it = jointOf.find(ancestor); it != jointOf.end()) {
                joint.parent = it->second;
                break;
            }
            preTransform = nodeLocalMatrix(model.nodes[static_cast<size_t>(ancestor)]) * preTransform;
            ancestor = parents[static_cast<size_t>(ancestor)];
        }
        joint.preTransform = preTransform;

        if (inverseBinds) {
            const size_t original = originalIndexOf[nodeIndex];
            const auto* m = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(inverseBinds) + original * inverseBindStride);
            joint.inverseBind = glm::make_mat4(m);
        }
    }

    outNodeToJoint = jointOf;
    return skeleton;
}

AnimInterpolation interpolationFrom(const std::string& name) {
    if (name == "STEP") return AnimInterpolation::Step;
    if (name == "CUBICSPLINE") return AnimInterpolation::CubicSpline;
    return AnimInterpolation::Linear;
}

// Every animation in the file, with node targets remapped to joint indices of
// the given skin. Channels aimed at anything else are dropped.
std::vector<AnimationClip> buildClips(const tinygltf::Model& model,
                                      const std::unordered_map<int, int32_t>& nodeToJoint) {
    std::vector<AnimationClip> clips;
    clips.reserve(model.animations.size());

    for (size_t animIndex = 0; animIndex < model.animations.size(); ++animIndex) {
        const tinygltf::Animation& animation = model.animations[animIndex];

        AnimationClip clip;
        clip.name = animation.name.empty() ? ("Clip" + std::to_string(animIndex)) : animation.name;

        for (const auto& channel : animation.channels) {
            const auto jointIt = nodeToJoint.find(channel.target_node);
            if (jointIt == nodeToJoint.end()) continue;

            if (channel.sampler < 0 ||
                static_cast<size_t>(channel.sampler) >= animation.samplers.size()) continue;
            const tinygltf::AnimationSampler& sampler =
                animation.samplers[static_cast<size_t>(channel.sampler)];

            AnimChannel out;
            out.joint = jointIt->second;
            out.interpolation = interpolationFrom(sampler.interpolation);

            if (channel.target_path == "translation")   out.path = AnimPath::Translation;
            else if (channel.target_path == "rotation") out.path = AnimPath::Rotation;
            else if (channel.target_path == "scale")    out.path = AnimPath::Scale;
            else continue;  // morph weights are not supported

            if (sampler.input < 0 || static_cast<size_t>(sampler.input) >= model.accessors.size()) continue;
            if (sampler.output < 0 || static_cast<size_t>(sampler.output) >= model.accessors.size()) continue;

            const tinygltf::Accessor& inputAcc = model.accessors[static_cast<size_t>(sampler.input)];
            const tinygltf::Accessor& outputAcc = model.accessors[static_cast<size_t>(sampler.output)];

            size_t inputStride = 0;
            const auto* times = accessorData<uint8_t>(model, inputAcc, inputStride);
            size_t outputStride = 0;
            const auto* values = accessorData<uint8_t>(model, outputAcc, outputStride);
            if (!times || !values || inputAcc.count == 0) continue;

            out.times.resize(inputAcc.count);
            for (size_t k = 0; k < inputAcc.count; ++k) {
                out.times[k] = componentAsFloat(times + k * inputStride,
                                                inputAcc.componentType, 0, inputAcc.normalized);
            }

            const size_t components = out.path == AnimPath::Rotation ? 4u : 3u;
            out.values.resize(outputAcc.count);
            for (size_t k = 0; k < outputAcc.count; ++k) {
                const uint8_t* element = values + k * outputStride;
                glm::vec4 value(0.0f);
                for (size_t c = 0; c < components; ++c) {
                    value[static_cast<glm::length_t>(c)] =
                        componentAsFloat(element, outputAcc.componentType, c, outputAcc.normalized);
                }
                out.values[k] = value;
            }

            // CUBICSPLINE stores in-tangent, value and out-tangent per key, so
            // the output accessor is three times the key count. A mismatch means
            // a malformed file and the channel is unusable.
            const size_t expected = out.interpolation == AnimInterpolation::CubicSpline
                                  ? out.times.size() * 3 : out.times.size();
            if (out.values.size() < expected) continue;

            clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
        }

        if (!clip.channels.empty()) clips.push_back(std::move(clip));
    }
    return clips;
}

void visitNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix,
               const std::string& sourcePath, std::vector<GltfLoader::Submesh>& out,
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
            // A skinned primitive is handed the IDENTITY, not its node's world
            // matrix: the inverse bind matrices are authored in the skin's own
            // space, and the glTF spec requires the skinned mesh node's own
            // transform to be ignored. Baking it in transforms the mesh twice.
            const int32_t skin = static_cast<int32_t>(node.skin);
            const glm::mat4 primitiveMatrix = skin >= 0 ? glm::mat4(1.0f) : world;
            appendPrimitive(model, primitive, primitiveMatrix, sourcePath, name, out, skin);
        }
    }

    for (const int child : node.children) {
        visitNode(model, child, world, sourcePath, out, visited);
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

    // Built with TINYGLTF_NO_STB_IMAGE, so there is no decoder registered - and
    // tinygltf treats that as a hard error the moment a file carries an image
    // rather than naming one, which is every .glb. It refused to load the file
    // AT ALL: no meshes, no skins, no animations, over a texture.
    //
    // A no-op that reports success is exactly right here, because this importer
    // never wants the pixels. It copies the encoded bytes out of the buffer
    // view verbatim, and ParseImage leaves image.bufferView intact whatever the
    // callback does with them.
    loader.SetImageLoader(
        [](tinygltf::Image*, const int, std::string*, std::string*, int, int,
           const unsigned char*, int, void*) { return true; },
        nullptr);
    std::string err;
    std::string warn;

    const std::string extension = fs::path(path).extension().string();
    const bool binary = extension == ".glb" || extension == ".GLB";

    const bool loaded = binary
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) {
        SUPERSONIC_LOG_WARN("GltfLoader") << path << ": " << warn << std::endl;
    }
    if (!loaded) {
        scene.error = err.empty() ? ("could not parse " + path) : err;
        return scene;
    }


    std::vector<bool> visited(model.nodes.size(), false);

    // Skins first, because the node walk records a skin index per primitive and
    // the clips are remapped onto the first skin's joints.
    const std::vector<int> parents = buildParentTable(model);
    std::unordered_map<int, int32_t> nodeToJoint;
    for (size_t i = 0; i < model.skins.size(); ++i) {
        std::unordered_map<int, int32_t> skinNodeToJoint;
        Skeleton skeleton = buildSkeleton(model, model.skins[i], parents, skinNodeToJoint);
        if (i == 0) nodeToJoint = skinNodeToJoint;
        scene.skeletons.push_back(std::move(skeleton));
    }
    if (!nodeToJoint.empty()) {
        scene.clips = buildClips(model, nodeToJoint);
    }

    // Walk the default scene's node graph so each primitive comes out already
    // placed by its parent chain.
    if (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size())) {
        for (const int root : model.scenes[static_cast<size_t>(model.defaultScene)].nodes) {
            visitNode(model, root, glm::mat4(1.0f), path, scene.submeshes, visited);
        }
    } else if (!model.scenes.empty()) {
        for (const int root : model.scenes[0].nodes) {
            visitNode(model, root, glm::mat4(1.0f), path, scene.submeshes, visited);
        }
    } else {
        // No scene description at all: fall back to every node in the file.
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
            visitNode(model, i, glm::mat4(1.0f), path, scene.submeshes, visited);
        }
    }

    if (scene.submeshes.empty()) {
        scene.error = path + " contains no drawable triangle geometry";
        return scene;
    }

    size_t triangles = 0;
    for (const auto& sub : scene.submeshes) triangles += sub.mesh.indices.size() / 3;

    scene.ok = true;
    SUPERSONIC_LOG_INFO("GltfLoader") << "Loaded " << path << " (" << scene.submeshes.size()
              << " primitives, " << triangles << " triangles)." << std::endl;
    return scene;
}

} // namespace Supersonic
