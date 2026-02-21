// PBR Forward Pass Shaders
// Compiled to .cso files at build time

cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
    
    float3 light_dir;
    float _padding2;
    float3 light_color;
    float light_intensity;
    
    float4 point_light_pos[4];
    float4 point_light_color[4];
    int point_light_count;
    float3 _padding3;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

cbuffer Material : register(b2) {
    float4 albedo;
    float4 emission;
    float roughness;
    float metallic;
    float alpha_cutoff;
    int use_albedo_map;
    int use_normal_map;
    int use_arm_map;
    int use_emission_map;
    float2 _padding_mat;
};

Texture2D albedo_map : register(t0);
Texture2D normal_map : register(t1);
Texture2D arm_map : register(t2);
Texture2D emission_map : register(t3);

SamplerState default_sampler : register(s0);

// ============================================================================
// Vertex Shader
// ============================================================================
struct VSInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float4 world_tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
    float3 view_dir : TEXCOORD1;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Use default values if tangent/texcoord are not provided (zero)
    float4 tangent_val = input.tangent;
    if (length(tangent_val.xyz) < 0.001) {
        // Generate a default tangent perpendicular to normal
        float3 up_vec = abs(input.normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t = normalize(cross(up_vec, input.normal));
        tangent_val = float4(t, 1.0);
    }
    
    // Transform position to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space (using inverse transpose)
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // Transform tangent to world space
    float3 world_tangent = mul((float3x3)model, tangent_val.xyz);
    output.world_tangent = float4(normalize(world_tangent), tangent_val.w);
    
    // Pass through texcoord (0,0 is fine for non-textured meshes)
    output.texcoord = input.texcoord;
    
    // View direction for specular
    output.view_dir = normalize(camera_pos - world_pos.xyz);
    
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
struct PSInput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float4 world_tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
    float3 view_dir : TEXCOORD1;
};

// Constants
static const float PI = 3.14159265359;

// Fresnel Schlick
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX Distribution
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

// Geometry Schlick GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Geometry Smith
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Lambert diffuse
float3 DiffuseLambert(float3 diffuseColor) {
    return diffuseColor * (1.0 / PI);
}

// Calculate tangent space normal
float3 GetNormalFromMap(PSInput input) {
    float3 tangent_normal = normal_map.Sample(default_sampler, input.texcoord).xyz * 2.0 - 1.0;
    
    float3 N = normalize(input.world_normal);
    float3 T = normalize(input.world_tangent.xyz);
    float3 B = cross(N, T) * input.world_tangent.w;
    
    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(TBN, tangent_normal));
}

// PBR BRDF calculation
float3 CalculateBRDF(float3 albedo_color, float roughness_val, float metallic_val, 
                      float3 N, float3 V, float3 L, float3 radiance) {
    float3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    float3 F0 = lerp((0.04).xxx, albedo_color, metallic_val);
    
    float D = DistributionGGX(N, H, roughness_val);
    float G = GeometrySmith(N, V, L, roughness_val);
    float3 F = FresnelSchlick(VdotH, F0);
    
    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular = numerator / denominator;
    
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic_val);
    
    float3 diffuse = DiffuseLambert(albedo_color);
    
    return (kD * diffuse + specular) * radiance * NdotL;
}

// Point light falloff
float PointLightFalloff(float dist, float radius) {
    return pow(saturate(1.0 - pow(dist / radius, 4)), 2) / (dist * dist + 1.0);
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Get base color
    float3 albedo_color = albedo.rgb;
    if (use_albedo_map) {
        float4 sampled = albedo_map.Sample(default_sampler, input.texcoord);
        albedo_color *= sampled.rgb;
        if (alpha_cutoff > 0.0 && sampled.a < alpha_cutoff) {
            discard;
        }
    }
    
    // Get material properties
    float roughness_val = roughness;
    float metallic_val = metallic;
    float ao = 1.0;
    
    if (use_arm_map) {
        float3 arm = arm_map.Sample(default_sampler, input.texcoord).rgb;
        ao = arm.r;
        roughness_val *= arm.g;
        metallic_val *= arm.b;
    }
    
    // Get normal
    float3 N = normalize(input.world_normal);
    if (use_normal_map) {
        N = GetNormalFromMap(input);
    }
    
    float3 V = normalize(input.view_dir);
    
    // Start with emission
    float3 final_color = emission.rgb;
    if (use_emission_map) {
        final_color *= emission_map.Sample(default_sampler, input.texcoord).rgb;
    }
    
    // Directional light
    {
        float3 L = normalize(-light_dir);
        float3 radiance = light_color * light_intensity;
        final_color += CalculateBRDF(albedo_color, roughness_val, metallic_val, N, V, L, radiance);
    }
    
    // Point lights
    for (int i = 0; i < point_light_count; i++) {
        float3 light_pos = point_light_pos[i].xyz;
        float light_range = point_light_pos[i].w;
        float3 light_col = point_light_color[i].rgb;
        float light_int = point_light_color[i].a;
        
        float3 L = normalize(light_pos - input.world_pos);
        float distance = length(light_pos - input.world_pos);
        
        float attenuation = PointLightFalloff(distance, light_range);
        float3 radiance = light_col * light_int * attenuation;
        
        final_color += CalculateBRDF(albedo_color, roughness_val, metallic_val, N, V, L, radiance);
    }
    
    // Ambient
    float3 ambient = 0.03 * albedo_color * ao;
    final_color += ambient;
    
    // Tone mapping (Reinhard)
    final_color = final_color / (final_color + 1.0);
    
    // Gamma correction
    final_color = pow(final_color, 1.0 / 2.2);
    
    return float4(final_color, 1.0);
}
