// NPR Forward Pass Shaders (Toon/Cel Shading)
// Based on ToyRenderer's npr_klee.frag

// ============================================================================
// Constant Buffers
// ============================================================================
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

cbuffer Material : register(b2) {
    float4 albedo;
    float4 emission;
    
    // NPR specific parameters packed into vec4 for alignment
    // npr_params1: lambert_clamp, ramp_tex_offset, rim_threshold, rim_strength
    float4 npr_params1;
    
    // npr_params2: rim_width, use_albedo_map, use_normal_map, use_light_map
    float4 npr_params2;
    
    // rim_color_and_use_ramp: rim_color (xyz), use_ramp_map (w)
    float4 rim_color_and_use_ramp;
    
    // face_mode: 1.0 = face mode (unlit, output albedo directly), 0.0 = normal NPR
    float face_mode;
    float3 _padding_mat;
};

// Helper macros to access packed parameters
#define lambert_clamp      npr_params1.x
#define ramp_tex_offset    npr_params1.y
#define rim_threshold      npr_params1.z
#define rim_strength       npr_params1.w
#define rim_width          npr_params2.x
#define use_albedo_map     npr_params2.y
#define use_normal_map     npr_params2.z
#define use_light_map      npr_params2.w
#define rim_color          rim_color_and_use_ramp.xyz
#define use_ramp_map       rim_color_and_use_ramp.w

// ============================================================================
// Textures and Samplers
// ============================================================================
Texture2D albedo_map : register(t0);
Texture2D normal_map : register(t1);
Texture2D light_map : register(t2);    // LightMap: R=metallic, G=ao, B=specular, A=materialType
Texture2D ramp_map : register(t3);     // Ramp texture for toon shading
Texture2D depth_texture : register(t4); // Screen space depth for rim light

SamplerState default_sampler : register(s0);
SamplerState clamp_sampler : register(s1); // For ramp texture (clamp to edge)

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
    float4 clip_pos : TEXCOORD1;    // For screen space operations
    float3 view_dir : TEXCOORD2;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Use default values if tangent is not provided
    float4 tangent_val = input.tangent;
    if (length(tangent_val.xyz) < 0.001) {
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
    output.clip_pos = output.position;
    
    // Transform normal to world space
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // Transform tangent to world space
    float3 world_tangent = mul((float3x3)model, tangent_val.xyz);
    output.world_tangent = float4(normalize(world_tangent), tangent_val.w);
    
    // Pass through texcoord
    output.texcoord = input.texcoord;
    
    // View direction for fresnel
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
    float4 clip_pos : TEXCOORD1;
    float3 view_dir : TEXCOORD2;
};

// Get normal from normal map or use vertex normal
float3 GetNormal(PSInput input) {
    float3 N = normalize(input.world_normal);
    
    if (use_normal_map > 0.5) {
        float3 tangent_normal = normal_map.Sample(default_sampler, input.texcoord).xyz * 2.0 - 1.0;
        float3 T = normalize(input.world_tangent.xyz);
        float3 B = cross(N, T) * input.world_tangent.w;
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(TBN, tangent_normal));
    }
    
    return N;
}

// Linearize depth value
float Linear01Depth(float depth, float near_plane, float far_plane) {
    // Reverse Z depth buffer (DX11 typical)
    return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
}

