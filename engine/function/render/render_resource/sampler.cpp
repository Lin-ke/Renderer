#include "engine/function/render/render_resource/sampler.h"
#include "engine/main/engine_context.h"
#include <cassert>

Sampler::Sampler() {
    auto rhi = EngineContext::rhi();
    assert(rhi && "RHI not initialized when creating Sampler");
    
    RHISamplerInfo info = {};
    info.min_filter = FILTER_TYPE_LINEAR;
    info.mag_filter = FILTER_TYPE_LINEAR;
    info.mipmap_mode = MIPMAP_MODE_LINEAR;
    info.address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE;
    info.address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE;
    info.address_mode_w = ADDRESS_MODE_CLAMP_TO_EDGE;
    info.compare_function = COMPARE_FUNCTION_NEVER;
    info.mip_lod_bias = 0.0f;
    info.max_anisotropy = 0.0f;

    sampler_ = rhi->create_sampler(info);
}

Sampler::Sampler(AddressMode address_mode, FilterType filter_type, MipMapMode mipmap_mode, float max_anisotropy, SamplerReductionMode reduction_mode) {
    auto rhi = EngineContext::rhi();
    assert(rhi && "RHI not initialized when creating Sampler");
    
    RHISamplerInfo info = {};
    info.min_filter = filter_type;
    info.mag_filter = filter_type;
    info.mipmap_mode = mipmap_mode;
    info.address_mode_u = address_mode;
    info.address_mode_v = address_mode;
    info.address_mode_w = address_mode;
    info.compare_function = COMPARE_FUNCTION_NEVER;
    info.reduction_mode = reduction_mode;
    info.mip_lod_bias = 0.0f;
    info.max_anisotropy = max_anisotropy;

    sampler_ = rhi->create_sampler(info);
}
