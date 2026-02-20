#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogGBufferPass, "GBufferPass");

namespace render {

// G-Buffer vertex shader (HLSL)
static const char* s_gbuffer_vs_src = R"(
cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

struct VSInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    
    // Transform to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // Pass through texcoord
    output.texcoord = input.texcoord;
    
    return output;
}
)";

// G-Buffer fragment shader (HLSL) - outputs to MRT
static const char* s_gbuffer_fs_src = R"(
struct PSInput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput {
    float4 albedo_ao : SV_TARGET0;           // RGB: Albedo, A: AO
    float4 normal_roughness : SV_TARGET1;    // RGB: Normal (packed), A: Roughness
    float4 material_emission : SV_TARGET2;   // R: Metallic, G: Emission intensity, B: Specular, A: unused
    float4 position_depth : SV_TARGET3;      // RGB: World position, A: Linear depth
};

PSOutput main(PSInput input) {
    PSOutput output;
    
    // Normalize normal
    float3 N = normalize(input.world_normal);
    
    // Pack normal to [0,1] range for storage
    float3 packed_normal = N * 0.5 + 0.5;
    
    // Material parameters (hardcoded for now, would come from material system)
    float3 albedo = float3(0.8, 0.6, 0.5);  // Default albedo
    float roughness = 0.5;                   // Default roughness
    float metallic = 0.0;                    // Default metallic
    float ao = 1.0;                          // Default AO
    float emission = 0.0;                    // Default emission
    float specular = 0.5;                    // Default specular
    
    // Output to G-Buffer targets
    output.albedo_ao = float4(albedo, ao);
    output.normal_roughness = float4(packed_normal, roughness);
    output.material_emission = float4(metallic, emission, specular, 1.0);
    output.position_depth = float4(input.world_pos, input.position.w);
    
    return output;
}
)";

static std::vector<uint8_t> compile_shader_rhi(const char* source, const char* entry, 
                                                const char* profile) {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogGBufferPass, "RHI backend not available for shader compilation");
        return {};
    }
    return backend->compile_shader(source, entry, profile);
}

GBufferPass::GBufferPass() = default;

GBufferPass::~GBufferPass() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
}

void GBufferPass::init() {
    INFO(LogGBufferPass, "Initializing GBufferPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogGBufferPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_) {
        ERR(LogGBufferPass, "Failed to create uniform buffers");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogGBufferPass, "Failed to create pipeline");
        return;
    }
    
    initialized_ = true;
    INFO(LogGBufferPass, "GBufferPass initialized successfully");
}

void GBufferPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Compile vertex shader
    auto vs_code = compile_shader_rhi(s_gbuffer_vs_src, "main", "vs_5_0");
    if (vs_code.empty()) return;
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "main";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogGBufferPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Compile fragment shader
    auto fs_code = compile_shader_rhi(s_gbuffer_fs_src, "main", "ps_5_0");
    if (fs_code.empty()) return;
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "main";
    fs_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    fs_info.code = fs_code;
    
    auto fs = backend->create_shader(fs_info);
    if (!fs) {
        ERR(LogGBufferPass, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = fs;
    
    INFO(LogGBufferPass, "Shaders created successfully");
}

void GBufferPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Per-frame buffer
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(GBufferPerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogGBufferPass, "Failed to create per-frame buffer");
        return;
    }
    
    // Per-object buffer
    RHIBufferInfo object_info = {};
    object_info.size = sizeof(GBufferPerObjectData);
    object_info.stride = 0;
    object_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    object_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    object_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_object_buffer_ = backend->create_buffer(object_info);
    if (!per_object_buffer_) {
        ERR(LogGBufferPass, "Failed to create per-object buffer");
        return;
    }
    
    INFO(LogGBufferPass, "Uniform buffers created successfully");
}

