struct SceneInstance {
    float4x4 model;
    float4 bounds_min;
    float4 bounds_max;
    float4 color;
    uint index_count;
    uint first_index;
    int vertex_offset;
    uint object_id;
};

cbuffer View : register(b0) {
    float4x4 view_projection;
    uint instance_count;
    uint3 padding;
};
StructuredBuffer<SceneInstance> instances : register(t0);
RWByteAddressBuffer arguments : register(u0);
RWStructuredBuffer<uint> visible_ids : register(u1);
RWByteAddressBuffer draw_count : register(u2);

[numthreads(1,1,1)]
void ResetCS(uint3 id : SV_DispatchThreadID) {
    draw_count.Store(0,0);
}

[numthreads(64,1,1)]
void CullCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= instance_count) return;
    SceneInstance item = instances[id.x];
    if (!item.index_count) return;
    float4x4 local_to_clip = mul(view_projection,item.model);
    uint outside = 63;
    [unroll] for (uint corner = 0; corner < 8; ++corner) {
        float3 corner_position = float3((corner & 1) ? item.bounds_max.x : item.bounds_min.x,
            (corner & 2) ? item.bounds_max.y : item.bounds_min.y,
            (corner & 4) ? item.bounds_max.z : item.bounds_min.z);
        float4 clip = mul(local_to_clip,float4(corner_position,1));
        float epsilon = 1e-5 * max(1.0,dot(abs(clip),float4(1,1,1,1)));
        uint mask = (clip.x < -clip.w-epsilon ? 1u : 0u) | (clip.x > clip.w+epsilon ? 2u : 0u) |
            (clip.y < -clip.w-epsilon ? 4u : 0u) | (clip.y > clip.w+epsilon ? 8u : 0u) |
            (clip.z < -epsilon ? 16u : 0u) | (clip.z > clip.w+epsilon ? 32u : 0u);
        outside &= mask;
    }
    if (outside) return;
    uint output_index;
    draw_count.InterlockedAdd(0,1,output_index);
    // At most one command is emitted per candidate, so the output fits its capacity.
    arguments.Store4(output_index*20,uint4(item.index_count,1,item.first_index,asuint(item.vertex_offset)));
    arguments.Store(output_index*20+16,output_index);
    visible_ids[output_index] = id.x;
}

struct RasterInput {
    float3 position : POSITION0;
    uint scene_index : INSTANCE_ID0;
};
struct RasterOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};
RasterOutput VSMain(RasterInput input) {
    SceneInstance item = instances[input.scene_index];
    RasterOutput output;
    output.position = mul(view_projection,mul(item.model,float4(input.position,1)));
    output.color = item.color;
    return output;
}
float4 PSMain(RasterOutput input) : SV_Target0 {
    return input.color;
}