// Calculate screen space rim light
float CalculateRim(PSInput input, float3 N) {
    // Calculate screen space position (flip Y: clip Y+ is up, but UV V=0 is top)
    float2 ndc = input.clip_pos.xy / input.clip_pos.w;
    float2 screen_uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    
    // Sample depth at current pixel
    float depth = depth_texture.Sample(default_sampler, screen_uv).r;
    // Note: In real implementation, we'd need proper near/far planes
    // For now, use depth directly for comparison
    
    // Offset position along normal in world space, then project to screen
    float3 offset_world_pos = input.world_pos + rim_width * normalize(input.world_normal);
    float4 offset_view_pos = mul(view, float4(offset_world_pos, 1.0));
    float4 offset_clip_pos = mul(proj, offset_view_pos);
    float2 offset_ndc = offset_clip_pos.xy / offset_clip_pos.w;
    float2 offset_screen_uv = float2(offset_ndc.x * 0.5 + 0.5, -offset_ndc.y * 0.5 + 0.5);
    
    // Sample depth at offset position
    float offset_depth = depth_texture.Sample(default_sampler, offset_screen_uv).r;
    
    // Rim factor based on depth difference
    float rim = step(rim_threshold, offset_depth - depth);
    
    // Fresnel effect
    float3 V = normalize(input.view_dir);
    float fresnel_power = 6.0;
    float fresnel_clamp = 0.8;
    float fresnel = 1.0 - saturate(dot(V, N));
    fresnel = pow(fresnel, fresnel_power);
    fresnel = fresnel * fresnel_clamp + (1.0 - fresnel_clamp);
    
    return rim * fresnel;
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Get base color
    float3 base_color = albedo.rgb;
    if (use_albedo_map > 0.5) {
        float4 sampled = albedo_map.Sample(default_sampler, input.texcoord);
        base_color *= sampled.rgb;
    }
    
    // Face mode: output albedo directly without any lighting (reference: npr_klee_face.frag)
    if (face_mode > 0.5) {
        // Gamma correction for display (render target is UNORM, not sRGB)
        float3 face_color = pow(base_color, 1.0 / 2.2);
        return float4(face_color, 1.0);
    }
    
    float2 ndc = input.clip_pos.xy / input.clip_pos.w;
    float2 screen_uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    // Get normal
    float3 N = GetNormal(input);
    
    // Get LightMap data (default values if no light map)
    float metallic = 0.0;
    float ao = 0.5;      // 0.5 = neutral
    float specular = 0.5;
    float material_type = 0.5;
    
    if (use_light_map > 0.5) {
        float4 light_data = light_map.Sample(default_sampler, input.texcoord);
        // Gamma correction on light map (match reference: lightMap = pow(lightMap, vec4(1.0/2.2)))
        light_data = pow(light_data, 1.0 / 2.2);
        metallic = light_data.r;
        ao = light_data.g;
        specular = light_data.b;
        material_type = light_data.a;
    }
    
    // Directional light calculation
    float3 dir_color = float3(0.0, 0.0, 0.0);
    {
        float3 L = normalize(-light_dir);
        
        // Half Lambert lighting
        float ao_shadow = ao * 2.0;  // AO shadow factor
        float lambert = max(0.0, dot(N, L));
        float half_lambert = max(0.0, min(1.0, (lambert * 0.5 + 0.5) * ao_shadow));
        
        // Smoothstep for toon look
        half_lambert = smoothstep(0.0, lambert_clamp, half_lambert);
        
        // Sample ramp texture
        float3 surface_color;
        if (use_ramp_map > 0.5) {
            // Get ramp texture dimensions
            int2 ramp_size;
            ramp_map.GetDimensions(ramp_size.x, ramp_size.y);
            
            // Calculate ramp UV
            float ramp_x = half_lambert;
            // Material type determines which row to sample (0.3, 0.5, 0.7, 1.0 map to rows)
            float material_row = (1.0 - material_type) * 4.0 + 0.5 + ramp_tex_offset;
            float ramp_y = (1.0 / float(ramp_size.y)) * material_row;
            
            float4 ramp = ramp_map.Sample(clamp_sampler, float2(ramp_x, ramp_y));
            ramp = pow(ramp, 1.0 / 2.2);
            surface_color = ramp.rgb * base_color;
        } else {
            // No ramp map, use simple half lambert
            surface_color = half_lambert * base_color;
        }
        
        // NPR does NOT multiply by light_intensity (reference: dirColor += max(vec3(0.0f), surfaceColor))
        // light_intensity is for PBR; NPR uses artistic ramp/half-lambert control instead
        dir_color = max(float3(0.0, 0.0, 0.0), surface_color) * light_color;
    }


    
    // Screen space rim light (match reference: outRimColor * rim + dirColor * (1.0 - rim))
    float rim_factor = 0.0;
    float3 rim_base = float3(0.0, 0.0, 0.0);
    if (rim_strength > 0.0) {
        rim_factor = CalculateRim(input, N);
        rim_base = rim_color * rim_strength * base_color;
    }

    // Combine lighting: lerp between dir_color and rim using rim_factor
    float3 final_color = rim_base * rim_factor + dir_color * (1.0 - rim_factor);
    
    // Add emission
    final_color += emission.rgb;
    
    // Gamma correction
    final_color = pow(final_color, 1.0 / 2.2);
    
    return float4(final_color, 1.0);
}
