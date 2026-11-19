#include "engine/function/render/render_pass/draw_material.h"
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogGBufferPass, "GBufferPass");

namespace render {

GBufferPass::GBufferPass() = default;

GBufferPass::~GBufferPass() {
    if (pipeline_) pipeline_->destroy();
    if (double_sided_pipeline_) double_sided_pipeline_->destroy();
    if (front_cull_pipeline_) front_cull_pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
    if (material_buffer_) material_buffer_->destroy();
    if (default_sampler_) default_sampler_->destroy();
    if (material_buffer_) material_buffer_->destroy();
    if (default_sampler_) default_sampler_->destroy();
}

void GBufferPass::init() {
    INFO(LogGBufferPass, "Initializing GBufferPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogGBufferPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_ || !material_buffer_) {
        ERR(LogGBufferPass, "Failed to create uniform buffers");
        return;
    }
    
    create_samplers();
    if (!default_sampler_) {
        ERR(LogGBufferPass, "Failed to create samplers");
        return;
    }
    
    create_samplers();
    if (!default_sampler_) {
        ERR(LogGBufferPass, "Failed to create samplers");
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
    
    auto vs_code = ShaderUtils::load_or_compile("g_buffer_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogGBufferPass, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogGBufferPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    auto ps_code = ShaderUtils::load_or_compile("g_buffer_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (ps_code.empty()) {
        ERR(LogGBufferPass, "Failed to load/compile pixel shader");
        return;
    }
    
    RHIShaderInfo ps_info = {};
    ps_info.entry = "PSMain";
    ps_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    ps_info.code = ps_code;
    
    auto ps = backend->create_shader(ps_info);
    if (!ps) {
        ERR(LogGBufferPass, "Failed to create pixel shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = ps;
    
    INFO(LogGBufferPass, "Shaders created successfully");
}

void GBufferPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
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
    
   // Create material buffer (b2)
    RHIBufferInfo material_info = {};
    material_info.size = sizeof(GBufferMaterialData);
    material_info.stride = 0;
    material_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    material_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    material_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    material_buffer_ = backend->create_buffer(material_info);
    if (!material_buffer_) {
        ERR(LogGBufferPass, "Failed to create material buffer");
        return;
    }
    
    INFO(LogGBufferPass, "Uniform buffers created successfully");
}

void GBufferPass::create_samplers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    RHISamplerInfo sampler_info = {};
    sampler_info.min_filter = FILTER_TYPE_LINEAR;
    sampler_info.mag_filter = FILTER_TYPE_LINEAR;
    sampler_info.mipmap_mode = MIPMAP_MODE_LINEAR;
    sampler_info.address_mode_u = ADDRESS_MODE_REPEAT;
    sampler_info.address_mode_v = ADDRESS_MODE_REPEAT;
    sampler_info.address_mode_w = ADDRESS_MODE_REPEAT;
    sampler_info.max_anisotropy = 16.0f;
    
    default_sampler_ = backend->create_sampler(sampler_info);
    if (!default_sampler_) {
        ERR(LogGBufferPass, "Failed to create default sampler");
    }
}


void GBufferPass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;
    
    RHIRootSignatureInfo root_info = {};
    root_info.add_entry_from_reflect(vertex_shader_->shader_);
    root_info.add_entry_from_reflect(fragment_shader_->shader_);
    root_signature_ = backend->create_root_signature(root_info);
    if (root_signature_) descriptor_set_ = root_signature_->create_descriptor_set(0);
    if (!root_signature_) return;
    
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    pipe_info.vertex_input_state.vertex_elements.resize(4);
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].semantic_name = "POSITION";
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].semantic_name = "NORMAL";
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    pipe_info.vertex_input_state.vertex_elements[2].stream_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].semantic_name = "TEXCOORD";
    pipe_info.vertex_input_state.vertex_elements[2].format = FORMAT_R32G32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[2].offset = 0;
    pipe_info.vertex_input_state.vertex_elements[3].stream_index = 3;
    pipe_info.vertex_input_state.vertex_elements[3].semantic_name = "TANGENT";
    pipe_info.vertex_input_state.vertex_elements[3].format = FORMAT_R32G32B32A32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[3].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_BACK;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = true;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    
    pipe_info.color_attachment_formats[0] = get_albedo_ao_format();
    pipe_info.color_attachment_formats[1] = get_normal_roughness_format();
    pipe_info.color_attachment_formats[2] = get_material_emission_format();
    pipe_info.color_attachment_formats[3] = get_position_depth_format();
    pipe_info.depth_stencil_attachment_format = get_depth_format();
    
    pipeline_ = backend->create_graphics_pipeline(pipe_info);
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    double_sided_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_FRONT;
    front_cull_pipeline_ = backend->create_graphics_pipeline(pipe_info);
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
}

void GBufferPass::build(RDGBuilder& builder) {
    // Default build does nothing - use the explicit batches version
}

