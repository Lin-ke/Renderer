#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogForwardPass, "ForwardPass");

namespace render {



ForwardPass::ForwardPass() = default;

ForwardPass::~ForwardPass() {
    if (solid_pipeline_) solid_pipeline_->destroy();
    if (wireframe_pipeline_) wireframe_pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
}

void ForwardPass::init() {
    INFO(LogForwardPass, "Initializing ForwardPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogForwardPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_) {
        ERR(LogForwardPass, "Failed to create uniform buffers");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogForwardPass, "Failed to create pipeline");
        return;
    }
    
    initialized_ = true;
    INFO(LogForwardPass, "ForwardPass initialized successfully");
}

void ForwardPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    auto vs_code = ShaderUtils::load_or_compile("forward_pass_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogForwardPass, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogForwardPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    auto fs_code = ShaderUtils::load_or_compile("forward_pass_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (fs_code.empty()) {
        ERR(LogForwardPass, "Failed to load/compile fragment shader");
        return;
    }
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "PSMain";
    fs_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    fs_info.code = fs_code;
    
    auto fs = backend->create_shader(fs_info);
    if (!fs) {
        ERR(LogForwardPass, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = fs;
    
    INFO(LogForwardPass, "Shaders created successfully");
}

void ForwardPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(PerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogForwardPass, "Failed to create per-frame buffer");
        return;
    }
    
    RHIBufferInfo object_info = {};
    object_info.size = sizeof(PerObjectData);
    object_info.stride = 0;
    object_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    object_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    object_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_object_buffer_ = backend->create_buffer(object_info);
    if (!per_object_buffer_) {
        ERR(LogForwardPass, "Failed to create per-object buffer");
        return;
    }
    
    INFO(LogForwardPass, "Uniform buffers created successfully");
}

void ForwardPass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;
    
    RHIRootSignatureInfo root_info = {};
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) return;
    
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    pipe_info.vertex_input_state.vertex_elements.resize(2);
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].semantic_name = "POSITION";
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].semantic_name = "NORMAL";
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = true;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    
    auto render_system = EngineContext::render_system();
    if (render_system) {
        pipe_info.color_attachment_formats[0] = render_system->get_color_format();
        pipe_info.depth_stencil_attachment_format = render_system->get_depth_format();
    } else {
        pipe_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
        pipe_info.depth_stencil_attachment_format = FORMAT_D32_SFLOAT;
    }
    
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    solid_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!solid_pipeline_) {
        ERR(LogForwardPass, "Failed to create solid graphics pipeline");
        return;
    }
    
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_WIREFRAME;
    wireframe_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!wireframe_pipeline_) {
        ERR(LogForwardPass, "Failed to create wireframe graphics pipeline");
        return;
    }
    
    pipeline_ = solid_pipeline_;
    
    INFO(LogForwardPass, "Solid and wireframe pipelines created successfully");
}

void ForwardPass::set_wireframe(bool enable) {
    if (wireframe_mode_ == enable) return;
    
    wireframe_mode_ = enable;
    pipeline_ = enable ? wireframe_pipeline_ : solid_pipeline_;
    
    INFO(LogForwardPass, "Switched to {} mode", enable ? "wireframe" : "solid");
}

void ForwardPass::set_per_frame_data(const Mat4& view, const Mat4& proj, 
                                      const Vec3& camera_pos,
                                      const Vec3& light_dir,
                                      const Vec3& light_color,
                                      float light_intensity) {
    per_frame_data_.view = view;
    per_frame_data_.proj = proj;
    per_frame_data_.camera_pos = camera_pos;
    per_frame_data_.light_dir = light_dir;
    per_frame_data_.light_color = light_color;
    per_frame_data_.light_intensity = light_intensity;
    per_frame_dirty_ = true;
}

void ForwardPass::build(RDGBuilder& builder) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) {
        return;
    }
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) {
        return;
    }
    
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
            
            set_per_frame_data(
                camera->get_view_matrix(),
                camera->get_projection_matrix(),
                camera->get_position(),
                light_dir,
                light_color,
                light_intensity
            );
        }
    }
    
    RDGTextureHandle color_target = builder.create_texture("ForwardPass_Color")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
    
    builder.create_render_pass("ForwardPass_Main")
        .color(0, color_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.1f, 0.2f, 0.4f, 1.0f})
        .execute([this, render_system](RDGPassContext context) {
            auto mesh_manager = render_system->get_mesh_manager();
            if (!mesh_manager) {
                ERR(LogForwardPass, "Mesh manager is null!");
                return;
            }
            
            RHICommandListRef cmd = context.command;
            if (!cmd) {
                ERR(LogForwardPass, "Command list is null!");
                return;
            }
            
            Extent2D extent = render_system->get_swapchain()->get_extent();
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
            
            std::vector<DrawBatch> batches;
            mesh_manager->collect_draw_batches(batches);
            for (const auto& batch : batches) {
                if (per_object_buffer_) {
                    PerObjectData object_data;
                    object_data.model = batch.model_matrix;
                    object_data.inv_model = batch.inv_model_matrix;
                    void* mapped = per_object_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &object_data, sizeof(object_data));
                        per_object_buffer_->unmap();
                    }
                    cmd->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
                }
                
                if (batch.vertex_buffer) {
                    cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
                }
                if (batch.normal_buffer) {
                    cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
                }
                
                if (batch.index_buffer) {
                    cmd->bind_index_buffer(batch.index_buffer, 0);
                    cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
                }
            }
        })
        .finish();
}

void ForwardPass::draw_batch(RHICommandContextRef command, const DrawBatch& batch) {
    if (!command || !pipeline_ || !batch.vertex_buffer || !batch.index_buffer || batch.index_count == 0) {
        ERR(LogForwardPass, "draw_batch: invalid parameters");
        return;
    }
    
    command->set_graphics_pipeline(pipeline_);
    
    if (per_frame_buffer_) {
        if (per_frame_dirty_) {
            void* mapped = per_frame_buffer_->map();
            if (mapped) {
                memcpy(mapped, &per_frame_data_, sizeof(per_frame_data_));
                per_frame_buffer_->unmap();
            }
            per_frame_dirty_ = false;
        }
        command->bind_constant_buffer(per_frame_buffer_, 0, 
            static_cast<ShaderFrequency>(SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
    }
    
    if (per_object_buffer_) {
        PerObjectData object_data;
        object_data.model = batch.model_matrix;
        object_data.inv_model = batch.inv_model_matrix;
        
        void* mapped = per_object_buffer_->map();
        if (mapped) {
            memcpy(mapped, &object_data, sizeof(object_data));
            per_object_buffer_->unmap();
        }
        
        command->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
    }
    
    if (batch.vertex_buffer) {
        command->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
    }
    if (batch.normal_buffer) {
        command->bind_vertex_buffer(batch.normal_buffer, 1, 0);
    }
    
    if (batch.index_buffer) {
        command->bind_index_buffer(batch.index_buffer, 0);
        command->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
    }
}

} // namespace render
