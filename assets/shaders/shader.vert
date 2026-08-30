#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;   // xyz tangent, w handedness
layout(location = 5) in uvec4 inJointIndices;  // uvec4: the attribute is a UINT format
layout(location = 6) in vec4 inJointWeights;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

// WHICH instance this fragment belongs to. gl_InstanceIndex is a vertex-stage
// input and the fragment shader cannot ask for it, so it is carried across -
// flat, because an interpolated index is not an index.
layout(location = 6) flat out int fragInstance;

#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

// PER-DRAW DATA, ONE ENTRY PER INSTANCE.
//
// This was a push constant, and the block was exactly 128 bytes - the
// guaranteed minimum, with the comment on `flags` saying outright that nothing
// else fits. It moves here for a different reason though: a push constant is
// written per draw, so every drawable cost a vkCmdPushConstants and a
// vkCmdDrawIndexed of its own. Read from a buffer and indexed by the instance,
// a thousand identical cubes are ONE draw.
//
// The layout is unchanged and still matches Engine::PushConstantData byte for
// byte - std430 gives mat4 a 16-byte alignment and a 64-byte size, each vec4
// 16, each int 4, for 128 with no padding, which is the stride the C++ side
// asserts.
struct InstanceData {
    mat4 model;
    vec4 albedoColor;
    vec4 material;      // x = roughness, y = metallic, z = ao, w = alpha cutoff
    vec4 emissive;      // rgb added after shading, may exceed 1.0; w = occlusion
    int skinPaletteBase;   // -1 = not skinned
    int skinJointCount;
    int probeIndex;        // which environment lights this draw
    int flags;             // bit 0 = unlit; bits 8.. = uv slot
};

layout(std430, set = 0, binding = 11) readonly buffer InstanceBuffer {
    InstanceData instances[];
};


// This frame's joint matrices for every skinned entity, back to back. std430
// spelled out so the mat4 array stride is 64 bytes and matches
// std::vector<glm::mat4> exactly.
layout(std430, set = 0, binding = 2) readonly buffer JointPalette {
    mat4 joints[];
} palette;

// Identity when the draw is not skinned, so neither vertex shader needs a branch
// around the matrix multiply itself.
mat4 skinMatrix(int paletteBase, int jointCount, uvec4 indices, vec4 weights) {
    if (paletteBase < 0 || jointCount <= 0) return mat4(1.0);

    // Every index is clamped. robustBufferAccess is not enabled on this device,
    // so a stray index is undefined behaviour - a device loss, not a zero read -
    // and the loader's clamp is not something this stage can verify.
    mat4 result = mat4(0.0);
    for (int i = 0; i < 4; ++i) {
        float weight = weights[i];
        if (weight <= 0.0) continue;
        int index = clamp(int(indices[i]), 0, jointCount - 1);
        result += weight * palette.joints[paletteBase + index];
    }

    // A vertex with no influences at all would otherwise collapse onto the
    // origin, which looks like the mesh imploding rather than like missing data.
    if (result[3][3] == 0.0) return mat4(1.0);
    return result;
}


void main() {
    // gl_InstanceIndex includes firstInstance in Vulkan, which is what lets a
    // batch address its own slice of the buffer without a per-draw uniform.
    fragInstance = gl_InstanceIndex;
    InstanceData inst = instances[gl_InstanceIndex];

    mat4 skin = skinMatrix(inst.skinPaletteBase, inst.skinJointCount, inJointIndices, inJointWeights);
    mat3 skinNormal = mat3(skin);

    vec4 worldPos = inst.model * skin * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragWorldPos = worldPos.xyz;

    // Normals need the inverse-transpose (non-uniform scale would shear them
    // off the surface); tangents are true directions and take the model matrix.
    mat3 normalMatrix = mat3(transpose(inverse(inst.model)));
    fragNormal = normalMatrix * (skinNormal * inNormal);
    fragTangent = mat3(inst.model) * (skinNormal * inTangent.xyz);
    // Handedness in w reconstructs the bitangent without storing it.
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // No light-space position is interpolated any more: which cascade to use
    // depends on the fragment's view depth, and the lookup is offset along the
    // shaded normal, so both have to happen in the fragment stage.
}
