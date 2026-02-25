#pragma once

#include "engine/function/render/render_pass/render_pass.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/core/math/math.h"
#include <memory>
#include <vector>

// Forward declarations
class SkyboxComponent;
class Mesh;
using MeshRef = std::shared_ptr<Mesh>;

namespace render {

/**
 * @brief Skybox rendering pass
 * 
 * Renders skybox environment as a cube map at infinity.
 * Uses special pipeline states:
 * - Depth test: LESS_EQUAL (so it appears behind everything)
 * - Depth write: OFF
 * - Cull mode: NONE (we see inside the cube)
 */
class SkyboxPass : public RenderPass {
public:
    SkyboxPass();
    ~SkyboxPass() override;

    void init() override;
    void build(RDGBuilder& builder) override;
    
    /**
     * @brief Build the skybox pass into the RDG
     * @param builder RDG builder
     * @param color_target Color attachment target
     * @param depth_target Depth attachment target (for depth test)
     * @param view View matrix (will have translation removed)
     * @param proj Projection matrix
     * @param skyboxes List of skybox components to render
     */
    void build(RDGBuilder& builder, RDGTextureHandle color_target,
               RDGTextureHandle depth_target, const Mat4& view, const Mat4& proj,
               const std::vector<SkyboxComponent*>& skyboxes);

    PassType get_type() const override { return PassType::Forward; }
    std::string_view get_name() const override { return "SkyboxPass"; }
    
    bool is_ready() const { return initialized_ && pipeline_ != nullptr; }

private:
    void create_shaders();
    void create_pipeline();
    void create_uniform_buffers();
    void create_samplers();
    
    // Load default cube mesh for skybox rendering
    void ensure_cube_mesh();

    // Shaders
    ShaderRef vertex_shader_;
    ShaderRef fragment_shader_;

    // Pipeline
    RHIGraphicsPipelineRef pipeline_;
    RHIRootSignatureRef root_signature_;

    // Uniform buffers
    RHIBufferRef per_frame_buffer_;   // b0: view, proj, camera_pos
    RHIBufferRef per_object_buffer_;  // b1: model matrix
    RHIBufferRef params_buffer_;      // b2: intensity

    // Sampler for cube texture
    RHISamplerRef cube_sampler_;
    
    // Default cube mesh
    MeshRef cube_mesh_;

    // Per-frame data structure
    struct PerFrameData {
        Mat4 view;
        Mat4 proj;
        Vec3 camera_pos;
        float _padding;
    };

    // Per-object data structure
    struct PerObjectData {
        Mat4 model;
        Mat4 inv_model;
    };

    // Skybox parameters
    struct SkyboxParams {
        float intensity;
        float _padding[3]; // 16 bytes alignment
    };

    PerFrameData per_frame_data_;
    bool per_frame_dirty_ = true;
    bool initialized_ = false;
};

} // namespace render
