#version 450

// The screen overlay's colour: the texel as stored, times the quad's colour,
// written into the composited image with blending on (core/ScreenOverlay.hpp).
//
// NO CONVERSION, deliberately and in both directions. The image is read UNORM,
// so a texel is the byte in the file; the target is the tone-mapped, encoded
// output, so what is written is a display value. Sampling an sRGB view and
// re-encoding with pow(1/2.2) would look nearly the same and be off by a few
// grey levels, because the hardware's decode is the piecewise sRGB curve and
// 2.2 is not its inverse.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Set 1 is the material set every other pipeline binds its textures through;
// only its first binding is read here.
layout(set = 1, binding = 0) uniform sampler2D image;

layout(push_constant) uniform Quad {
    vec4 rect;
    vec4 uv;
    vec4 color;
} quad;

void main() {
    outColor = texture(image, fragUV) * quad.color;
}
