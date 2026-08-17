#version 450

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;
layout(location = 2) in mat4 viewMat;
layout(location = 6) in mat4 projMat;

layout(location = 0) out vec4 outColor;

vec4 grid(vec3 fragPos3D, float scale) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1.0);
    float minimumx = min(derivative.x, 1.0);
    vec4 color = vec4(0.2, 0.22, 0.25, 1.0 - min(line, 1.0));

    // Highlight X (Red) and Z (Blue) Origin Axes
    if (fragPos3D.x > -0.1 * minimumx && fragPos3D.x < 0.1 * minimumx)
        color.z = 1.0;
    if (fragPos3D.z > -0.1 * minimumz && fragPos3D.z < 0.1 * minimumz)
        color.x = 1.0;
        
    return color;
}

void main() {
    float denom = farPoint.y - nearPoint.y;
    if (abs(denom) < 1e-6) discard;   // ray parallel to the ground plane

    float t = -nearPoint.y / denom;
    if (t < 0.0) discard;             // plane is behind the camera

    vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);

    // Write real depth so scene geometry occludes the grid instead of the grid
    // always compositing on top of it.
    vec4 clip = projMat * viewMat * vec4(fragPos3D, 1.0);
    gl_FragDepth = clip.z / clip.w;

    vec4 color = grid(fragPos3D, 1.0);

    // Distance fading
    float fading = max(0.0, 1.0 - length(fragPos3D) / 80.0);

    outColor = color;
    outColor.a *= fading * 0.7;
}
