// Skybox Shader
// Renders a cubemap as the environment background

// ============================================================================
// Vertex Shader
// ============================================================================
struct VSInput {
    float3 position : POSITION0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 sample_dir : TEXCOORD0;
};

cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding0;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

cbuffer SkyboxParams : register(b2) {
    float intensity;
    float _padding1[3];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Transform position to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    
    // Use the local position as the cube sample direction
    // The cube vertices are already in the correct directions
    output.sample_dir = input.position;
    
    // Transform to clip space
    // Note: We remove translation from view matrix to keep skybox at infinity
    float4x4 view_no_translation = view;
    view_no_translation[0][3] = 0.0;
    view_no_translation[1][3] = 0.0;
    view_no_translation[2][3] = 0.0;
    
    float4 view_pos = mul(view_no_translation, world_pos);
    output.position = mul(proj, view_pos);
    
    // Ensure skybox is at the far plane
    output.position = output.position.xyww;
    
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
struct PSInput {
    float4 position : SV_POSITION;
    float3 sample_dir : TEXCOORD0;
};

TextureCube<float4> skybox_texture : register(t0);
SamplerState skybox_sampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET {
    // Sample the cube texture
    float3 sample_dir = normalize(input.sample_dir);
    float4 color = skybox_texture.Sample(skybox_sampler, sample_dir);
    
    // Apply intensity
    color.rgb *= intensity;
    
    return color;
}
