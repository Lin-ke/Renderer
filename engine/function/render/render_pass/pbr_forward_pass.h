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
    int point_light_count;
    Vec3 _padding3;
};

// PBR Per-object data structure (matches HLSL cbuffer)
struct PBRPerObjectData {
    Mat4 model;
    Mat4 inv_model;
};

// PBR Material data structure (matches HLSL cbuffer)
struct PBRMaterialData {
    Vec4 albedo;           // base color
    Vec4 emission;
    float roughness;
    float metallic;
    float alpha_cutoff;
    int use_albedo_map;
    int use_normal_map;
    int use_arm_map;       // AO/Roughness/Metallic map
    int use_emission_map;
    Vec2 _padding;
};

class PBRForwardPass : public RenderPass {
public:
    PBRForwardPass();
    ~PBRForwardPass() override;
    
    void init() override;
    void build(RDGBuilder& builder) override;
    
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
     * @brief Draw a single batch using PBR
     * @param command Command context to record to
     * @param batch Draw batch data
     */
    void draw_batch(RHICommandContextRef command, const DrawBatch& batch);

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
    
    // Default vertex buffers for meshes missing tangent/texcoord
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
