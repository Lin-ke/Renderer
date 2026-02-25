// Panorama to Cubemap Conversion Shader
// Converts an equirectangular (panorama) 2D texture to a cube texture

// ============================================================================
// Vertex Shader
// ============================================================================
struct VSInput {
    float3 position : POSITION0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    // Full screen quad position (no matrices needed for skybox cube)
    output.position = float4(input.position.xy, 1.0, 1.0);
    output.local_pos = input.position;
    return output;
}

// ============================================================================
// Pixel Shader
// ============================================================================
struct PSInput {
    float4 position : SV_POSITION;
    float3 local_pos : TEXCOORD0;
};

Texture2D<float4> panorama_texture : register(t0);
SamplerState panorama_sampler : register(s0);

// Face index pushed from CPU
cbuffer FaceIndexCB : register(b0) {
    uint face_index;
};

// Convert cubemap face UV and face index to direction vector
float3 get_cubemap_direction(uint face, float2 uv) {
    // Convert UV from [0,1] to [-1,1]
    float2 pos = uv * 2.0 - 1.0;
    
    float3 dir;
    // DX11 cubemap face order: +X, -X, +Y, -Y, +Z, -Z
    switch (face) {
        case 0: // +X (Right)
            dir = float3(1.0, -pos.y, -pos.x);
            break;
        case 1: // -X (Left)
            dir = float3(-1.0, -pos.y, pos.x);
            break;
        case 2: // +Y (Top)
            dir = float3(pos.x, 1.0, pos.y);
            break;
        case 3: // -Y (Bottom)
            dir = float3(pos.x, -1.0, -pos.y);
            break;
        case 4: // +Z (Front)
            dir = float3(pos.x, -pos.y, 1.0);
            break;
        case 5: // -Z (Back)
            dir = float3(-pos.x, -pos.y, -1.0);
            break;
        default:
            dir = float3(0.0, 0.0, 1.0);
            break;
    }
    
    return normalize(dir);
}

// Convert direction vector to equirectangular UV
float2 direction_to_equirect_uv(float3 dir) {
    // atan2 returns angle in [-pi, pi]
    float phi = atan2(dir.z, dir.x);  // azimuth
    float theta = asin(clamp(dir.y, -1.0, 1.0));  // elevation
    
    // Convert to [0,1] UV space
    float u = phi / (2.0 * 3.14159265359) + 0.5;
    float v = theta / 3.14159265359 + 0.5;
    
    return float2(u, v);
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Calculate UV from pixel position
    float2 uv = input.position.xy;
    
    // Get direction for this cubemap texel
    float3 dir = get_cubemap_direction(face_index, uv);
    
    // Convert direction to equirectangular UV
    float2 panorama_uv = direction_to_equirect_uv(dir);
    
    // Sample the panorama texture
    float4 color = panorama_texture.SampleLevel(panorama_sampler, panorama_uv, 0);
    
    return color;
}
