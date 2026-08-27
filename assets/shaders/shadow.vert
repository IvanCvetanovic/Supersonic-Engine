#version 450

// Depth-only pass from the light's point of view, run once per shadow cascade.
// Only the position matters; the other vertex attributes are declared so this
// pipeline can share the same vertex input layout as the scene pipeline.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in uvec4 inJointIndices;
layout(location = 6) in vec4 inJointWeights;

// Must match Engine::ShadowPushConstantData.
//
// cascadeViewProj * model arrives premultiplied, for two reasons: two separate
// matrices would be the whole 128-byte push constant budget and leave nothing
// for the skinning indices, and taking the cascade transform this way keeps this
// stage free of the scene UBO block entirely - so it cannot drift out of step
// with the three other declarations of it, which is a mismatch nothing
// diagnoses.
layout(push_constant) uniform PushConstants {
    mat4 viewProjModel;
    int skinPaletteBase;
    int skinJointCount;
    // Read by shadow_cutout.frag, not here. The block is declared whole in both
    // stages because a push constant range is one range across both, and two
    // declarations that disagree about its tail is a mismatch nothing catches.
    float alphaCutoff;
    float baseAlpha;

    // Must match Engine::UvTransform. Read HERE and not in the cut-out
    // fragment shader: the transform is affine, so transforming per vertex and
    // interpolating is the same answer as the other order, with fewer of them.
    vec4 uvAxes;     // xy = where U points, zw = where V points
    vec4 uvOffset;   // xy = translation, zw unused
} push;

// For the cut-out depth pipeline, which samples albedo to decide whether a
// fragment is there at all. shadow.frag ignores it, and an unconsumed vertex
// output is legal - the same latitude that lets this stage declare vertex
// attributes it does not read.
layout(location = 0) out vec2 fragTexCoord;

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
    // A skinned character whose shadow is not skinned too stays frozen in bind
    // pose on the ground while the character moves - and no validation layer
    // says a word, because unconsumed vertex attributes are perfectly legal.
    mat4 skin = skinMatrix(push.skinPaletteBase, push.skinJointCount, inJointIndices, inJointWeights);
    gl_Position = push.viewProjModel * skin * vec4(inPosition, 1.0);
    // The cut is made against the coordinates the surface actually samples.
    // Passing inTexCoord straight through casts the silhouette the material
    // had before it scrolled, which looks like a shadow that is merely a
    // little wrong.
    fragTexCoord = mat2(push.uvAxes.x, push.uvAxes.y, push.uvAxes.z, push.uvAxes.w)
                 * inTexCoord + push.uvOffset.xy;
}
