// Depth Visualize Pass Shaders
// Compiled to .cso files at build time

// ============================================================================
// Shared structures
// ============================================================================
struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// ============================================================================
// Vertex Shader
// ============================================================================
VSOutput VSMain(uint VertexID : SV_VertexID) {
    VSOutput output;
    output.texcoord = float2((VertexID << 1) & 2, VertexID & 2);
    output.position = float4(output.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
cbuffer DepthVisualizeConstants : register(b0) {
    float near_plane;
    float far_plane;
    float padding1;
    float padding2;
};

Texture2D depth_texture : register(t0);
SamplerState depth_sampler : register(s0);

float LinearizeDepth(float depth) {
    // Assuming standard DX11 depth range [0, 1]
    // z_eye = (far * near) / (far - depth * (far - near))
    return (far_plane * near_plane) / (far_plane - depth * (far_plane - near_plane));
}

float4 PSMain(VSOutput input) : SV_Target {
    float depth = depth_texture.Sample(depth_sampler, input.texcoord).r;
    float linear_depth = LinearizeDepth(depth);
    
    // Visualize linear depth normalized by far plane
    float viz = linear_depth / far_plane;
    
    // Apply a curve to make near objects more visible
    viz = pow(viz, 1.0/2.2);
    
    return float4(viz, viz, viz, 1.0f);
}
