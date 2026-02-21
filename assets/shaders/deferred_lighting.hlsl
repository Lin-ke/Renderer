// Deferred Lighting Pass Shaders
// Compiled to .cso files at build time

// ============================================================================
// Vertex Shader
// ============================================================================
struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float2 positions[4] = {
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2( 1.0,  1.0),
    float2(-1.0,  1.0)
};

static const float2 uvs[4] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0)
};

VSOutput VSMain(uint vertex_id : SV_VertexID) {
    VSOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// G-Buffer textures
Texture2D g_albedo_ao : register(t0);
Texture2D g_normal_roughness : register(t1);
Texture2D g_material : register(t2);
Texture2D g_position_depth : register(t3);

SamplerState g_sampler : register(s0);

// Per-frame data
cbuffer PerFrame : register(b0) {
    float3 camera_pos;
    float _padding0;
    
    uint light_count;
    float _padding1[3];
    
    float3 main_light_dir;
    float _padding2;
    float3 main_light_color;
    float main_light_intensity;
    
    float4x4 inv_view_proj;
};

// Light structure
struct Light {
    float3 position;
    float _padding0;
    
    float3 color;
    float intensity;
    
    float3 direction;
    float range;
    
    uint type;
    float inner_angle;
    float outer_angle;
    float _padding1;
};

// PBR Constants
static const float PI = 3.14159265359;
static const float MIN_ROUGHNESS = 0.001;
static const float MAX_ROUGHNESS = 0.999;

// GGX Normal Distribution Function
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Geometry Function - Schlick approximation
float G_Schlick(float NoV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

// Smith Geometry Function
float G_Smith(float NoV, float NoL, float roughness) {
    return G_Schlick(NoV, roughness) * G_Schlick(NoL, roughness);
}

// Schlick Fresnel approximation
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel with roughness modification (for diffuse)
float3 F_Schlick_Roughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Lambertian diffuse
float3 Diffuse_Lambert(float3 albedo) {
    return albedo / PI;
}

// PBR BRDF calculation
float3 PBR_BRDF(float3 albedo, float roughness, float metallic, float3 N, float3 V, float3 L, out float3 F) {
    float3 H = normalize(V + L);
    
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    
    // Fresnel reflectance at normal incidence
    float3 F0 = lerp(0.04, albedo, metallic);
    
    // Fresnel
    F = F_Schlick(VoH, F0);
    
    // Normal Distribution
    float D = D_GGX(NoH, roughness);
    
    // Geometry
    float G = G_Smith(NoV, NoL, roughness);
    
    // Specular
    float3 numerator = D * G * F;
    float denominator = 4.0 * NoV * NoL + 0.0001;
    float3 specular = numerator / denominator;
    
    // Diffuse (energy conservation)
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * Diffuse_Lambert(albedo);
    
    return diffuse + specular;
}

// Directional light calculation
float3 CalcDirectionalLight(float3 albedo, float roughness, float metallic, float3 N, float3 V, float3 lightDir, float3 lightColor, float lightIntensity) {
    float3 L = normalize(-lightDir);
    float NoL = saturate(dot(N, L));
    
    float3 F;
    float3 brdf = PBR_BRDF(albedo, roughness, metallic, N, V, L, F);
    
    float3 radiance = lightColor * lightIntensity;
    return brdf * radiance * NoL;
}

// Point light calculation
float3 CalcPointLight(float3 albedo, float roughness, float metallic, float3 worldPos, float3 N, float3 V, Light light) {
    float3 L = light.position - worldPos;
    float dist = length(L);
    L = normalize(L);
    
    // Attenuation
    float attenuation = 1.0;
    if (light.range > 0.0) {
        attenuation = saturate(1.0 - (dist / light.range));
        attenuation *= attenuation;
    }
    
    float NoL = saturate(dot(N, L));
    
    float3 F;
    float3 brdf = PBR_BRDF(albedo, roughness, metallic, N, V, L, F);
    
    float3 radiance = light.color * light.intensity * attenuation;
    return brdf * radiance * NoL;
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Sample G-Buffer
    float4 albedo_ao = g_albedo_ao.Sample(g_sampler, input.uv);
    float4 normal_roughness = g_normal_roughness.Sample(g_sampler, input.uv);
    float4 material = g_material.Sample(g_sampler, input.uv);
    float4 position_depth = g_position_depth.Sample(g_sampler, input.uv);
    
    // Unpack G-Buffer data
    float3 albedo = albedo_ao.rgb;
    float ao = albedo_ao.a;
    
    float3 N = normalize(normal_roughness.rgb * 2.0 - 1.0);
    float roughness = clamp(normal_roughness.a, MIN_ROUGHNESS, MAX_ROUGHNESS);
    
    float metallic = material.r;
    float emission = material.g;
    float specular = material.b;
    
    float3 worldPos = position_depth.rgb;
    
    // Early out if no geometry
    if (position_depth.a == 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    
    // View direction
    float3 V = normalize(camera_pos - worldPos);
    
    // Ambient occlusion
    ao = 1.0;
    
    // PBR Lighting
    float3 Lo = float3(0.0, 0.0, 0.0);
    
    // Main directional light
    if (main_light_intensity > 0.0) {
        Lo += CalcDirectionalLight(albedo, roughness, metallic, N, V, 
                                    main_light_dir, main_light_color, main_light_intensity);
    }
    
    // Ambient lighting (simplified IBL)
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = F_Schlick_Roughness(saturate(dot(N, V)), F0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    
    float3 irradiance = 0.03;
    float3 diffuse = irradiance * albedo;
    float3 ambient = (kD * diffuse) * ao;
    
    // Emission
    float3 emission_color = albedo * emission;
    
    // Final color
    float3 color = ambient + Lo + emission_color;
    
    // HDR tone mapping (Reinhard)
    color = color / (color + 1.0);
    
    // Gamma correction
    color = pow(color, 1.0 / 2.2);
    
    return float4(color, 1.0);
}
