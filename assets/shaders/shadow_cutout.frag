#version 450

// The depth pass for surfaces that are mostly holes.
//
// shadow.frag writes nothing and is the right shader for a crate. It is the
// wrong one for a leaf card, a chain-link fence or a grate: those occlude only
// where their texture says they do, and a depth pass that cannot ask casts a
// solid rectangle - nothing like the shape drawn.
//
// A separate pipeline rather than one compare in shadow.frag, and the reason is
// not the compare. `discard` costs every draw on the pipeline its early depth
// rejection, and a cut-out caster needs a different CULL MODE as well: see the
// note beside cullMode in VulkanRenderer::createGraphicsPipeline. Opaque
// casters keep the shader and the rasterisation they had.

layout(location = 0) in vec2 fragTexCoord;

// Set 1 binding 0, the same albedo the scene pass samples. Every pipeline in
// this engine is built with both set layouts, so this needs no layout change -
// only the per-draw bind that RenderDepthOnly now makes for these casters.
layout(set = 1, binding = 0) uniform sampler2D albedoMap;

// Must match Engine::ShadowPushConstantData, and shadow.vert's declaration of
// the same block.
layout(push_constant) uniform PushConstants {
    mat4 viewProjModel;
    int skinPaletteBase;
    int skinJointCount;
    float alphaCutoff;
    float baseAlpha;
    // Declared and not read. A push constant block must be declared identically
    // in every stage of a pipeline that has one, and shadow.vert applies these
    // before the coordinates arrive here.
    vec4 uvAxes;
    vec4 uvOffset;
} push;

void main() {
    // textureLod at 0 rather than texture(), and this is the one place the
    // depth pass deliberately does NOT match shader.frag:302-303.
    //
    // texture() picks its mip from screen-space derivatives, and this stage
    // rasterises into a shadow map whose texels cover far more world than a
    // camera pixel does. A leaf two metres wide lands on a handful of texels in
    // the outer cascade, derivatives choose a mip that has averaged the holes
    // into the leaf, and the averaged alpha falls under the cutoff - so the
    // whole leaf drops out of the shadow map while its surface still draws.
    // Sampling the top mip costs bandwidth and keeps the silhouette.
    //
    // No `alphaCutoff > 0.0` guard: this pipeline only ever draws casters whose
    // cutoff is above zero. The guard belongs where the choice is made, and
    // that is GatherShadowCasters.
    if (textureLod(albedoMap, fragTexCoord, 0.0).a * push.baseAlpha < push.alphaCutoff) {
        discard;
    }
}
