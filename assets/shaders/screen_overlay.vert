#version 450

// One quad of the screen overlay (core/ScreenOverlay.hpp), from gl_VertexIndex
// with no vertex buffer: six vertices, two triangles, the rectangle in the push
// constant.

layout(push_constant) uniform Quad {
    vec4 rect;   // xy = top-left, zw = bottom-right, fractions of the image, +y down
    vec4 uv;     // xy = uvMin, zw = uvMax
    vec4 color;  // display-referred, multiplied into the texel
} quad;

layout(location = 0) out vec2 fragUV;

// Must match kCorners in src/core/ScreenOverlay.cpp, in the same order.
const vec2 kCorners[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 c = kCorners[gl_VertexIndex];
    fragUV = mix(quad.uv.xy, quad.uv.zw, c);
    // Vulkan's clip space already runs +y down, so the fractions need no flip.
    gl_Position = vec4(mix(quad.rect.xy, quad.rect.zw, c) * 2.0 - 1.0, 0.0, 1.0);
}
