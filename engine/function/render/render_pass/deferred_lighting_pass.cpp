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
    per_frame_data_.light_count = 1;
    per_frame_dirty_ = true;
}

void DeferredLightingPass::build(RDGBuilder& builder) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    Extent2D extent = swapchain->get_extent();
    
    auto mesh_manager = render_system->get_mesh_manager();
    if (mesh_manager) {
        auto* camera = mesh_manager->get_active_camera();
        if (camera) {
            Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);
            Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
            float light_intensity = 1.0f;
            
            auto* world = EngineContext::world();
            if (world && world->get_active_scene()) {
                auto* light = world->get_active_scene()->get_directional_light();
                if (light && light->enable()) {
                    auto* entity = light->get_owner();
                    if (entity) {
                        auto* transform = entity->get_component<TransformComponent>();
                        if (transform) {
                            light_dir = -transform->transform.front();
                        }
                    }
                    light_color = light->get_color();
                    light_intensity = light->get_intensity();
                }
            }
            
            set_main_light(light_dir, light_color, light_intensity);
            
            Mat4 inv_view_proj = (camera->get_projection_matrix() * camera->get_view_matrix()).inverse();
            set_per_frame_data(camera->get_position(), inv_view_proj);
        }
    }
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return;
    
    RDGTextureHandle color_target = builder.create_texture("DeferredLighting_Output")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
    
    builder.create_render_pass("DeferredLighting_Pass")
        .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
        .execute([this, extent](RDGPassContext context) {
            RHICommandListRef cmd = context.command;
            if (!cmd) return;
            
            cmd->set_viewport({0, 0}, {extent.width, extent.height});
            cmd->set_scissor({0, 0}, {extent.width, extent.height});
            cmd->set_graphics_pipeline(pipeline_);
            
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
            
            cmd->draw(3, 1, 0, 0);
        })
        .finish();
}

} // namespace render
