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

float4 PSMain(VSOutput input) : SV_Target {
    float depth = depth_texture.Sample(depth_sampler, input.texcoord).r;
    
    // Non-linear depth visualization
    // Goal: very sensitive near camera, almost same color far away
    
    // Method: exponential mapping with high exponent
    // pow(depth, k) where k is small (<1) makes dark values (near) stretched
    // This is similar to gamma correction but more aggressive
    
    float k = 0.8; // Moderate exponent: non-linear but not too extreme
    float viz = pow(saturate(depth), k);
    
    // Or alternatively: logarithmic mapping
    // float viz = log(1 + depth * 50) / log(51);
    
    // Use heatmap: blue (near) -> cyan -> green -> yellow -> red (far)
    // But most of the range will be in blue/cyan due to non-linearity
    float3 colors[5];
    colors[0] = float3(0.0, 0.0, 1.0);  // Blue
    colors[1] = float3(0.0, 1.0, 1.0);  // Cyan  
    colors[2] = float3(0.0, 1.0, 0.0);  // Green
    colors[3] = float3(1.0, 1.0, 0.0);  // Yellow
    colors[4] = float3(1.0, 0.0, 0.0);  // Red
    
    float scaled = viz * 4.0;
    int idx = (int)scaled;
    float frac = scaled - (float)idx;
    
    if (idx >= 4) {
        return float4(colors[4], 1.0);
    }
    
    float3 finalColor = lerp(colors[idx], colors[idx + 1], frac);
    return float4(finalColor, 1.0f);
}
