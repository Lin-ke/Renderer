#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/rhi/rhi.h"
#include <memory>

class Sampler {
public:
    Sampler();
    Sampler(AddressMode address_mode,
            FilterType filter_type = FILTER_TYPE_LINEAR,
            MipMapMode mipmap_mode = MIPMAP_MODE_LINEAR,
            float max_anisotropy = 0.0f,
            SamplerReductionMode reduction_mode = SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE);
    ~Sampler() = default;

    RHISamplerRef sampler_;
};
using SamplerRef = std::shared_ptr<Sampler>;
