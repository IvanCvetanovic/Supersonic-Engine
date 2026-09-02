#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

// The sky, as a fullscreen triangle at the far plane.
//
// Nothing was drawn behind the scene before this: the offscreen target was
// cleared to a flat 0.00023 and that was the background. A mirror-metal sphere
// therefore reflected the hemisphere gradient the shader invents in code, with
// nothing on screen for it to agree with - the reflection and the background
// were two different skies.
//
// One oversized triangle rather than a quad, for the same reason fullscreen.vert
// uses one: a quad's shared diagonal makes the GPU shade that edge's helper
// pixels twice.
//
// z = 1.0 puts it exactly on the far plane under the zero-to-one depth
// convention this engine uses, so it is drawn AFTER opaque geometry with
// depthCompare = lessOrEqual and fills only the pixels nothing else claimed.
// Drawing it first with depth write off would shade every pixel the scene then
// covers, which on a full screen is the whole point of not doing it that way.

layout(location = 0) out vec3 fragViewRay;

void main() {
    const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    const vec2 ndc = uv * 2.0 - 1.0;

    gl_Position = vec4(ndc, 1.0, 1.0);

    // The world-space direction this pixel looks along, without needing an
    // inverse matrix in the UBO.
    //
    // A view matrix's upper 3x3 is orthonormal, so its transpose is its
    // inverse: transpose(mat3(view)) takes camera-space directions to world
    // space.

    // WHICH PROJECTION THIS IS, read from the matrix rather than passed in.
    // The fourth diagonal is 1 for an orthographic projection and 0 for a
    // perspective one - that is the whole difference between the two, and it
    // is what the w-divide does or does not do. The engine's Y flip negates
    // proj[1][1] and leaves this alone, so the test survives it. Pinned in
    // test_camera against the matrices the camera actually builds, because a
    // shader cannot be unit-tested and its premise can.
    const bool orthographic = ubo.proj[3][3] != 0.0;

    vec3 cameraSpace;
    if (orthographic) {
        // NO EYE POINT, so every ray through the image is the same one. A
        // parallel projection has no field of view - the rays do not fan out
        // from anywhere - and the sky is therefore one colour, which is what a
        // gradient sampled in a single direction comes out as.
        //
        // What this replaces read a tangent of half the field of view out of
        // proj[0][0], a number an orthographic matrix does not carry: there it
        // is 2/width, so the "rays" fanned out from a point that is not there
        // by an amount that depended on how far the camera happened to be
        // zoomed. Nothing hit it while the only orthographic camera in the
        // tree drew no sky; the editor can author one now.
        cameraSpace = vec3(0.0, 0.0, -1.0);
    } else {
        // The half-extents of the near plane come out of the projection
        // itself - proj[0][0] is 1/(aspect * tanHalfFov) and proj[1][1] is
        // -1/tanHalfFov, negative because this engine flips Y for Vulkan.
        const float tanHalfFovX = 1.0 / ubo.proj[0][0];
        const float tanHalfFovY = 1.0 / ubo.proj[1][1];

        // -Z is forward in camera space.
        cameraSpace = vec3(ndc.x * tanHalfFovX, ndc.y * tanHalfFovY, -1.0);
    }

    fragViewRay = transpose(mat3(ubo.view)) * cameraSpace;
}
