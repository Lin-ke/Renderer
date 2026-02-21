// G-Buffer Pass Shaders
// Compiled to .cso files at build time

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
    
    // Normalize normal
    float3 N = normalize(input.world_normal);
    
    // Pack normal to [0,1] range for storage
    float3 packed_normal = N * 0.5 + 0.5;
    
    // Material parameters
    float3 albedo = float3(0.8, 0.6, 0.5);
    float roughness = 0.5;
    float metallic = 0.0;
    float ao = 1.0;
    float emission = 0.0;
    float specular = 0.5;
    
    // Output to G-Buffer targets
    output.albedo_ao = float4(albedo, ao);
    output.normal_roughness = float4(packed_normal, roughness);
    output.material_emission = float4(metallic, emission, specular, 1.0);
    output.position_depth = float4(input.world_pos, input.position.w);
    
    return output;
}
