Texture2D<float4> input_color : register(t0);
Texture2D<float4> bloom_color : register(t1);
RWTexture2D<float4> output_color : register(u0);
SamplerState linear_clamp : register(s0);

cbuffer BloomParameters : register(b0) {
    uint2 output_extent;
    float amount;
    uint horizontal;
};

[numthreads(8, 8, 1)]
void BrightCS(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= output_extent)) return;

    uint width, height;
    input_color.GetDimensions(width, height);
    float3 bright = 0.0;
    uint samples = 0;
    for (uint y = 0; y < 2; ++y) {
        for (uint x = 0; x < 2; ++x) {
            uint2 pixel = id.xy * 2 + uint2(x, y);
            if (pixel.x < width && pixel.y < height) {
                float3 color = max(input_color.Load(int3(pixel, 0)).rgb, 0.0);
                // Scene lighting currently produces gamma-encoded display color.
                color = pow(color, 2.2);
                float brightness = max(color.r, max(color.g, color.b));
                bright += color * max(brightness - amount, 0.0) / max(brightness, 0.00001);
                ++samples;
            }
        }
    }
    output_color[id.xy] = float4(bright / samples, 1.0);
}

[numthreads(8, 8, 1)]
void BlurCS(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= output_extent)) return;

    static const float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    int2 axis = horizontal ? int2(1, 0) : int2(0, 1);
    int2 last_pixel = int2(output_extent) - 1;
    float3 color = input_color.Load(int3(id.xy, 0)).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        int2 positive = clamp(int2(id.xy) + axis * i, 0, last_pixel);
        int2 negative = clamp(int2(id.xy) - axis * i, 0, last_pixel);
        color += (input_color.Load(int3(positive, 0)).rgb +
                  input_color.Load(int3(negative, 0)).rgb) * weights[i];
    }
    output_color[id.xy] = float4(color, 1.0);
}

struct CompositeVertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

CompositeVertex CompositeVS(uint vertex_id : SV_VertexID) {
    CompositeVertex vertex;
    vertex.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    vertex.position = float4(vertex.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return vertex;
}

float4 CompositePS(CompositeVertex input) : SV_Target {
    float4 scene = input_color.Load(int3(int2(input.position.xy), 0));
    float3 bloom = bloom_color.SampleLevel(linear_clamp, input.uv, 0).rgb;
    float3 linear_color = pow(max(scene.rgb, 0.0), 2.2) + bloom * amount;
    return float4(pow(max(linear_color, 0.0), 1.0 / 2.2), scene.a);
}
