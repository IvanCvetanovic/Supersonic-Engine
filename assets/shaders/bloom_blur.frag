#version 450

// One direction of a separable Gaussian blur.
//
// Separable: a 9x9 two-dimensional kernel is 81 taps, the same blur run twice
// in one dimension is 18. The pass is invoked twice with a different direction.
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Params {
    vec4 value;   // xy = one texel step along the blur axis
} params;

void main() {
    // Sampled at the midpoint between texel pairs so the hardware's bilinear
    // filter supplies two texels per fetch: nine taps of reach for five reads.
    const float offsets[5] = float[](0.0, 1.4117647, 3.2941176, 5.1764705, 7.0588234);
    const float weights[5] = float[](0.1964825, 0.2969069, 0.0944703, 0.0103813, 0.0002593);

    vec2 step = params.value.xy;
    vec3 result = texture(source, fragUV).rgb * weights[0];

    for (int i = 1; i < 5; ++i) {
        vec2 delta = step * offsets[i];
        result += texture(source, fragUV + delta).rgb * weights[i];
        result += texture(source, fragUV - delta).rgb * weights[i];
    }

    outColor = vec4(result, 1.0);
}
