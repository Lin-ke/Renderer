// Depth Pre-Pass Shader

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

struct VSInput {
    float3 position : POSITION0;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.position = mul(proj, mul(view, world_pos));
    return output;
}

void PSMain(VSOutput input) {
    // Depth-only pass, no color output needed
}
