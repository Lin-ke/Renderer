#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
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

// Blinn-Phong vertex shader source (HLSL)
static const char* s_vertex_shader_src = R"(
cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
    
    float3 light_dir;
    float _padding2;
    float3 light_color;
    float light_intensity;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

struct VSInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float3 view_dir : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    // Transform position to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space (using inverse transpose)
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // View direction for specular
    output.view_dir = normalize(camera_pos - world_pos.xyz);
    
    return output;
}
)";

// Blinn-Phong fragment shader source (HLSL)
static const char* s_fragment_shader_src = R"(
struct PSInput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float3 view_dir : TEXCOORD0;
};

cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
    
    float3 light_dir;
    float _padding2;
    float3 light_color;
    float light_intensity;
};

float4 main(PSInput input) : SV_TARGET {
    // Normalize inputs
    float3 N = normalize(input.world_normal);
    float3 V = normalize(input.view_dir);
    float3 L = normalize(-light_dir); // Light direction points FROM light TO surface
    float3 H = normalize(L + V); // Halfway vector for Blinn-Phong
    
    // Material properties (hardcoded for now)
    float3 diffuse_color = float3(0.8, 0.6, 0.5); // Bunny-like color
    float3 specular_color = float3(0.5, 0.5, 0.5);
    float shininess = 32.0;
    float ambient = 0.1;
    
    // Ambient
    float3 ambient_term = diffuse_color * ambient;
    
    // Diffuse (Lambert)
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse_term = diffuse_color * light_color * NdotL * light_intensity;
    
    // Specular (Blinn-Phong)
    float NdotH = max(dot(N, H), 0.0);
    float3 specular_term = specular_color * light_color * pow(NdotH, shininess) * light_intensity;
    
    // Final color
    float3 final_color = ambient_term + diffuse_term + specular_term;
    
    // Simple gamma correction
    final_color = pow(final_color, 1.0 / 2.2);
    
    return float4(final_color, 1.0);
}
)";

static std::vector<uint8_t> compile_shader_rhi(const char* source, const char* entry, 
                                                  const char* profile) {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogForwardPass, "RHI backend not available for shader compilation");
        return {};
    }
    return backend->compile_shader(source, entry, profile);
}

ForwardPass::ForwardPass() = default;

