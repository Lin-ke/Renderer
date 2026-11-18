#pragma once

#include "engine/function/render/render_pass/render_pass.h"

namespace render {

struct BloomSettings {
    bool enabled = false;
    float threshold = 0.25f;
    float intensity = 0.8f;
};

class BloomPass : public RenderPass {
public:
    void init() override;
    bool is_ready() const;
    bool build(RDGBuilder& graph, RDGTextureHandle scene_color, RDGTextureHandle final_color,
               Extent2D extent, const BloomSettings& settings) const;

    PassType get_type() const override { return PassType::PostProcess; }
    std::string_view get_name() const override { return "BloomPass"; }

private:
    RHIRootSignatureRef compute_root_;
    RHIRootSignatureRef composite_root_;
    RHIComputePipelineRef bright_pipeline_;
    RHIComputePipelineRef blur_pipeline_;
    RHIGraphicsPipelineRef composite_pipeline_;
    RHISamplerRef sampler_;
};

} // namespace render
