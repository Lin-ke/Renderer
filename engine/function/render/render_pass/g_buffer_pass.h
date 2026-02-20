#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/core/math/math.h"
#include <memory>

namespace render {

// Forward declarations
struct DrawBatch;

/**
 * @brief G-Buffer data structure for deferred rendering
 * 
 * Layout:
 * - RT0: Albedo (RGB) + AO (A)
 * - RT1: Normal (RGB) + roughness (A) 
 * - RT2: Metallic (R) + emission (G) + specular (B) + _padding (A)
 * - RT3: World Position (RGB) + depth (A)
 */
struct GBufferData {
    static constexpr uint32_t ALBEDO_AO_INDEX = 0;
    static constexpr uint32_t NORMAL_ROUGHNESS_INDEX = 1;
    static constexpr uint32_t MATERIAL_EMISSION_INDEX = 2;
    static constexpr uint32_t POSITION_DEPTH_INDEX = 3;
    static constexpr uint32_t COUNT = 4;
};

/**
 * @brief Per-frame uniform data for G-Buffer pass
 */
struct GBufferPerFrameData {
    Mat4 view;
    Mat4 proj;
    Vec3 camera_pos;
    float _padding;
};

/**
 * @brief Per-object uniform data for G-Buffer pass
 */
struct GBufferPerObjectData {
    Mat4 model;
    Mat4 inv_model;
};

/**
 * @brief G-Buffer rendering pass for deferred shading
 * 
 * Renders scene geometry into multiple render targets for deferred lighting.
 */
class GBufferPass : public RenderPass {
public:
    GBufferPass();
    ~GBufferPass() override;

    void init() override;
    void build(RDGBuilder& builder) override;

    std::string_view get_name() const override { return "GBufferPass"; }
    PassType get_type() const override { return PassType::GBuffer; }

    /**
     * @brief Update per-frame uniforms
     */
    void set_per_frame_data(const Mat4& view, const Mat4& proj, const Vec3& camera_pos);

    /**
     * @brief Check if pass is ready
     */
    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }

    /**
     * @brief Get G-Buffer texture formats
     */
    static RHIFormat get_albedo_ao_format() { return FORMAT_R8G8B8A8_UNORM; }
    static RHIFormat get_normal_roughness_format() { return FORMAT_R8G8B8A8_UNORM; }
    static RHIFormat get_material_emission_format() { return FORMAT_R8G8B8A8_UNORM; }
    static RHIFormat get_position_depth_format() { return FORMAT_R32G32B32A32_SFLOAT; }
    static RHIFormat get_depth_format() { return FORMAT_D32_SFLOAT; }

private:
    void create_shaders();
    void create_pipeline();
    void create_uniform_buffers();

    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;

    // Uniform buffers
    RHIBufferRef per_frame_buffer_;   // Slot b0: view, proj, camera
    RHIBufferRef per_object_buffer_;  // Slot b1: model matrix

    GBufferPerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;

    bool initialized_ = false;
};

using GBufferPassRef = std::shared_ptr<GBufferPass>;

} // namespace render
