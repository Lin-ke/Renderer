#pragma once

#include "engine/function/render/render_system/gpu_scene.h"

namespace render {

// Stage A: static geometry, per-object solid color, GPU frustum culling and indexed indirect rasterization.
class GpuDrivenPass {
public:
    bool init();
    std::shared_ptr<GpuSceneFrame> build(RDGBuilder& graph, const std::vector<DrawBatch>& instances,
        const Mat4& view_projection, RDGTextureHandle color, RDGTextureHandle depth, Extent2D extent, bool debug_colors = false);
    bool prepare(GpuSceneFrame& frame, RDGBuilder& graph, uint32_t frame_slot);
private:
    GpuScene scene_;
    RHIRootSignatureRef reset_root_, cull_root_, raster_root_;
    RHIComputePipelineRef reset_pipeline_, cull_pipeline_;
    RHIGraphicsPipelineRef raster_pipeline_;
};

}
