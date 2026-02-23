#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/core/math/math.h"
#include <memory>

namespace render {

// Forward declarations
struct DrawBatch;

// PBR Per-frame data structure (matches HLSL cbuffer)
struct PBRPerFrameData {
    Mat4 view;
    Mat4 proj;
    Vec3 camera_pos;
    float _padding;
    
    // Directional light
    Vec3 light_dir;
    float _padding2;
    Vec3 light_color;
    float light_intensity;
    
    // Point lights (up to 4 for simple implementation)
    Vec4 point_light_pos[4];      // xyz = position, w = range
    Vec4 point_light_color[4];    // rgb = color, a = intensity
    int point_light_count = 0;
    Vec3 _padding3;
};

// PBR Per-object data structure (matches HLSL cbuffer)
struct PBRPerObjectData {
    Mat4 model;
    Mat4 inv_model;
};

// PBR Material data structure (matches HLSL cbuffer)
// Use float for boolean flags to match HLSL alignment
// Total size must be 16-byte aligned for DX11 constant buffers
struct PBRMaterialData {
    Vec4 albedo;           // base color (16 bytes)
    Vec4 emission;         // (16 bytes)
    float roughness;       // (4 bytes)
    float metallic;        // (4 bytes)
    float alpha_cutoff;    // (4 bytes)
    float use_albedo_map;  // (4 bytes)
    float use_normal_map;  // (4 bytes)
    float use_arm_map;     // AO/Roughness/Metallic map (4 bytes)
    float use_emission_map;// (4 bytes)
    float _padding;        // (4 bytes) - padding to ensure 16-byte alignment
    // Total: 64 bytes (16 * 4), perfectly aligned
};

class PBRForwardPass : public RenderPass {
public:
    PBRForwardPass();
    ~PBRForwardPass() override;
    
    void init() override;
    
    void set_wireframe(bool enable);
    void set_per_frame_data(const Mat4& view, const Mat4& proj, 
                            const Vec3& camera_pos,
                            const Vec3& light_dir,
                            const Vec3& light_color,
                            float light_intensity);
    
    // Point lights
    void add_point_light(const Vec3& pos, const Vec3& color, float intensity, float range);
    void clear_point_lights();
    
    /**
     * @brief Draw a single batch (legacy method for backward compatibility)
     * @param command Command context to record to
     * @param batch Draw batch data
     */
    void draw_batch(RHICommandContextRef command, const DrawBatch& batch);

    /**
     * @brief Execute rendering of batches directly (for non-RDG rendering path)
     * @param command Command list to record to
     * @param batches Draw batches to render
     */
    void execute_batches(RHICommandListRef command, const std::vector<DrawBatch>& batches);

    /**
     * @brief Build the render pass into the RDG
     * @param builder RDG builder
     * @param color_target Color attachment target
     * @param depth_target Depth attachment target (optional)
     * @param batches Draw batches to render
     */
    void build(RDGBuilder& builder, RDGTextureHandle color_target, 
               std::optional<RDGTextureHandle> depth_target,
               const std::vector<DrawBatch>& batches);

    // Public interface for RenderMeshManager
    /**
     * @brief Check if pass is ready to render
     */
    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }

    bool is_initialized() const { return initialized_; }
    
    RHIGraphicsPipelineRef get_pipeline() const { return pipeline_; }
    
    std::string_view get_name() const override { return "PBRForwardPass"; }
    PassType get_type() const override { return PassType::Forward; }
    
private:
    void create_shaders();
    void create_uniform_buffers();
    void create_pipeline();
    void create_samplers();
    void create_default_vertex_buffers();
    
    // Shaders
    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;
    
    // Pipelines
    RHIGraphicsPipelineRef solid_pipeline_;
    RHIGraphicsPipelineRef wireframe_pipeline_;
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;
    
    // Uniform buffers
    RHIBufferRef per_frame_buffer_;
    RHIBufferRef per_object_buffer_;
    RHIBufferRef material_buffer_;
    
    // Samplers
    RHISamplerRef default_sampler_;
    
    // Default vertex buffers for meshes missing attributes
    RHIBufferRef default_normal_buffer_;  // Default normal (0,1,0)
    RHIBufferRef default_tangent_buffer_;
    RHIBufferRef default_texcoord_buffer_;
    static constexpr uint32_t DEFAULT_VERTEX_COUNT = 65536; // Max vertices for default buffers
    
    // Data
    PBRPerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;
    bool wireframe_mode_ = false;
    bool initialized_ = false;
};

} // namespace render
