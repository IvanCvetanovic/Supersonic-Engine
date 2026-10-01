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

// Where an accessor's data starts, or null if the accessor cannot be read.
//
// Null for an accessor with no buffer view. tinygltf defaults
// Accessor::bufferView to -1, and the cast to size_t made that index element
// SIZE_MAX - latent while only positions and UVs came through here, and reached
// the moment inverse bind matrices and four animation samplers did too.
//
// And null for one whose ELEMENTS do not fit the buffer. This used to check only
// that the data STARTED inside it, so an accessor claiming a thousand elements
// of a buffer that holds three was handed back as readable and every caller
// then read `count` elements past the end. tinygltf checks neither that an
// accessor fits its buffer view nor that a view fits its buffer, so this is the
// only place it can be checked. The test is against the BUFFER, not the view:
// that is what memory safety needs, and an exporter whose accessor runs a few
// bytes past a view it declared too short still loads, as it always did.
//
// A caller must still check that `count` is the number of elements IT needs - an
// attribute with fewer elements than the primitive has vertices is in range and
// still too short.
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

    // Both are -1 for a type or component the format does not define, which the
    // cast to size_t used to turn into an element roughly the size of the
    // address space.
    const int componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
    const int componentCount = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
    if (componentSize <= 0 || componentCount <= 0) return nullptr;
    const size_t elementSize = static_cast<size_t>(componentSize) * static_cast<size_t>(componentCount);

    // Written so that no sum can wrap: each term is compared against what is
    // left, never added to something that could already be near SIZE_MAX.
    const size_t bufferSize = buffer.data.size();
    if (view.byteOffset > bufferSize) return nullptr;
    if (accessor.byteOffset > bufferSize - view.byteOffset) return nullptr;
    const size_t offset = view.byteOffset + accessor.byteOffset;

    const size_t stride = view.byteStride != 0 ? view.byteStride : elementSize;

    if (accessor.count > 0) {
        const size_t room = bufferSize - offset;
        // One element, and then (count - 1) strides to the start of the last.
        if (elementSize > room) return nullptr;
        if (accessor.count - 1 > (room - elementSize) / stride) return nullptr;
    }

    strideOut = stride;
    return reinterpret_cast<const T*>(buffer.data.data() + offset);
}

// The accessor an index names, or null if the file names one it does not have.
// Every index in a glTF file is the file's own claim, and a negative one, or one
// past the table, was used to subscript `model.accessors` directly.
const tinygltf::Accessor* accessorAt(const tinygltf::Model& model, int index) {
    if (index < 0 || static_cast<size_t>(index) >= model.accessors.size()) return nullptr;
    return &model.accessors[static_cast<size_t>(index)];
}

