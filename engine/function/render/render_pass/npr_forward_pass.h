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
// Size must be 16-byte aligned for DX11 constant buffers
struct NPRMaterialData {
    Vec4 albedo;           // 16 bytes - base color
    Vec4 emission;         // 16 bytes
    
    // Pack floats into Vec4 for better alignment (16 bytes)
    // lambert_clamp, ramp_tex_offset, rim_threshold, rim_strength
    Vec4 npr_params1;
    
    // rim_width, use_albedo_map, use_normal_map, use_light_map
    Vec4 npr_params2;
    
    // use_ramp_map + rim_color (3 floats) = fits in one Vec4
    // use_ramp_map is in w component
    Vec4 rim_color_and_use_ramp;
    
    // Additional padding to ensure 16-byte alignment (80 bytes so far, need 16 more)
    float _padding[4];     // 16 bytes padding
};

// Helper to access NPRMaterialData fields
inline float get_lambert_clamp(const NPRMaterialData& data) { return data.npr_params1.x; }
inline float get_ramp_tex_offset(const NPRMaterialData& data) { return data.npr_params1.y; }
inline float get_rim_threshold(const NPRMaterialData& data) { return data.npr_params1.z; }
inline float get_rim_strength(const NPRMaterialData& data) { return data.npr_params1.w; }
inline float get_rim_width(const NPRMaterialData& data) { return data.npr_params2.x; }
inline float get_use_albedo_map(const NPRMaterialData& data) { return data.npr_params2.y; }
inline float get_use_normal_map(const NPRMaterialData& data) { return data.npr_params2.z; }
inline float get_use_light_map(const NPRMaterialData& data) { return data.npr_params2.w; }
inline Vec3 get_rim_color(const NPRMaterialData& data) { return Vec3(data.rim_color_and_use_ramp.x, data.rim_color_and_use_ramp.y, data.rim_color_and_use_ramp.z); }
inline float get_use_ramp_map(const NPRMaterialData& data) { return data.rim_color_and_use_ramp.w; }

inline void set_npr_params(NPRMaterialData& data, 
    float lambert_clamp, float ramp_tex_offset, float rim_threshold, float rim_strength,
    float rim_width, float use_albedo_map, float use_normal_map, float use_light_map,
    const Vec3& rim_color, float use_ramp_map) {
    data.npr_params1 = Vec4(lambert_clamp, ramp_tex_offset, rim_threshold, rim_strength);
    data.npr_params2 = Vec4(rim_width, use_albedo_map, use_normal_map, use_light_map);
    data.rim_color_and_use_ramp = Vec4(rim_color.x, rim_color.y, rim_color.z, use_ramp_map);
}

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
     * @brief Set the depth texture for screen space rim light calculation
     */
    void set_depth_texture(RHITextureRef depth_texture) { depth_texture_ = depth_texture; }
    
    /**
     * @brief Draw a single batch
     */
    void draw_batch(RHICommandContextRef command, const DrawBatch& batch, const Extent2D& extent);

    /**
     * @brief Execute rendering of batches directly
     */
    void execute_batches(RHICommandListRef command, const std::vector<DrawBatch>& batches, const Extent2D& extent);

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
    RHISamplerRef clamp_sampler_;
    
    // Depth texture for screen space rim light (from depth prepass)
    RHITextureRef depth_texture_;
    
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
    
    // Depth texture bind flag
    bool depth_texture_bound_ = false;
};

} // namespace render
