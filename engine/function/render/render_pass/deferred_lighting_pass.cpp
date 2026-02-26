#include "engine/function/render/render_pass/deferred_lighting_pass.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_resource/shader_utils.h"

#include <cstring>

DEFINE_LOG_TAG(LogDeferredLighting, "DeferredLighting");

namespace render {

DeferredLightingPass::DeferredLightingPass() = default;

DeferredLightingPass::~DeferredLightingPass() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (quad_vertex_buffer_) quad_vertex_buffer_->destroy();
    if (quad_index_buffer_) quad_index_buffer_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (light_buffer_) light_buffer_->destroy();
    if (gbuffer_sampler_) gbuffer_sampler_->destroy();
}

void DeferredLightingPass::init() {
    INFO(LogDeferredLighting, "Initializing DeferredLightingPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogDeferredLighting, "Failed to create shaders");
        return;
    }
    
    create_quad_geometry();
    if (!quad_vertex_buffer_ || !quad_index_buffer_) {
        ERR(LogDeferredLighting, "Failed to create quad geometry");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_) {
        ERR(LogDeferredLighting, "Failed to create uniform buffers");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogDeferredLighting, "Failed to create pipeline");
        return;
    }
    
    initialized_ = true;
    INFO(LogDeferredLighting, "DeferredLightingPass initialized successfully");
}

void DeferredLightingPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load pre-compiled vertex shader
    auto vs_code = ShaderUtils::load_or_compile("deferred_lighting_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogDeferredLighting, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogDeferredLighting, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Load pre-compiled pixel shader
    auto ps_code = ShaderUtils::load_or_compile("deferred_lighting_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (ps_code.empty()) {
        ERR(LogDeferredLighting, "Failed to load/compile pixel shader");
        return;
    }
    
    RHIShaderInfo ps_info = {};
    ps_info.entry = "PSMain";
    ps_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    ps_info.code = ps_code;
    
    auto ps = backend->create_shader(ps_info);
    if (!ps) {
        ERR(LogDeferredLighting, "Failed to create pixel shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = ps;
    
    INFO(LogDeferredLighting, "Shaders created successfully");
}

void DeferredLightingPass::create_quad_geometry() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f
    };
    
    uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
    
    RHIBufferInfo vb_info = {};
    vb_info.size = sizeof(vertices);
    vb_info.stride = sizeof(float) * 4;
    vb_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    vb_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    
    quad_vertex_buffer_ = backend->create_buffer(vb_info);
    if (!quad_vertex_buffer_) {
        ERR(LogDeferredLighting, "Failed to create quad vertex buffer");
        return;
    }
    
    void* mapped = quad_vertex_buffer_->map();
    if (mapped) {
        memcpy(mapped, vertices, sizeof(vertices));
        quad_vertex_buffer_->unmap();
    }
    
    RHIBufferInfo ib_info = {};
    ib_info.size = sizeof(indices);
    ib_info.stride = sizeof(uint32_t);
    ib_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    ib_info.type = RESOURCE_TYPE_INDEX_BUFFER;
    
    quad_index_buffer_ = backend->create_buffer(ib_info);
    if (!quad_index_buffer_) {
        ERR(LogDeferredLighting, "Failed to create quad index buffer");
        return;
    }
    
    mapped = quad_index_buffer_->map();
    if (mapped) {
        memcpy(mapped, indices, sizeof(indices));
        quad_index_buffer_->unmap();
    }
    
    INFO(LogDeferredLighting, "Quad geometry created successfully");
}

void DeferredLightingPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Per-frame buffer
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(DeferredLightingPerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogDeferredLighting, "Failed to create per-frame buffer");
        return;
    }
    
    // Light array buffer (cbuffer b1)
    RHIBufferInfo light_info = {};
    light_info.size = sizeof(ShaderLightData) * MAX_LIGHTS;
    light_info.stride = 0;
    light_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    light_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    light_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    light_buffer_ = backend->create_buffer(light_info);
    if (!light_buffer_) {
        ERR(LogDeferredLighting, "Failed to create light buffer");
        return;
    }
    
    // Create sampler for GBuffer textures
    RHISamplerInfo sampler_info = {};
    sampler_info.min_filter = FILTER_TYPE_LINEAR;
    sampler_info.mag_filter = FILTER_TYPE_LINEAR;
    sampler_info.mipmap_mode = MIPMAP_MODE_LINEAR;
    sampler_info.address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = ADDRESS_MODE_CLAMP_TO_EDGE;
    
    gbuffer_sampler_ = backend->create_sampler(sampler_info);
    if (!gbuffer_sampler_) {
        ERR(LogDeferredLighting, "Failed to create GBuffer sampler");
        return;
    }
    
    INFO(LogDeferredLighting, "Uniform buffers created successfully");
}

void DeferredLightingPass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;
    
    // Create root signature
    RHIRootSignatureInfo root_info = {};
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) return;
    
    // Pipeline configuration
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    // No vertex input - using SV_VertexID
    pipe_info.vertex_input_state.vertex_elements.clear();
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    pipe_info.depth_stencil_state.enable_depth_test = false;
    pipe_info.depth_stencil_state.enable_depth_write = false;
    
    // Output to HDR format
    auto render_system = EngineContext::render_system();
    if (render_system) {
        pipe_info.color_attachment_formats[0] = render_system->get_color_format();
    } else {
        pipe_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
    }
    
    pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!pipeline_) {
        ERR(LogDeferredLighting, "Failed to create graphics pipeline");
        return;
    }
    
    INFO(LogDeferredLighting, "Pipeline created successfully");
}