// The accessor a primitive names for an attribute, if it is usable for
// `vertexCount` vertices: it exists, and it has an element for every vertex.
//
// Null otherwise, with a warning when the file named something wrong - the
// attribute is then treated as absent, which is what every optional attribute
// already does, rather than read past its end. An attribute with MORE elements
// than vertices is accepted: the surplus is never read.
const tinygltf::Accessor* attributeAccessor(const tinygltf::Model& model,
                                            const tinygltf::Primitive& primitive,
                                            const char* attribute, size_t vertexCount,
                                            const std::string& nodeName) {
    const auto it = primitive.attributes.find(attribute);
    if (it == primitive.attributes.end()) return nullptr;

    const tinygltf::Accessor* accessor = accessorAt(model, it->second);
    if (!accessor) {
        SUPERSONIC_LOG_WARN("GltfLoader") << "'" << nodeName << "' names accessor " << it->second
            << " for " << attribute << ", which does not exist; ignoring it." << std::endl;
        return nullptr;
    }
    if (accessor->count < vertexCount) {
        SUPERSONIC_LOG_WARN("GltfLoader") << "'" << nodeName << "' has " << accessor->count
            << " " << attribute << " elements for " << vertexCount << " vertices; ignoring it."
            << std::endl;
        return nullptr;
    }
    return accessor;
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
                    int32_t skinIndex,
                    int32_t rigidJoint) {

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

    const tinygltf::Accessor* posAccessor = accessorAt(model, positionIt->second);
    if (!posAccessor) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "'" << nodeName << "' names accessor "
            << positionIt->second << " for POSITION, which does not exist." << std::endl;
        return;
    }
    const size_t vertexCount = posAccessor->count;
    if (vertexCount == 0) return;

    // Read as three floats a vertex, so it has to BE three floats a vertex: any
    // other type was reinterpreted, and a SCALAR byte accessor read twelve bytes
    // of a one-byte element. (KHR_mesh_quantization writes other types; this
    // importer does not read them, and says so rather than draw noise.)
    if (posAccessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        posAccessor->type != TINYGLTF_TYPE_VEC3) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "'" << nodeName
            << "' has POSITION data that is not float VEC3, which is the only form read."
            << std::endl;
        return;
    }

    // Before the vertex array is sized, not after: the count is the file's claim,
    // and sizing from it first turned four billion into a length_error - or a
    // 240 GB allocation - from a file of a few hundred bytes. Once the accessor is
    // known to fit the buffer, the count is bounded by what the buffer holds.
    size_t posStride = 0;
    const auto* positions = accessorData<float>(model, *posAccessor, posStride);
    if (!positions) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "'" << nodeName << "' has an unreadable POSITION accessor." << std::endl;
        return;
    }

    GltfLoader::Submesh submesh;
    submesh.name = nodeName;
    submesh.skinIndex = skinIndex;
    submesh.mesh.vertices.resize(vertexCount);

    // Each optional attribute: named correctly, one element per vertex, and in
    // the form it is read in. Anything else is treated as absent.
    const float* normals = nullptr;
    size_t normalStride = 0;
    if (const auto* acc = attributeAccessor(model, primitive, "NORMAL", vertexCount, nodeName)) {
        if (acc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc->type == TINYGLTF_TYPE_VEC3) {
            normals = accessorData<float>(model, *acc, normalStride);
        }
    }

    const float* tangents = nullptr;
    size_t tangentStride = 0;
    if (const auto* acc = attributeAccessor(model, primitive, "TANGENT", vertexCount, nodeName)) {
        // glTF TANGENT is vec4: xyz plus a handedness sign in w, which is
        // exactly the layout Vertex::tangent uses.
        if (acc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc->type == TINYGLTF_TYPE_VEC4) {
            tangents = accessorData<float>(model, *acc, tangentStride);
        }
    }

    const float* uvs = nullptr;
    size_t uvStride = 0;
    if (const auto* acc = attributeAccessor(model, primitive, "TEXCOORD_0", vertexCount, nodeName)) {
        // Only float UVs are handled; normalised byte/short variants are rare
        // and would need unpacking. Two floats an element, so a VEC2.
        if (acc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc->type == TINYGLTF_TYPE_VEC2) {
            uvs = accessorData<float>(model, *acc, uvStride);
        }
    }

    // Baked vertex colour.
    //
    // Deliberately NOT gated on float, unlike UVs above: exporters write
    // COLOR_0 as normalised unsigned byte or short far more often than as
    // float, because it is the one attribute where eight bits is plainly
    // enough. Refusing those would drop the attribute on most of the files
    // that actually carry it.
    //
    // vec3 and vec4 are both legal; the alpha is read and discarded, because
    // Vertex::color is rgb and this engine's opacity comes from the material.
    const uint8_t* colorBytes = nullptr;
    size_t colorStride = 0;
    int colorComponentType = 0;
    bool colorNormalized = false;
    if (const auto* acc = attributeAccessor(model, primitive, "COLOR_0", vertexCount, nodeName)) {
        if (acc->type == TINYGLTF_TYPE_VEC3 || acc->type == TINYGLTF_TYPE_VEC4) {
            colorBytes = accessorData<uint8_t>(model, *acc, colorStride);
            colorComponentType = acc->componentType;
            colorNormalized = acc->normalized;
        }
    }

    // Skinning influences. Deliberately NOT gated on the component type being
    // float: JOINTS_0 never is, and WEIGHTS_0 frequently is not either.
    const uint8_t* jointBytes = nullptr;
    size_t jointStride = 0;
    int jointComponentType = 0;
    //
    // Both are read four components an element, so both have to BE four
    // components an element: as SCALAR the last element read three bytes past the
    // end of the buffer.
    if (const auto* acc = attributeAccessor(model, primitive, "JOINTS_0", vertexCount, nodeName)) {
        if (acc->type == TINYGLTF_TYPE_VEC4) {
            jointBytes = accessorData<uint8_t>(model, *acc, jointStride);
            jointComponentType = acc->componentType;
        }
    }

    const uint8_t* weightBytes = nullptr;
    size_t weightStride = 0;
    int weightComponentType = 0;
    bool weightNormalized = false;
    if (const auto* acc = attributeAccessor(model, primitive, "WEIGHTS_0", vertexCount, nodeName)) {
        if (acc->type == TINYGLTF_TYPE_VEC4) {
            weightBytes = accessorData<uint8_t>(model, *acc, weightStride);
            weightComponentType = acc->componentType;
            weightNormalized = acc->normalized;
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

        if (rigidJoint >= 0 && !(jointBytes && weightBytes)) {
            // RIGID SKINNING. The primitive has no influences of its own, so it
            // is given exactly one: full weight on the node that animates it.
            //
            // This is what turns a node-hierarchy rig into something the
            // existing skinning path can play. The vertices are already baked
            // into world space by visitNode, and the joint's inverse bind is
            // the inverse of that same world matrix - so at rest the two cancel
            // and the mesh sits where it was authored, and in motion the joint
            // carries it.
            v.jointIndices = glm::u8vec4(static_cast<uint8_t>(rigidJoint), 0u, 0u, 0u);
            v.jointWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        } else if (jointBytes && weightBytes) {
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

        // WHITE WHEN ABSENT, which is the identity for a multiply - so a model
        // with no COLOR_0 looks exactly as it did before this existed.
        //
        // Until this read the attribute, every vertex was white unconditionally
        // and any baked lighting in the file was silently discarded. For a
        // model library that paints its shading into the mesh rather than into
        // a texture, that is the whole look thrown away with nothing to point
        // at: the geometry is right, the materials are right, and it is flat.
        if (colorBytes) {
            const uint8_t* c = colorBytes + i * colorStride;
            v.color = glm::vec3(componentAsFloat(c, colorComponentType, 0, colorNormalized),
                                componentAsFloat(c, colorComponentType, 1, colorNormalized),
                                componentAsFloat(c, colorComponentType, 2, colorNormalized));
        } else {
            v.color = glm::vec3(1.0f);
        }
    }

    // Indices. glTF permits ubyte/ushort/uint; all widen to uint32 here.
    if (primitive.indices >= 0) {
        const tinygltf::Accessor* idxAccessor = accessorAt(model, primitive.indices);
        if (!idxAccessor) {
            SUPERSONIC_LOG_ERROR("GltfLoader") << "'" << nodeName << "' names accessor "
                << primitive.indices << " for its indices, which does not exist." << std::endl;
            return;
        }

        // Checked before anything is reserved: the count is the file's claim, and
        // `reserve(count)` of four billion was a crash from a few hundred bytes.
        size_t idxStride = 0;
        const auto* base = accessorData<uint8_t>(model, *idxAccessor, idxStride);
        if (!base) return;
        const int indexType = idxAccessor->componentType;
        if (indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            return;
        }

        // Whole triangles only. An index no vertex answers to used to be dropped
        // ON ITS OWN, which left a triangle short by one and shifted every
        // triangle after it by one index - a file with one bad index drew noise
        // from there to the end. Now the triangle that named it goes, and the rest
        // stay where they were. A trailing one or two indices are not a triangle.
        submesh.mesh.indices.reserve(idxAccessor->count - idxAccessor->count % 3);
        for (size_t first = 0; first + 2 < idxAccessor->count; first += 3) {
            uint32_t triangle[3] = {0, 0, 0};
            bool inRange = true;
            for (size_t corner = 0; corner < 3; ++corner) {
                const uint8_t* element = base + (first + corner) * idxStride;
                switch (indexType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        triangle[corner] = *element; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        triangle[corner] = *reinterpret_cast<const uint16_t*>(element); break;
                    default:
                        triangle[corner] = *reinterpret_cast<const uint32_t*>(element); break;
                }
                if (triangle[corner] >= vertexCount) inRange = false;
            }
            if (!inRange) continue;
            submesh.mesh.indices.insert(submesh.mesh.indices.end(), triangle, triangle + 3);
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
        submesh.material.name = material.name;

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
// The half both rigs share: order the joints parent-before-child, remap the
// parent indices, and fold any non-joint ancestors into preTransform.
//
// `inverseBinds` is aligned to `jointNodes` in the order given, NOT to the
// sorted order, because a skin's inverse bind matrices are indexed by the
// original skin.joints order. An empty vector leaves every joint's inverse bind
// at identity.
Skeleton buildSkeletonFromNodes(const tinygltf::Model& model,
                                const std::vector<int>& jointNodes,
                                const std::vector<glm::mat4>& inverseBinds,
                                const std::vector<int>& parents,
                                std::unordered_map<int, int32_t>& outNodeToJoint) {
    Skeleton skeleton;
    outNodeToJoint.clear();
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

        // Permuted alongside the sort rather than read positionally: the
        // caller's array is in ITS order, and this loop is in depth order.
        if (!inverseBinds.empty()) {
            const size_t original = originalIndexOf[nodeIndex];
            if (original < inverseBinds.size()) joint.inverseBind = inverseBinds[original];
        }
    }

    outNodeToJoint = jointOf;
    return skeleton;
}

// One glTF skin -> one Skeleton.
Skeleton buildSkeleton(const tinygltf::Model& model, const tinygltf::Skin& skin,
                       const std::vector<int>& parents,
                       std::unordered_map<int, int32_t>& outNodeToJoint) {
    outNodeToJoint.clear();
    if (skin.joints.empty()) return Skeleton{};

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

    std::vector<glm::mat4> inverseBinds;
    if (skin.inverseBindMatrices >= 0 &&
        static_cast<size_t>(skin.inverseBindMatrices) < model.accessors.size()) {
        const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(skin.inverseBindMatrices)];
        // One matrix per joint, read for every joint below. An accessor with fewer
        // read the rest from past its end; it is treated as absent instead, which
        // is what the glTF spec says an absent array means: every inverse bind
        // matrix is the identity.
        if (acc.count < jointNodes.size()) {
            SUPERSONIC_LOG_WARN("GltfLoader") << "A skin has " << jointNodes.size()
                << " joints but " << acc.count << " inverse bind matrices; ignoring them."
                << std::endl;
        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && acc.type == TINYGLTF_TYPE_MAT4) {
            size_t stride = 0;
            if (const float* data = accessorData<float>(model, acc, stride)) {
                inverseBinds.reserve(jointNodes.size());
                for (size_t i = 0; i < jointNodes.size(); ++i) {
                    const auto* m = reinterpret_cast<const float*>(
                        reinterpret_cast<const uint8_t*>(data) + i * stride);
                    inverseBinds.push_back(glm::make_mat4(m));
                }
            }
        }
    }

    return buildSkeletonFromNodes(model, jointNodes, inverseBinds, parents, outNodeToJoint);
}

// Where a node sits in the file's bind pose, composed from the root down.
glm::mat4 nodeWorldMatrix(const tinygltf::Model& model, int nodeIndex,
                          const std::vector<int>& parents) {
    glm::mat4 world(1.0f);
    int current = nodeIndex;
    int guard = 0;
    while (current >= 0 && guard++ < 1024) {
        world = nodeLocalMatrix(model.nodes[static_cast<size_t>(current)]) * world;
        current = parents[static_cast<size_t>(current)];
    }
    return world;
}

// A skeleton for a file that animates its NODES and has no skin at all.
//
// This is not an exotic case, it is the common one for hand-built content: an
// exporter writes a skin when a mesh is deformed by bones, and writes nothing
// when a turret rotates, a wheel spins or a limb swings as a rigid piece. The
// importer built clips only from skins, so every one of those files came in
// silent - and it never said so, because a file with no skin is not an error.
//
// The trick is that rigid animation IS skinning with one influence per vertex.
// visitNode has already baked each primitive into world space, so with
//
//     inverseBind(J) = inverse(worldBind(J))
//
// the joint matrix worldAnim(J) * inverseBind(J) cancels the bake at rest and
// carries the mesh in motion. Nothing in the pose evaluator, the palette upload
// or the vertex shader needs to know the difference.
//
// ONLY THE ANIMATED NODES BECOME JOINTS, and that is a budget decision rather
// than a tidiness one. kMaxPaletteMatrices is 1024 for the whole frame across
// every entity, so a soldier costs six joints and about a hundred and seventy
// of them fit; making every node a joint would cost twenty-three and fit
// forty-four. Non-animated nodes in between are folded into preTransform, which
// the skin path already does for the same reason.
Skeleton buildNodeRig(const tinygltf::Model& model, const std::vector<int>& parents,
                      std::unordered_map<int, int32_t>& outNodeToJoint,
                      int32_t& outStaticJoint) {
    Skeleton skeleton;
    outNodeToJoint.clear();
    outStaticJoint = -1;

    std::vector<int> animated;
    std::vector<bool> seen(model.nodes.size(), false);
    for (const auto& animation : model.animations) {
        for (const auto& channel : animation.channels) {
            const int node = channel.target_node;
            if (node < 0 || static_cast<size_t>(node) >= model.nodes.size()) continue;
            // Morph weights do not move a node, so a file that only animates
            // them must not acquire a rig that does nothing.
            if (channel.target_path != "translation" && channel.target_path != "rotation" &&
                channel.target_path != "scale") continue;
            if (seen[static_cast<size_t>(node)]) continue;
            seen[static_cast<size_t>(node)] = true;
            animated.push_back(node);
        }
    }
    if (animated.empty()) return skeleton;

    // One spare joint for the static geometry, and it is not optional.
    // MeshRegistry merges a file into ONE mesh and skips any primitive whose
    // skin index differs from the first, so a file where some primitives were
    // rigged and others were not would lose the others outright. Everything
    // binds to something; this one is identity, so what binds to it does not
    // move.
    if (animated.size() + 1 > jointLimit) {
        SUPERSONIC_LOG_ERROR("GltfLoader")
            << "A node rig with " << animated.size() << " animated nodes exceeds the "
            << jointLimit << "-joint limit; the file is imported unanimated." << std::endl;
        return skeleton;
    }

    std::vector<glm::mat4> inverseBinds;
    inverseBinds.reserve(animated.size());
    for (const int node : animated) {
        inverseBinds.push_back(glm::inverse(nodeWorldMatrix(model, node, parents)));
    }

    skeleton = buildSkeletonFromNodes(model, animated, inverseBinds, parents, outNodeToJoint);
    if (skeleton.joints.empty()) return skeleton;

    outStaticJoint = static_cast<int32_t>(skeleton.joints.size());
    Joint stationary;
    stationary.name = "static";
    skeleton.joints.push_back(stationary);   // identity throughout, parent -1
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

            // The loop below reads one component of a key time and three or four
            // of a value, so the accessors have to have that many. A rotation
            // output of VEC3, or any output of SCALAR, read bytes of the NEXT
            // element - or, for the last key, of nothing at all.
            const auto wantedOutputType = out.path == AnimPath::Rotation ? TINYGLTF_TYPE_VEC4
                                                                         : TINYGLTF_TYPE_VEC3;
            if (inputAcc.type != TINYGLTF_TYPE_SCALAR || outputAcc.type != wantedOutputType) continue;

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

// What a node-rigged file needs the walk to know. Absent for a file with a real
// skin or no animation at all, in which case the walk behaves exactly as before.
struct NodeRig {
    const std::unordered_map<int, int32_t>* nodeToJoint{nullptr};
    int32_t staticJoint{-1};

    bool active() const { return nodeToJoint != nullptr && staticJoint >= 0; }

    // The joint that moves this node: itself if it is animated, otherwise its
    // nearest animated ancestor, otherwise the stationary joint.
    //
    // NEAREST, walked upward, and not "the animated node whose subtree contains
    // this one" - those differ the moment one rigged node sits inside another,
    // and taking the outer one would leave the inner animation doing nothing.
    int32_t JointFor(int nodeIndex, const std::vector<int>& parents) const {
        int current = nodeIndex;
        int guard = 0;
        while (current >= 0 && guard++ < 1024) {
            if (const auto it = nodeToJoint->find(current); it != nodeToJoint->end()) {
                return it->second;
            }
            current = parents[static_cast<size_t>(current)];
        }
        return staticJoint;
    }
};

// How deep the node walk goes. It recurses once per level, so a chain of tens of
// thousands of nodes - each the only child of the last, from a file with nothing
// malformed in it - was a stack overflow: a crash, not an error. The other walks
// up the parent chain in this file stop at the same figure. No model that is
// drawn has a hierarchy anywhere near it.
constexpr int kMaxNodeDepth = 1024;

void visitNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix,
               const std::string& sourcePath, std::vector<GltfLoader::Submesh>& out,
               std::vector<bool>& visited, const NodeRig& rig,
               const std::vector<int>& parents, int depth = 0) {

    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) return;

    if (depth >= kMaxNodeDepth) {
        SUPERSONIC_LOG_ERROR("GltfLoader") << "Node hierarchy of '" << sourcePath
            << "' is deeper than " << kMaxNodeDepth << " levels; the rest of this branch is ignored."
            << std::endl;
        return;
    }

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
            // THREE cases, and conflating any two of them is a mesh in the
            // wrong place.
            //
            // A real skin is handed the IDENTITY, not its node's world matrix:
            // the inverse bind matrices are authored in the skin's own space,
            // and the glTF spec requires the skinned mesh node's own transform
            // to be ignored. Baking it in transforms the mesh twice.
            //
            // A NODE RIG is the opposite: its inverse binds are the inverse of
            // exactly these world matrices, so the bake is what they cancel.
            // Handing it identity here - which is what reusing "is it skinned"
            // as the test would do - collapses every animated prop onto the
            // origin. So bake-or-not is its own question, asked separately from
            // which skeleton the primitive belongs to.
            //
            // And a plain rigid primitive in a file with no animation at all is
            // baked and unskinned, exactly as before any of this existed.
            const int32_t skin = static_cast<int32_t>(node.skin);
            const bool rigged = skin < 0 && rig.active();

            const glm::mat4 primitiveMatrix = skin >= 0 ? glm::mat4(1.0f) : world;
            const int32_t submeshSkin = rigged ? 0 : skin;
            const int32_t rigidJoint = rigged ? rig.JointFor(nodeIndex, parents) : -1;

            appendPrimitive(model, primitive, primitiveMatrix, sourcePath, name, out,
                            submeshSkin, rigidJoint);
        }
    }

    for (const int child : node.children) {
        visitNode(model, child, world, sourcePath, out, visited, rig, parents, depth + 1);
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
    // No skin, but animated nodes: build a rig out of the node graph instead.
    //
    // A skin WINS when there is one - a file carrying both is describing its
    // deformation with the skin, and a second skeleton over the same nodes
    // would fight it.
    NodeRig rig;
    if (nodeToJoint.empty() && !model.animations.empty()) {
        int32_t staticJoint = -1;
        Skeleton nodeSkeleton = buildNodeRig(model, parents, nodeToJoint, staticJoint);
        if (!nodeSkeleton.empty()) {
            scene.skeletons.push_back(std::move(nodeSkeleton));
            rig.nodeToJoint = &nodeToJoint;
            rig.staticJoint = staticJoint;
        } else {
            nodeToJoint.clear();
        }
    }

    if (!nodeToJoint.empty()) {
        scene.clips = buildClips(model, nodeToJoint);
    }

    // Walk the default scene's node graph so each primitive comes out already
    // placed by its parent chain.
    if (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size())) {
        for (const int root : model.scenes[static_cast<size_t>(model.defaultScene)].nodes) {
            visitNode(model, root, glm::mat4(1.0f), path, scene.submeshes, visited, rig, parents);
        }
    } else if (!model.scenes.empty()) {
        for (const int root : model.scenes[0].nodes) {
            visitNode(model, root, glm::mat4(1.0f), path, scene.submeshes, visited, rig, parents);
        }
    } else {
        // No scene description at all: fall back to every node in the file.
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
            visitNode(model, i, glm::mat4(1.0f), path, scene.submeshes, visited, rig, parents);
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
