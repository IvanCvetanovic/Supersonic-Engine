#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.6, 1.0, 0.5)); // Directional Light
    
    // Ambient component
    vec3 ambient = vec3(0.22) * fragColor;
    
    // Diffuse component (Lambertian)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 0.95, 0.85) * fragColor;
    
    // Specular component (Blinn-Phong)
    vec3 viewDir = normalize(vec3(0.0, 1.5, 4.0) - fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3 specular = vec3(0.4) * spec;
    
    // Combine texture map with Blinn-Phong directional lighting
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 finalColor = (ambient + diffuse + specular) * texColor.rgb;
    
    outColor = vec4(finalColor, texColor.a);
}
