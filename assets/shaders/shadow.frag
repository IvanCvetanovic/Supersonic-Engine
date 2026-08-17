#version 450

// The shadow render pass has no colour attachment, so this writes nothing and
// exists only because the pipeline is built with a fragment stage. Depth is
// produced by the fixed-function stage from shadow.vert's gl_Position.
void main() {
}
