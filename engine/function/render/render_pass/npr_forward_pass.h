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

// NPR Per-frame data structure (matches HLSL cbuffer)
struct NPRPerFrameData {
    Mat4 view;
    Mat4 proj;
    Vec3 camera_pos;
    float _padding;
    
    // Directional light
    Vec3 light_dir;
    float _padding2;
    Vec3 light_color;
    float light_intensity;
};

// NPR Per-object data structure (matches HLSL cbuffer)
struct NPRPerObjectData {
    Mat4 model;
    Mat4 inv_model;
};

// NPR Material data structure (matches HLSL cbuffer)
struct NPRMaterialData {
    Vec4 albedo;           // base color
    Vec4 emission;
    
    // NPR specific parameters
    float lambert_clamp;   // Half lambert clamp threshold
    float ramp_tex_offset; // Ramp texture vertical offset (for material type)
    float rim_threshold;   // Rim light depth threshold
    float rim_strength;    // Rim light intensity
    float rim_width;       // Rim light screen space width
    float use_albedo_map;  // Use float for boolean flags
    float use_normal_map;
    float use_light_map;   // LightMap (metallic, ao, specular, materialType)
    float use_ramp_map;    // Ramp texture for toon shading
    Vec3 rim_color;        // Rim light color
    float _padding;
};

class NPRForwardPass : public RenderPass {
public:
    NPRForwardPass();
    ~NPRForwardPass() override;
    
    void init() override;
    
    void set_wireframe(bool enable);
    void set_per_frame_data(const Mat4& view, const Mat4& proj, 
                            const Vec3& camera_pos,
                            const Vec3& light_dir,
                            const Vec3& light_color,
                            float light_intensity);
    
    /**
     * @brief Draw a single batch
     */
    void draw_batch(RHICommandContextRef command, const DrawBatch& batch);

    /**
     * @brief Execute rendering of batches directly
     */
    void execute_batches(RHICommandListRef command, const std::vector<DrawBatch>& batches);

    /**
     * @brief Build the render pass into the RDG
     */
    void build(RDGBuilder& builder, RDGTextureHandle color_target, 
               RDGTextureHandle depth_target,
               const std::vector<DrawBatch>& batches);

    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }
    bool is_initialized() const { return initialized_; }
    
    RHIGraphicsPipelineRef get_pipeline() const { return pipeline_; }
    
    std::string_view get_name() const override { return "NPRForwardPass"; }
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
    RHIBufferRef default_normal_buffer_;
    RHIBufferRef default_tangent_buffer_;
    RHIBufferRef default_texcoord_buffer_;
    static constexpr uint32_t DEFAULT_VERTEX_COUNT = 65536;
    
    // Data
    NPRPerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;
    bool wireframe_mode_ = false;
    bool initialized_ = false;
};

} // namespace render