ForwardPass::~ForwardPass() {
    if (solid_pipeline_) solid_pipeline_->destroy();
    if (wireframe_pipeline_) wireframe_pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    // Shaders managed by shared_ptr
    // Shaders managed by shared_ptr
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
    
    // Compile vertex shader using RHI backend
    auto vs_code = compile_shader_rhi(s_vertex_shader_src, "main", "vs_5_0");
    if (vs_code.empty()) return;
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "main";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogForwardPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Compile fragment shader using RHI backend
    auto fs_code = compile_shader_rhi(s_fragment_shader_src, "main", "ps_5_0");
    if (fs_code.empty()) return;
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "main";
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
    
    // Create per-frame buffer (b0)
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
    
    // Create per-object buffer (b1) - dynamic, updated per draw
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
    
    // Create root signature with two uniform buffer slots
    RHIRootSignatureInfo root_info = {};
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) return;
    
    // Common pipeline configuration
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    // Vertex input layout: position (stream 0) + normal (stream 1)
    // Meshes store position and normal in separate buffers
    pipe_info.vertex_input_state.vertex_elements.resize(2);
    // Position - stream 0
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].attribute_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    // Normal - stream 1
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].attribute_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;  // Disable culling for testing
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    pipe_info.depth_stencil_state.enable_depth_test = false;
    pipe_info.depth_stencil_state.enable_depth_write = false;
    
    // Render targets
    auto render_system = EngineContext::render_system();
    if (render_system) {
        pipe_info.color_attachment_formats[0] = render_system->get_color_format();
        pipe_info.depth_stencil_attachment_format = render_system->get_depth_format();
    } else {
        pipe_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
        pipe_info.depth_stencil_attachment_format = FORMAT_D32_SFLOAT;
    }
    
    // Create solid pipeline
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    solid_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!solid_pipeline_) {
        ERR(LogForwardPass, "Failed to create solid graphics pipeline");
        return;
    }
    
    // Create wireframe pipeline
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_WIREFRAME;
    wireframe_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!wireframe_pipeline_) {
        ERR(LogForwardPass, "Failed to create wireframe graphics pipeline");
        return;
    }
    
    // Start with solid pipeline
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
    // HLSL uses column-major matrices with mul(matrix, vector), matching Eigen's storage
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
    
    // Import swapchain texture (external resource)
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) {
        return;
    }
    
    // Get current back buffer texture
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) {
        return;
    }
    
    // Update per-frame data from camera and lights BEFORE creating the pass
    auto mesh_manager = render_system->get_mesh_manager();
    if (mesh_manager) {
        auto* camera = mesh_manager->get_active_camera();
        if (camera) {
            // Get light data from world
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
    
    // Create or import color target
    RDGTextureHandle color_target = builder.create_texture("ForwardPass_Color")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
    
    // Create render pass using RDG
    INFO(LogForwardPass, "Building ForwardPass RDG node...");
    builder.create_render_pass("ForwardPass_Main")
        .color(0, color_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.1f, 0.2f, 0.4f, 1.0f})
        .execute([this, render_system](RDGPassContext context) {
            ERR(LogForwardPass, "Executing ForwardPass RDG lambda!");
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
            
            // Set viewport and scissor
            Extent2D extent = render_system->get_swapchain()->get_extent();
            cmd->set_viewport({0, 0}, {extent.width, extent.height});
            cmd->set_scissor({0, 0}, {extent.width, extent.height});
            cmd->set_graphics_pipeline(pipeline_);
            
            // Update and bind per-frame buffer
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
            
            // Get batches from mesh manager and draw them
            std::vector<DrawBatch> batches;
            mesh_manager->collect_draw_batches(batches);
            ERR(LogForwardPass, "Collected {} draw batches in RDG lambda", batches.size());
            for (const auto& batch : batches) {
                // Update and bind per-object buffer
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
                
                // Bind vertex buffers
                if (batch.vertex_buffer) {
                    cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
                }
                if (batch.normal_buffer) {
                    cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
                }
                
                // Draw
                if (batch.index_buffer) {
                    cmd->bind_index_buffer(batch.index_buffer, 0);
                    cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
                }
            }
        })
        .finish();
}

