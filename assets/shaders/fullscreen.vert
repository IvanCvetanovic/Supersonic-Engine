#version 450

// Fullscreen triangle, generated from gl_VertexIndex with no vertex buffer.
//
// One oversized triangle rather than two triangles forming a quad: the quad's
// shared diagonal makes the GPU shade that edge's helper pixels twice, and the
// triangle has no interior edge at all. The parts hanging outside the screen
// are clipped for free.
layout(location = 0) out vec2 fragUV;

void main() {
    // 0 -> (0,0), 1 -> (2,0), 2 -> (0,2)
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