void DeferredLightingPass::set_per_frame_data(const Vec3& camera_pos, const Mat4& inv_view_proj) {
    per_frame_data_.camera_pos = camera_pos;
    per_frame_data_.inv_view_proj = inv_view_proj;
    per_frame_dirty_ = true;
}

void DeferredLightingPass::set_main_light(const Vec3& dir, const Vec3& color, float intensity) {
    per_frame_data_.main_light_dir = dir;
    per_frame_data_.main_light_color = color;
    per_frame_data_.main_light_intensity = intensity;
    per_frame_dirty_ = true;
}

void DeferredLightingPass::set_lights(const std::vector<ShaderLightData>& lights) {
    lights_data_ = lights;
    per_frame_data_.light_count = static_cast<uint32_t>(std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS)));
    per_frame_dirty_ = true;
    lights_dirty_ = true;
}


void DeferredLightingPass::build(RDGBuilder& builder) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return;
    
    RDGTextureHandle color_target = builder.create_texture("DeferredLighting_Output")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
        
    build(builder, color_target);
}

void DeferredLightingPass::build(RDGBuilder& builder, const GBufferOutputHandles& gbuffer) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return;
    
    RDGTextureHandle color_target = builder.create_texture("DeferredLighting_Output")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
        
    build(builder, color_target, gbuffer);
}

void DeferredLightingPass::build(RDGBuilder& builder, RDGTextureHandle color_target) {
    // Get GBuffer texture nodes from blackboard (created by GBufferPass)
    auto albedo_node = builder.get_blackboard().texture("GBuffer_AlbedoAO");
    auto normal_node = builder.get_blackboard().texture("GBuffer_NormalRoughness");
    auto material_node = builder.get_blackboard().texture("GBuffer_Material");
    auto position_node = builder.get_blackboard().texture("GBuffer_Position");
    
    if (!albedo_node || !normal_node || !material_node || !position_node) {
        ERR(LogDeferredLighting, "Failed to get GBuffer textures from blackboard");
        return;
    }
    
    // Use the handle-based overload with proper RDG dependencies
    GBufferOutputHandles gbuffer{
        albedo_node->get_handle(),
        normal_node->get_handle(),
        material_node->get_handle(),
        position_node->get_handle()
    };
    build(builder, color_target, gbuffer);
}

void DeferredLightingPass::build(RDGBuilder& builder, RDGTextureHandle color_target, const GBufferOutputHandles& gbuffer) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    Extent2D extent = swapchain->get_extent();
    
    // Declare read dependencies on GBuffer textures via RDG
    auto rp_builder = builder.create_render_pass("DeferredLighting_Pass")
        .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
        .read(0, 0, 0, gbuffer.albedo_ao)
        .read(0, 1, 0, gbuffer.normal_roughness)
        .read(0, 2, 0, gbuffer.material_emission)
        .read(0, 3, 0, gbuffer.position_depth);
    
    // Capture handles for resolving in execute lambda
    auto gb = gbuffer;
    
    rp_builder.execute([this, extent, gb](RDGPassContext context) {
        RHICommandListRef cmd = context.command;
        if (!cmd) return;
        
        cmd->set_viewport({0, 0}, {extent.width, extent.height});
        cmd->set_scissor({0, 0}, {extent.width, extent.height});
        cmd->set_graphics_pipeline(pipeline_);
        
        // Resolve GBuffer textures from RDG handles (already allocated by prepare_descriptor_set)
        RHITextureRef albedo_tex = context.builder->resolve(gb.albedo_ao);
        RHITextureRef normal_tex = context.builder->resolve(gb.normal_roughness);
        RHITextureRef material_tex = context.builder->resolve(gb.material_emission);
        RHITextureRef position_tex = context.builder->resolve(gb.position_depth);
        
        // Bind GBuffer textures (manual binding for DX11 backend)
        if (albedo_tex)  cmd->bind_texture(albedo_tex, 0, SHADER_FREQUENCY_FRAGMENT);
        if (normal_tex)  cmd->bind_texture(normal_tex, 1, SHADER_FREQUENCY_FRAGMENT);
        if (material_tex) cmd->bind_texture(material_tex, 2, SHADER_FREQUENCY_FRAGMENT);
        if (position_tex) cmd->bind_texture(position_tex, 3, SHADER_FREQUENCY_FRAGMENT);
        
        // Bind sampler for GBuffer textures
        if (gbuffer_sampler_) {
            cmd->bind_sampler(gbuffer_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);
        }
        
        if (per_frame_buffer_) {
            if (per_frame_dirty_) {
                void* mapped = per_frame_buffer_->map();
                if (mapped) {
                    memcpy(mapped, &per_frame_data_, sizeof(per_frame_data_));
                    per_frame_buffer_->unmap();
                }
                per_frame_dirty_ = false;
            }
            cmd->bind_constant_buffer(per_frame_buffer_, 0, 
                static_cast<ShaderFrequency>(SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
        }
        
        // Upload and bind light buffer (cbuffer b1)
        if (light_buffer_ && lights_dirty_) {
            void* mapped = light_buffer_->map();
            if (mapped) {
                size_t copy_count = std::min(lights_data_.size(), static_cast<size_t>(MAX_LIGHTS));
                memset(mapped, 0, sizeof(ShaderLightData) * MAX_LIGHTS);
                if (copy_count > 0) {
                    memcpy(mapped, lights_data_.data(), sizeof(ShaderLightData) * copy_count);
                }
                light_buffer_->unmap();
            }
            lights_dirty_ = false;
        }
        if (light_buffer_) {
            cmd->bind_constant_buffer(light_buffer_, 1, SHADER_FREQUENCY_FRAGMENT);
        }
        
        cmd->draw(3, 1, 0, 0);  // 3 vertices for full-screen triangle using SV_VertexID
    })
    .finish();
}

} // namespace render
