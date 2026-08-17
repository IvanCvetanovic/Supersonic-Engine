#version 450

layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;
layout(location = 2) out mat4 viewMat;
layout(location = 6) out mat4 projMat;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
struct Light {
    vec4 positionOrDirection;
    vec4 colorAndIntensity;
    vec4 attenuation;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpace;
    vec4 cameraPosition;
    vec4 ambientColor;
    vec4 lightCount;
    Light lights[8];
} ubo;

vec3 gridPlane[6] = vec3[](
    vec3(-1.0, -1.0, 0.0), vec3( 1.0, -1.0, 0.0), vec3( 1.0,  1.0, 0.0),
    vec3(-1.0, -1.0, 0.0), vec3( 1.0,  1.0, 0.0), vec3(-1.0,  1.0, 0.0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 view, mat4 proj) {
    mat4 invView = inverse(view);
    mat4 invProj = inverse(proj);
    vec4 unprojectedPoint = invView * invProj * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    vec3 p = gridPlane[gl_VertexIndex];
    nearPoint = UnprojectPoint(p.x, p.y, 0.0, ubo.view, ubo.proj);
    farPoint = UnprojectPoint(p.x, p.y, 1.0, ubo.view, ubo.proj);
    viewMat = ubo.view;
    projMat = ubo.proj;
    gl_Position = vec4(p, 1.0);
}
