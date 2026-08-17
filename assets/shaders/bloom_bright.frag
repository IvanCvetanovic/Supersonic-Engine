#version 450

// Bright pass: keeps what is brighter than the threshold, discards the rest.
//
// Only meaningful because the scene target is now floating point. On an 8-bit
// tonemapped image every value is already clamped to 1.0, so a threshold could
// only ever select "almost white" - which is why bloom on an LDR buffer looks
// like a uniform haze rather than light spilling from bright sources.
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform Params {
    vec4 value;   // x = threshold, y = soft knee
} params;

void main() {
    vec3 color = texture(sceneColor, fragUV).rgb;

    // Perceived brightness rather than max(r,g,b): a saturated red at 1.0 reads
    // far dimmer than white at 1.0, and thresholding on the channel maximum
    // makes strongly coloured lights bloom out of proportion.
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft knee, so a surface drifting past the threshold fades in instead of
    // popping. A hard step here is visible as a crawling outline on any slowly
    // moving highlight.
    float threshold = params.value.x;
    float knee = max(params.value.y, 0.0001);
    float soft = clamp((brightness - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    float contribution = max(brightness - threshold, 0.0);
    contribution = max(contribution, soft * soft * knee);

    float weight = brightness > 0.0001 ? contribution / brightness : 0.0;
    outColor = vec4(color * weight, 1.0);
}
