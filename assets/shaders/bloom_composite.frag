#version 450

// Adds the bloom to the scene, then tone maps and encodes to sRGB - or, for a
// scene that says its numbers are already display values, only clamps
// (RenderSettings::SceneEncoding).
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
    vec4 value;   // x = bloom intensity, y = exposure,
                  // z = RenderSettings::SceneEncoding (0 LinearHdr, 1 LinearNoToneMap, 2 DisplayEncoded),
                  // w = 1 to quantise to 5/6/5 (RenderSettings::OutputQuantize::Rgb565)
} params;

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;
    vec3 bloom = texture(bloomColor, fragUV).rgb;

    vec3 color = (scene + bloom * params.value.x) * max(params.value.y, 0.0001);

    if (params.value.z < 0.5) {
        // LinearHdr. Reinhard, then sRGB. This is the single encode the whole
        // pipeline is built around: the output image is UNORM and ImGui blits
        // it to the swapchain with no colour conversion, so encoding twice
        // anywhere would wash the entire viewport out and nothing downstream
        // would cancel it.
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    } else if (params.value.z < 1.5) {
        // LinearNoToneMap. The same single encode, over a clamp instead of a
        // curve: linear 1.0 is white.
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    } else {
        // DisplayEncoded. The scene already holds display values, and a
        // fixed-point framebuffer would have clipped here too. The bloom is
        // not read: its passes are not recorded in this mode, and binding 1
        // holds the scene image itself, so it is valid to sample and means
        // nothing.
        color = clamp(scene * max(params.value.y, 0.0001), 0.0, 1.0);
    }

    if (params.value.w > 0.5) {
        // A 16-bit target stores round(v * (2^bits - 1)); this writes that
        // level's reading as 8 bits, round(level * 255 / (2^bits - 1)). As
        // k / 255, not as level / (2^bits - 1): the driver's float-to-UNORM
        // rounding stored 20/31, which is 164.516 of 255, as 164 (remaster
        // step 43), and k / 255 has no fraction to round. Replicating the high
        // bits into the low ones, the other common reading, differs by one on
        // 4 of the 32 five-bit levels and 10 of the 64 six-bit ones.
        const vec3 levels = vec3(31.0, 63.0, 31.0);
        color = floor(floor(color * levels + 0.5) * 255.0 / levels + 0.5) / 255.0;
    }

    outColor = vec4(color, 1.0);
}