void ForwardPass::draw_batch(RHICommandContextRef command, const DrawBatch& batch) {
    if (!command) {
        ERR(LogForwardPass, "draw_batch: command is null");
        return;
    }
    if (!pipeline_) {
        ERR(LogForwardPass, "draw_batch: pipeline is null");
        return;
    }
    if (!batch.vertex_buffer) {
        ERR(LogForwardPass, "draw_batch: vertex_buffer is null");
        return;
    }
    if (!batch.index_buffer) {
        ERR(LogForwardPass, "draw_batch: index_buffer is null");
        return;
    }
    if (batch.index_count == 0) {
        ERR(LogForwardPass, "draw_batch: index_count is 0");
        return;
    }
    
    // Log draw call info once
    static bool logged_once = false;
    if (!logged_once) {
        INFO(LogForwardPass, "Draw call: index_count={}, model_matrix[0]={},{},{},{}", 
             batch.index_count,
             batch.model_matrix(0,0), batch.model_matrix(0,1), 
             batch.model_matrix(0,2), batch.model_matrix(0,3));
        
        // Log view matrix
        INFO(LogForwardPass, "View matrix row 0: {}, {}, {}, {}",
             per_frame_data_.view(0,0), per_frame_data_.view(0,1),
             per_frame_data_.view(0,2), per_frame_data_.view(0,3));
        INFO(LogForwardPass, "View matrix row 1: {}, {}, {}, {}",
             per_frame_data_.view(1,0), per_frame_data_.view(1,1),
             per_frame_data_.view(1,2), per_frame_data_.view(1,3));
        INFO(LogForwardPass, "View matrix row 2: {}, {}, {}, {}",
             per_frame_data_.view(2,0), per_frame_data_.view(2,1),
             per_frame_data_.view(2,2), per_frame_data_.view(2,3));
        INFO(LogForwardPass, "View matrix row 3: {}, {}, {}, {}",
             per_frame_data_.view(3,0), per_frame_data_.view(3,1),
             per_frame_data_.view(3,2), per_frame_data_.view(3,3));
        
        // Log proj matrix
        INFO(LogForwardPass, "Proj matrix row 0: {}, {}, {}, {}",
             per_frame_data_.proj(0,0), per_frame_data_.proj(0,1),
             per_frame_data_.proj(0,2), per_frame_data_.proj(0,3));
        INFO(LogForwardPass, "Proj matrix row 1: {}, {}, {}, {}",
             per_frame_data_.proj(1,0), per_frame_data_.proj(1,1),
             per_frame_data_.proj(1,2), per_frame_data_.proj(1,3));
        INFO(LogForwardPass, "Proj matrix row 2: {}, {}, {}, {}",
             per_frame_data_.proj(2,0), per_frame_data_.proj(2,1),
             per_frame_data_.proj(2,2), per_frame_data_.proj(2,3));
        INFO(LogForwardPass, "Proj matrix row 3: {}, {}, {}, {}",
             per_frame_data_.proj(3,0), per_frame_data_.proj(3,1),
             per_frame_data_.proj(3,2), per_frame_data_.proj(3,3));
        
        logged_once = true;
    }
    
    // Set pipeline
    command->set_graphics_pipeline(pipeline_);
    
    // Bind per-frame uniform buffer (slot b0, vertex and fragment shader)
    if (per_frame_buffer_) {
        // Update per-frame buffer if dirty
        if (per_frame_dirty_) {
            void* mapped = per_frame_buffer_->map();
            if (mapped) {
                memcpy(mapped, &per_frame_data_, sizeof(per_frame_data_));
                per_frame_buffer_->unmap();
            } else {
                ERR(LogForwardPass, "Failed to map per_frame_buffer");
            }
            per_frame_dirty_ = false;
        }
        
        // Bind to both vertex and fragment shader
        command->bind_constant_buffer(per_frame_buffer_, 0, 
            static_cast<ShaderFrequency>(SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
    } else {
        ERR(LogForwardPass, "per_frame_buffer is null");
    }
    
    // Update and bind per-object buffer (slot b1)
    if (per_object_buffer_) {
        PerObjectData object_data;
        // HLSL uses column-major matrices with mul(matrix, vector), matching Eigen's storage
        object_data.model = batch.model_matrix;
        object_data.inv_model = batch.inv_model_matrix;
        
        void* mapped = per_object_buffer_->map();
        if (mapped) {
            memcpy(mapped, &object_data, sizeof(object_data));
            per_object_buffer_->unmap();
        } else {
            ERR(LogForwardPass, "Failed to map per_object_buffer");
        }
        
        // Bind to vertex shader only (model matrix usually not needed in fragment)
        command->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
    } else {
        ERR(LogForwardPass, "per_object_buffer is null");
    }
    
    // Bind vertex buffers (position at stream 0, normal at stream 1)
    if (batch.vertex_buffer) {
        command->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
    }
    if (batch.normal_buffer) {
        command->bind_vertex_buffer(batch.normal_buffer, 1, 0);
    }
    
    if (batch.index_buffer) {
        command->bind_index_buffer(batch.index_buffer, 0);
        command->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
    } else {
        // Non-indexed draw not supported for now
        WARN(LogForwardPass, "Non-indexed draw not supported");
    }
}

} // namespace render
