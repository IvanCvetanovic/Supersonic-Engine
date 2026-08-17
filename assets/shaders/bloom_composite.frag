#version 450

// Adds the bloom to the scene, then tone maps and encodes to sRGB.
//
// Tone mapping lives HERE and nowhere else. It used to sit at the end of
// shader.frag, which was correct while that shader wrote the final image - but
// with a floating-point scene target it has to happen after bloom, or the
// bright values bloom is meant to find have already been squashed into range.
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomColor;

layout(push_constant) uniform Params {
    vec4 value;   // x = bloom intensity, y = exposure
} params;

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;
    vec3 bloom = texture(bloomColor, fragUV).rgb;

    vec3 color = (scene + bloom * params.value.x) * max(params.value.y, 0.0001);

    // Reinhard, then sRGB. This is the single encode the whole pipeline is
    // built around: the output image is UNORM and ImGui blits it to the
    // swapchain with no colour conversion, so encoding twice anywhere would
    // wash the entire viewport out and nothing downstream would cancel it.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
