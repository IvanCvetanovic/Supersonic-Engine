#version 450

// Straight through. The colour is authored in linear space like every other
// colour in this renderer, so it reaches the tonemapper with the rest of the
// scene rather than being composited over the finished image - which is the
// whole difference between this and the UI shape layer.

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}
