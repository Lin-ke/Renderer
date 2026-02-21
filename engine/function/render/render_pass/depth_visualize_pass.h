#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/core/math/math.h"

#include <memory>

namespace render {

struct DepthVisualizeConstants {
    float near_plane;
    float far_plane;
    float padding[2];
};

class DepthVisualizePass {
public:
    DepthVisualizePass();
    ~DepthVisualizePass();

    void init();
    void draw(RHICommandContextRef command, RHITextureRef depth_texture, RHITextureViewRef output_rtv, Extent2D extent, float near_plane, float far_plane);
    void destroy();
    
    bool is_initialized() const { return initialized_; }

private:
    void create_shaders();
    void create_pipeline();
    void create_constant_buffer();

    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;
    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    RHIBufferRef constant_buffer_;
    
    bool initialized_ = false;
};

} // namespace render
