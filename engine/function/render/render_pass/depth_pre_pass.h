#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include <vector>

namespace render {

class DepthPrePass : public RenderPass {
public:
    DepthPrePass() = default;
    ~DepthPrePass() override;

    void init() override;
    
    // Standard RDG build (empty)
    void build(RDGBuilder& builder) override;

    // Specific build method
    void build(RDGBuilder& builder, RDGTextureHandle depth_target, const std::vector<DrawBatch>& batches);

    PassType get_type() const override { return PassType::Depth; }
    std::string_view get_name() const override { return "DepthPrePass"; }

    void set_per_frame_data(const Mat4& view, const Mat4& proj);

private:
    void create_shaders();
    void create_pipeline();
    void create_uniform_buffers();

    // Shaders
    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;

    // Pipeline
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;

    // Buffers
    // Double/Triple buffering for per-frame data to avoid CPU-GPU sync issues
    static constexpr uint32_t kFramesInFlight = 3; 
    std::vector<RHIBufferRef> per_frame_buffers_;
    
    // For per-object, we currently use a single buffer per frame (sub-optimal)
    RHIBufferRef per_object_buffer_;

    struct PerFrameData {
        Mat4 view;
        Mat4 proj;
        Vec3 camera_pos;
        float _padding;
        Vec3 light_dir;
        float _padding2;
        Vec3 light_color;
        float light_intensity;
    } per_frame_data_;

    struct PerObjectData {
        Mat4 model;
        Mat4 inv_model;
    };

    bool initialized_ = false;
};

} // namespace render
