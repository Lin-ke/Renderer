#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/core/math/math.h"
#include <memory>
#include <optional>
#include <vector>

namespace render {

// Forward declarations
struct DrawBatch;

/**
 * @brief Output handles from G-Buffer pass for downstream passes
 */
struct GBufferOutputHandles {
    RDGTextureHandle albedo_ao;
    RDGTextureHandle normal_roughness;
    RDGTextureHandle material_emission;
    RDGTextureHandle position_depth;
};

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
 * @brief Material data for G-Buffer pass (matches HLSL cbuffer)
 * 
 * Texture slots:
 * - t0: Albedo/Diffuse map
 * - t1: Normal map
 * - t2: ARM map (AO=R, Roughness=G, Metallic=B) - preferred
 * - t3: Roughness map (when ARM not available)
 * - t4: Metallic map (when ARM not available)
 * - t5: AO map (when ARM not available)
 * - t6: Emission map
 * 
 * Layout (64 bytes, 16-byte aligned):
 * - offset 0:  albedo (Vec4)
 * - offset 16: roughness (float)
 * - offset 20: metallic (float)
 * - offset 24: emission (float)
 * - offset 28: alpha_clip (float)
 * - offset 32: specular (float)
 * - offset 36: use_albedo_map (float)
 * - offset 40: use_normal_map (float)
 * - offset 44: use_arm_map (float)
 * - offset 48: use_roughness_map (float)
 * - offset 52: use_metallic_map (float)
 * - offset 56: use_ao_map (float)
 * - offset 60: use_emission_map (float)
 */
struct alignas(16) GBufferMaterialData {
    Vec4 albedo;                 // 16 bytes (offset 0)
    float roughness;             // 4 bytes (offset 16)
    float metallic;              // 4 bytes (offset 20)
    float emission;              // 4 bytes (offset 24)
    float alpha_clip;            // 4 bytes (offset 28)
    float specular;              // 4 bytes (offset 32)
    float use_albedo_map;        // 4 bytes (offset 36)
    float use_normal_map;        // 4 bytes (offset 40)
    float use_arm_map;           // 4 bytes (offset 44)
    float use_roughness_map;     // 4 bytes (offset 48)
    float use_metallic_map;      // 4 bytes (offset 52)
    float use_ao_map;            // 4 bytes (offset 56)
    float use_emission_map;      // 4 bytes (offset 60), total = 64 bytes (16 aligned)
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
    
    /**
     * @brief Build G-Buffer pass with explicit batches and depth dependency
     * @param builder RDG builder
     * @param depth_target Depth texture from DepthPrePass (read-only, LOAD op)
     * @param batches Draw batches to render
     * @return Output handles for downstream passes (e.g. DeferredLightingPass)
     */
    std::optional<GBufferOutputHandles> build(RDGBuilder& builder, RDGTextureHandle depth_target, 
               const std::vector<DrawBatch>& batches);

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

    void create_samplers();

    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;

    // Uniform buffers
    RHIBufferRef per_frame_buffer_;   // Slot b0: view, proj, camera
    RHIBufferRef per_object_buffer_;  // Slot b1: model matrix
    RHIBufferRef material_buffer_;    // Slot b2: material data

    // Sampler
    RHISamplerRef default_sampler_;   // Slot s0

    GBufferPerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;

    // Stored batches for deferred rendering
    std::vector<DrawBatch> current_batches_;

    bool initialized_ = false;
};

using GBufferPassRef = std::shared_ptr<GBufferPass>;

} // namespace render
