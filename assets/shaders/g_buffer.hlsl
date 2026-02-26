// G-Buffer Pass Shaders
// Supports both ARM packed texture and individual texture maps

cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

cbuffer Material : register(b2) {
    float4 albedo;
    float roughness;
    float metallic;
    float alpha_clip;
    float use_albedo_map;       // t0
    float use_normal_map;       // t1
    float use_arm_map;          // t2 (AO=R, Roughness=G, Metallic=B)
    float use_roughness_map;    // t3
    float use_metallic_map;     // t4
    float use_ao_map;           // t5
    float use_emission_map;     // t6
    float3 _padding_mat;
};

// Texture slots
texture2D albedo_map : register(t0);
Texture2D normal_map : register(t1);
Texture2D arm_map : register(t2);
Texture2D roughness_map : register(t3);
Texture2D metallic_map : register(t4);
Texture2D ao_map : register(t5);
Texture2D emission_map : register(t6);

SamplerState default_sampler : register(s0);

// ============================================================================
// Vertex Shader
// ============================================================================
struct VSInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Transform to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // Pass through texcoord
    output.texcoord = input.texcoord;
    
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
struct PSInput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput {
    float4 albedo_ao : SV_TARGET0;
    float4 normal_roughness : SV_TARGET1;
    float4 material_emission : SV_TARGET2;
    float4 position_depth : SV_TARGET3;
};

PSOutput PSMain(PSInput input) {
    PSOutput output;
    
    // Sample albedo texture or use constant
    float3 albedo_color = albedo.rgb;
    float alpha = 1.0;
    if (use_albedo_map > 0.5) {
        float4 sampled = albedo_map.Sample(default_sampler, input.texcoord);
        albedo_color *= sampled.rgb;
        alpha = sampled.a;
        // Alpha clip
        if (alpha_clip > 0.0 && alpha < alpha_clip) {
            discard;
        }
    }
    
    // Get material properties - start with material parameters
    float roughness_val = roughness;
    float metallic_val = metallic;
    float ao = 1.0;
    float emission_val = emission.r;
    
    // ARM map takes priority if available (AO=R, Roughness=G, Metallic=B)
    if (use_arm_map > 0.5) {
        float3 arm = arm_map.Sample(default_sampler, input.texcoord).rgb;
        ao = arm.r;
        roughness_val = roughness * arm.g;
        metallic_val = metallic * arm.b;
    } else {
        // Fall back to individual maps
        if (use_ao_map > 0.5) {
            ao = ao_map.Sample(default_sampler, input.texcoord).r;
        }
        if (use_roughness_map > 0.5) {
            roughness_val = roughness * roughness_map.Sample(default_sampler, input.texcoord).r;
        }
        if (use_metallic_map > 0.5) {
            metallic_val = metallic * metallic_map.Sample(default_sampler, input.texcoord).r;
        }
    }
    
    // Emission map
    if (use_emission_map > 0.5) {
        emission_val *= emission_map.Sample(default_sampler, input.texcoord).r;
    }
    
    // Calculate normal
    float3 N = normalize(input.world_normal);
    if (use_normal_map > 0.5) {
        // Sample normal map and decode from [0,1] to [-1,1]
        float3 tangent_normal = normal_map.Sample(default_sampler, input.texcoord).xyz * 2.0 - 1.0;
        
        // Build tangent space basis
        float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N));
        float3 B = cross(N, T);
        
        // Transform from tangent space to world space
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tangent_normal, TBN));
    }
    
    // Pack normal to [0,1] range for storage
    float3 packed_normal = N * 0.5 + 0.5;
    
    // Output to G-Buffer targets
    output.albedo_ao = float4(albedo_color, ao);
    output.normal_roughness = float4(packed_normal, roughness_val);
    output.material_emission = float4(metallic_val, emission_val, 0.5, 1.0); // metallic, emission, specular
    output.position_depth = float4(input.world_pos, input.position.w);
    
    return output;
}
