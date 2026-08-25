#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

// The same sky the shading already assumes.
//
// shader.frag's ambient term is hemisphere(N) - a mix between ambientGround and
// ambientColor by which way a surface faces. That function was the engine's
// only notion of an environment, and it was invisible: it lit surfaces while
// the background stayed a flat near-black clear. Drawing the same gradient here
// means what a surface reflects and what sits behind it are finally the same
// thing, which is the part that reads as "an environment" rather than "a
// gradient someone tuned".
//
// Deliberately NOT a new set of authored colours. Two sky definitions that can
// disagree is exactly the class of drift this project keeps finding in itself.

layout(location = 0) in vec3 fragViewRay;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 dir = normalize(fragViewRay);

    // A loaded environment IS the sky. Reflections have come from the cubemap
    // since image-based lighting landed, while the background stayed the
    // analytic gradient below - so a chrome sphere showed a room and the space
    // behind it showed a two-colour ramp, which is the same disagreement this
    // pass was written to end, one layer further up.
    //
    // Level 0 of the prefiltered chain, which is roughness zero, which is the
    // environment itself. Nothing else is needed: the cube is already bound to
    // this set, because scene_ubo.glsl declares it and the sky pass binds the
    // scene set.
    //
    // The whole procedural sky goes, sun included. An HDRI has its own sun in
    // it, and drawing another one over the top would light the scene from one
    // and show the other.
    if (ubo.environmentParams.x > 0.5) {
        // Slot 0, always: the sky is the scene-wide environment, and a probe
        // is a local override for objects, not for the background. This is why
        // environmentParams.x still means what it always meant - slot 0 holds a
        // real map - rather than being repurposed as a per-slot flag.
        outColor = vec4(textureLod(prefilteredMaps[0], dir, 0.0).rgb, 1.0);
        return;
    }

    // The same mapping hemisphere() uses, so the horizon sits where the shading
    // thinks it does.
    const float up = dir.y * 0.5 + 0.5;
    vec3 sky = mix(ubo.ambientGround.rgb, ubo.ambientColor.rgb, up);

    // A sun for the first directional light, if there is one.
    //
    // lights[0] is the shadow-casting directional light by construction -
    // gatherLights places it there because the cascade lookup is gated on
    // i == 0 - so this cannot disagree with the light casting the shadows.
    // positionOrDirection.w < 0.5 means directional.
    if (int(ubo.lightCount.x) > 0 && lightBuffer.lights[0].positionOrDirection.w < 0.5) {
        // The stored vector points TOWARD the light, which is where the disc
        // belongs; using it unnegated would put the sun opposite the shadows.
        const vec3 toSun = normalize(lightBuffer.lights[0].positionOrDirection.xyz);
        const float cosAngle = max(dot(dir, toSun), 0.0);

        // Two terms: a small bright disc, and a wide dim halo. One power large
        // enough for a crisp disc leaves nothing around it, and a sky with a
        // hard-edged dot and no glow reads as a bug rather than as a sun.
        const float disc = pow(cosAngle, 2200.0);
        const float halo = pow(cosAngle, 24.0) * 0.18;

        const vec3 sunColor = lightBuffer.lights[0].colorAndIntensity.rgb
                            * max(lightBuffer.lights[0].colorAndIntensity.a, 0.0);

        // Above 1.0 on purpose: the target is floating point and the bright
        // pass thresholds at 1.0, so the sun is something bloom can find. That
        // only works because the scene target stopped being 8-bit UNORM.
        sky += sunColor * (disc * 6.0 + halo);
    }

    // Linear and un-encoded, like everything else written into this target.
    // bloom_composite.frag tone-maps and encodes once, at the end.
    outColor = vec4(sky, 1.0);
}