void GBufferPass::create_pipeline() {
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
    
    // Vertex input: position + normal + texcoord
    pipe_info.vertex_input_state.vertex_elements.resize(3);
    // Position
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].attribute_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    // Normal
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].attribute_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    // Texcoord
    pipe_info.vertex_input_state.vertex_elements[2].stream_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].attribute_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].format = FORMAT_R32G32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[2].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_BACK;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = true;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    
    // MRT render targets
    pipe_info.color_attachment_formats[0] = get_albedo_ao_format();
    pipe_info.color_attachment_formats[1] = get_normal_roughness_format();
    pipe_info.color_attachment_formats[2] = get_material_emission_format();
    pipe_info.color_attachment_formats[3] = get_position_depth_format();
    pipe_info.depth_stencil_attachment_format = get_depth_format();
    
    pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!pipeline_) {
        ERR(LogGBufferPass, "Failed to create graphics pipeline");
        return;
    }
    
    INFO(LogGBufferPass, "Pipeline created successfully");
}

void GBufferPass::set_per_frame_data(const Mat4& view, const Mat4& proj, const Vec3& camera_pos) {
    per_frame_data_.view = view;
    per_frame_data_.proj = proj;
    per_frame_data_.camera_pos = camera_pos;
    per_frame_dirty_ = true;
}

void GBufferPass::build(RDGBuilder& builder) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    // Get extent
    Extent2D extent = swapchain->get_extent();
    
    // Update per-frame data from camera
    auto mesh_manager = render_system->get_mesh_manager();
    if (mesh_manager) {
        auto* camera = mesh_manager->get_active_camera();
        if (camera) {
            set_per_frame_data(
                camera->get_view_matrix(),
                camera->get_projection_matrix(),
                camera->get_position()
            );
        }
    }
    
    // Create G-Buffer textures
    Extent3D tex_extent = {extent.width, extent.height, 1};
    
    RDGTextureHandle gbuffer_albedo_ao = builder.create_texture("GBuffer_AlbedoAO")
        .extent(tex_extent)
        .format(get_albedo_ao_format())
        .allow_render_target()
        .finish();
    
    RDGTextureHandle gbuffer_normal_roughness = builder.create_texture("GBuffer_NormalRoughness")
        .extent(tex_extent)
        .format(get_normal_roughness_format())
        .allow_render_target()
        .finish();
    
    RDGTextureHandle gbuffer_material = builder.create_texture("GBuffer_Material")
        .extent(tex_extent)
        .format(get_material_emission_format())
        .allow_render_target()
        .finish();
    
    RDGTextureHandle gbuffer_position = builder.create_texture("GBuffer_Position")
        .extent(tex_extent)
        .format(get_position_depth_format())
        .allow_render_target()
        .finish();
    
    RDGTextureHandle depth_target = builder.create_texture("GBuffer_Depth")
        .extent(tex_extent)
        .format(get_depth_format())
        .allow_depth_stencil()
        .finish();
    
    // Create render pass
    builder.create_render_pass("GBuffer_Pass")
        .color(GBufferData::ALBEDO_AO_INDEX, gbuffer_albedo_ao, 
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.0f, 0.0f, 0.0f, 1.0f})
        .color(GBufferData::NORMAL_ROUGHNESS_INDEX, gbuffer_normal_roughness,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.5f, 0.5f, 1.0f, 1.0f})  // Default normal pointing up
        .color(GBufferData::MATERIAL_EMISSION_INDEX, gbuffer_material,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.0f, 0.0f, 0.5f, 1.0f})
        .color(GBufferData::POSITION_DEPTH_INDEX, gbuffer_position,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.0f, 0.0f, 0.0f, 0.0f})
        .depth_stencil(depth_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
                       1.0f, 0)
        .execute([this, render_system, extent](RDGPassContext context) {
            RHICommandListRef cmd = context.command;
            if (!cmd) return;
            
            // Set viewport and scissor
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
            
            // Get batches and draw
            std::vector<DrawBatch> batches;
            auto mesh_manager = render_system->get_mesh_manager();
            if (mesh_manager) {
                mesh_manager->collect_draw_batches(batches);
            }
            
            for (const auto& batch : batches) {
                // Update per-object buffer
                if (per_object_buffer_) {
                    GBufferPerObjectData object_data;
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
                //####TODO####: Bind texcoord buffer when available
                
                // Draw
                if (batch.index_buffer) {
                    cmd->bind_index_buffer(batch.index_buffer, 0);
                    cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
                }
            }
        })
        .finish();
}

} // namespace render
