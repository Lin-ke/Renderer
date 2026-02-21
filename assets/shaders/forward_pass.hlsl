// Forward Pass Shaders (Blinn-Phong)
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
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float3 view_dir : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Transform position to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space (using inverse transpose)
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
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
    float3 view_dir : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Normalize inputs
    float3 N = normalize(input.world_normal);
    float3 V = normalize(input.view_dir);
    float3 L = normalize(-light_dir);
    float3 H = normalize(L + V);
    
    // Material properties (hardcoded for now)
    float3 diffuse_color = float3(0.8, 0.6, 0.5);
    float3 specular_color = float3(0.5, 0.5, 0.5);
    float shininess = 32.0;
    float ambient = 0.1;
    
    // Ambient
    float3 ambient_term = diffuse_color * ambient;
    
    // Diffuse (Lambert)
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse_term = diffuse_color * light_color * NdotL * light_intensity;
    
    // Specular (Blinn-Phong)
    float NdotH = max(dot(N, H), 0.0);
    float3 specular_term = specular_color * light_color * pow(NdotH, shininess) * light_intensity;
    
    // Final color
    float3 final_color = ambient_term + diffuse_term + specular_term;
    
    // Simple gamma correction
    final_color = pow(final_color, 1.0 / 2.2);
    
    return float4(final_color, 1.0);
}