std::optional<GBufferOutputHandles> GBufferPass::build(RDGBuilder& builder, RDGTextureHandle depth_target, 
                        const std::vector<DrawBatch>& batches) {
    if (!initialized_ || !pipeline_) {
        return std::nullopt;
    }
    
    if (batches.empty()) {
        return std::nullopt;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return std::nullopt;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return std::nullopt;
    
    Extent2D extent = swapchain->get_extent();
    
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
    
    // Use depth from DepthPrePass - LOAD to preserve early-z benefits
    const std::array fallbacks{render_system->get_fallback_white_texture(), render_system->get_fallback_normal_texture(),
        render_system->get_fallback_black_texture(), render_system->get_fallback_black_texture(),
        render_system->get_fallback_black_texture(), render_system->get_fallback_white_texture(),
        render_system->get_fallback_black_texture()};
    auto pass = builder.create_render_pass("GBuffer_Pass")
        .root_signature(root_signature_)
        .descriptor_set(0, descriptor_set_)
        .color(GBufferData::ALBEDO_AO_INDEX, gbuffer_albedo_ao, 
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.0f, 0.0f, 0.0f, 1.0f})
        .color(GBufferData::NORMAL_ROUGHNESS_INDEX, gbuffer_normal_roughness,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.5f, 0.5f, 1.0f, 1.0f})
        .color(GBufferData::MATERIAL_EMISSION_INDEX, gbuffer_material,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.0f, 0.0f, 0.5f, 1.0f})
        .color(GBufferData::POSITION_DEPTH_INDEX, gbuffer_position,
               ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
               Color4{0.0f, 0.0f, 0.0f, 0.0f})
        .depth_stencil(depth_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 
                       1.0f, 0);
    for (const auto& fallback : fallbacks) pass.sample(fallback);
    for (const auto& batch : batches) {
        pass.vertex_read(batch.vertex_buffer).vertex_read(batch.normal_buffer).vertex_read(batch.texcoord_buffer)
            .vertex_read(batch.tangent_buffer).index_read(batch.index_buffer);
        const auto fallback_material = !batch.material_snapshot && batch.material ? snapshot_material(*batch.material) : DrawMaterial{};
        const auto& material = batch.material_snapshot ? *batch.material_snapshot : fallback_material;
        if (material.pbr) for (const auto& texture : material.textures) pass.sample(texture);
    }
    pass.execute([this, batches, extent, frame_data = per_frame_data_, fallbacks](RDGPassContext context) {
            RHICommandListRef cmd = context.command;
            if (!cmd) return;
            
            cmd->set_viewport({0, 0}, {extent.width, extent.height});
            cmd->set_scissor({0, 0}, {extent.width, extent.height});
            cmd->set_graphics_pipeline(pipeline_);
            
            if (per_frame_buffer_) {
                if (void* mapped = per_frame_buffer_->map()) {
                    memcpy(mapped, &frame_data, sizeof(frame_data));
                    per_frame_buffer_->unmap();
                }
                descriptor_set_->write_buffer(0, per_frame_buffer_);
            }
            
            // Bind material buffer and sampler (used by all batches)
            descriptor_set_->write_buffer(2, material_buffer_);
            descriptor_set_->write_sampler(0, default_sampler_);
            
            for (const auto& batch : batches) {
                if (per_object_buffer_) {
                    GBufferPerObjectData object_data;
                    object_data.model = batch.model_matrix;
                    object_data.inv_model = batch.inv_model_matrix;
                    void* mapped = per_object_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &object_data, sizeof(object_data));
                        per_object_buffer_->unmap();
                    }
                    descriptor_set_->write_buffer(1, per_object_buffer_);
                }
                
                const auto fallback_material = !batch.material_snapshot && batch.material
                    ? snapshot_material(*batch.material) : DrawMaterial{};
                const auto& material = batch.material_snapshot ? *batch.material_snapshot : fallback_material;
                cmd->set_graphics_pipeline(material.cull_mode == CULL_MODE_NONE ? double_sided_pipeline_ :
                    material.cull_mode == CULL_MODE_FRONT ? front_cull_pipeline_ : pipeline_);
                if (material_buffer_ && material.pbr) {
                    if (void* mapped = material_buffer_->map()) {
                        memcpy(mapped, &*material.pbr, sizeof(GBufferMaterialData));
                        material_buffer_->unmap();
                    }
                }
                for (uint32_t slot = 0; slot < fallbacks.size(); ++slot) {
                    const auto& texture = material.pbr ? material.textures[slot] : fallbacks[slot];
                    descriptor_set_->write_texture(slot, texture ? texture : fallbacks[slot]);
                }

                if (batch.vertex_buffer) {
                    cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
                }
                if (batch.normal_buffer) {
                    cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
                }
                if (batch.texcoord_buffer) {
                    cmd->bind_vertex_buffer(batch.texcoord_buffer, 2, 0);
                }
                if (batch.tangent_buffer) {
                    cmd->bind_vertex_buffer(batch.tangent_buffer, 3, 0);
                }
                
                if (batch.index_buffer) {
                    cmd->bind_index_buffer(batch.index_buffer, 0);
                    cmd->bind_descriptor_set(descriptor_set_, 0);
                    cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
                }
            }
        })
        .finish();
    
    return GBufferOutputHandles{
        gbuffer_albedo_ao,
        gbuffer_normal_roughness,
        gbuffer_material,
        gbuffer_position
    };
}

} // namespace render
