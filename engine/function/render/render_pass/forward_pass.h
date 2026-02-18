#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/core/math/math.h"
#include <memory>
#include <vector>

namespace render {

// Forward declarations
struct DrawBatch;

/**
 * @brief Per-frame uniform data (view, projection, camera, lights)
 */
struct PerFrameData {
    Mat4 view;
    Mat4 proj;
    Vec3 camera_pos;
    float _padding;
    
    // Light data (simple Blinn-Phong with single directional light)
    Vec3 light_dir;
    float _padding2;
    Vec3 light_color;
    float light_intensity;
};

/**
 * @brief Per-object uniform data (model matrix)
 */
struct PerObjectData {
    Mat4 model;
    Mat4 inv_model;
};

/**
 * @brief Forward rendering pass
 * 
 * Renders meshes with simple forward shading using uniform buffers
 * for MVP matrices.
 */
class ForwardPass : public RenderPass {
public:
    ForwardPass();
    ~ForwardPass() override;

    void init() override;
    void build(RDGBuilder& builder) override;

    std::string_view get_name() const override { return "ForwardPass"; }
    PassType get_type() const override { return PassType::Forward; }

    /**
     * @brief Update per-frame uniforms (view, proj, camera, lights)
     * @param view View matrix
     * @param proj Projection matrix
     * @param camera_pos Camera position in world space
     * @param light_dir Light direction
     * @param light_color Light color
     * @param light_intensity Light intensity
     */
    void set_per_frame_data(const Mat4& view, const Mat4& proj, const Vec3& camera_pos,
                            const Vec3& light_dir = Vec3(0.0f, -1.0f, 0.0f),
                            const Vec3& light_color = Vec3(1.0f, 1.0f, 1.0f),
                            float light_intensity = 1.0f);

    /**
     * @brief Draw a single batch
     * @param command Command context to record to
     * @param batch Draw batch data
     */
    void draw_batch(RHICommandContextRef command, const DrawBatch& batch);

    /**
     * @brief Check if pass is ready to render
     */
    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }

    /**
     * @brief Set wireframe mode
     * @param enable true for wireframe, false for solid
     */
    void set_wireframe(bool enable);

private:
    void create_shaders();
    void create_pipeline();
    void create_uniform_buffers();

    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    RHIGraphicsPipelineRef pipeline_;        // Current active pipeline
    RHIGraphicsPipelineRef solid_pipeline_;  // Solid fill pipeline
    RHIGraphicsPipelineRef wireframe_pipeline_; // Wireframe pipeline
    RHIRootSignatureRef root_signature_;
    
    bool wireframe_mode_ = false;

    // Uniform buffers
    RHIBufferRef per_frame_buffer_;  // Slot b0: view, proj, camera_pos, lights
    RHIBufferRef per_object_buffer_; // Slot b1: model matrix

    PerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;

    bool initialized_ = false;
};

} // namespace render
