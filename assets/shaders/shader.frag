#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

const float PI = 3.14159265359;

// Cook-Torrance GGX Normal Distribution
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0001);
}

// Schlick-GGX Geometry Shadowing
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Schlick Fresnel
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 albedoTex = texture(texSampler, fragTexCoord);
    vec3 albedo = albedoTex.rgb * fragColor;
    
    // PBR Material Properties
    float roughness = 0.35;
    float metallic = 0.15;
    float ao = 1.0;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(vec3(0.0, 1.5, 4.0) - fragWorldPos);

    // Directional Light Properties
    vec3 L = normalize(vec3(0.6, 1.0, 0.5));
    vec3 H = normalize(V + L);
    vec3 lightColor = vec3(1.0, 0.95, 0.88) * 1.5;

    // Calculate reflectance at normal incidence; if dia-magnetic (plastic/wood) use F0 of 0.04, if metal use albedo
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
       
    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    // kS is equal to Fresnel
    vec3 kS = F;
    // Energy conservation: kD (diffuse) + kS (specular) = 1.0
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;	  

    // Scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);        

    // Outgoing radiance Lo
    vec3 Lo = (kD * albedo / PI + specular) * lightColor * NdotL;
    
    // Ambient lighting
    vec3 ambient = vec3(0.12) * albedo * ao;
    vec3 color = ambient + Lo;

    // HDR tone mapping (Reinhard) and Gamma Correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedoTex.a);
}
