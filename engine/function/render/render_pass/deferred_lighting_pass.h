#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/core/math/math.h"
#include <memory>
#include <vector>

namespace render {

// Forward declarations
struct DrawBatch;

/**
 * @brief Light type enumeration
 */
enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2
};

/**
 * @brief Light data for shader (matches HLSL Light struct)
 */
struct ShaderLightData {
    Vec3 position;      // For point/spot: world position
    float _padding0;
    
    Vec3 color;
    float intensity;
    
    Vec3 direction;     // For directional/spot: light travel direction
    float range;        // For point/spot
    
    uint32_t type;      // LightType
    float inner_angle;  // For spot light (cosine)
    float outer_angle;  // For spot light (cosine)
    float _padding1;
};

static constexpr uint32_t MAX_LIGHTS = 32;

/**
 * @brief Per-frame data for deferred lighting
 */
struct DeferredLightingPerFrameData {
    Vec3 camera_pos;
    float _padding0;
    
    uint32_t light_count;
    float _padding1[3];
    
    // First directional light (main light)
    Vec3 main_light_dir;
    float _padding2;
    Vec3 main_light_color;
    float main_light_intensity;
    
    Mat4 inv_view_proj;  // For reconstructing world position from depth
};

/**
 * @brief Deferred lighting pass
 * 
 * Computes lighting from G-Buffer data using PBR BRDF.
 * Supports multiple light types and IBL.
 */
class DeferredLightingPass : public RenderPass {
public:
    DeferredLightingPass();
    ~DeferredLightingPass() override;

    void init() override;
    void build(RDGBuilder& builder) override;
    void build(RDGBuilder& builder, RDGTextureHandle color_target);
    void build(RDGBuilder& builder, const GBufferOutputHandles& gbuffer);
    void build(RDGBuilder& builder, RDGTextureHandle color_target, const GBufferOutputHandles& gbuffer);

    std::string_view get_name() const override { return "DeferredLightingPass"; }
    PassType get_type() const override { return PassType::DeferredLighting; }

    /**
     * @brief Update per-frame data
     */
    void set_per_frame_data(const Vec3& camera_pos, const Mat4& inv_view_proj);
    
    /**
     * @brief Set main directional light
     */
    void set_main_light(const Vec3& dir, const Vec3& color, float intensity);
    
    /**
     * @brief Set additional lights (point, spot, extra directional)
     */
    void set_lights(const std::vector<ShaderLightData>& lights);
    
    /**
     * @brief Check if pass is ready
     */
    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }

    /**
     * @brief Get output format
     */
    static RHIFormat get_hdr_format() { return FORMAT_R16G16B16A16_SFLOAT; }

private:
    void create_shaders();
    void create_pipeline();
    void create_uniform_buffers();
    void create_quad_geometry();

    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;
    
    // Full-screen quad
    RHIBufferRef quad_vertex_buffer_;
    RHIBufferRef quad_index_buffer_;
    
    // Uniform buffers
    RHIBufferRef per_frame_buffer_;
    RHIBufferRef light_buffer_;  // SSBO for multiple lights
    
    // Sampler for GBuffer textures
    RHISamplerRef gbuffer_sampler_;
    
    DeferredLightingPerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;
    
    std::vector<ShaderLightData> lights_data_;
    bool lights_dirty_ = true;
    
    bool initialized_ = false;
};

using DeferredLightingPassRef = std::shared_ptr<DeferredLightingPass>;

} // namespace render
