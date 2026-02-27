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
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) return;
    
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    pipe_info.vertex_input_state.vertex_elements.resize(3);
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
    
    // Store batches for lambda access (avoids copy in capture)
    current_batches_ = batches;
    
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
    builder.create_render_pass("GBuffer_Pass")
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
                       1.0f, 0)
        .execute([this, render_system, extent](RDGPassContext context) {
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
            
            // Bind material buffer and sampler (used by all batches)
            cmd->bind_constant_buffer(material_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
            cmd->bind_sampler(default_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);
            
            // Get fallback textures from render system
            auto* rsys = EngineContext::render_system();
            RHITextureRef fallback_white = rsys ? rsys->get_fallback_white_texture() : nullptr;
            RHITextureRef fallback_black = rsys ? rsys->get_fallback_black_texture() : nullptr;
            RHITextureRef fallback_normal = rsys ? rsys->get_fallback_normal_texture() : nullptr;
            
            // Use stored batches
            for (const auto& batch : current_batches_) {
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
                
                // Update material data and bind textures
                auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(batch.material);
                if (material_buffer_ && pbr_mat) {
                    GBufferMaterialData mat_data;
                    mat_data.albedo = pbr_mat->get_diffuse();
                    mat_data.roughness = pbr_mat->get_roughness();
                    mat_data.metallic = pbr_mat->get_metallic();
                    mat_data.emission = pbr_mat->get_emission().x; // Use emission x channel as scalar
                    mat_data.alpha_clip = pbr_mat->get_alpha_clip();
                    mat_data.specular = pbr_mat->get_specular();
                    mat_data.use_albedo_map = pbr_mat->get_diffuse_texture() ? 1.0f : 0.0f;
                    mat_data.use_normal_map = pbr_mat->get_normal_texture() ? 1.0f : 0.0f;
                    mat_data.use_arm_map = pbr_mat->get_arm_texture() ? 1.0f : 0.0f;
                    mat_data.use_roughness_map = pbr_mat->get_roughness_texture() ? 1.0f : 0.0f;
                    mat_data.use_metallic_map = pbr_mat->get_metallic_texture() ? 1.0f : 0.0f;
                    mat_data.use_ao_map = pbr_mat->get_ao_texture() ? 1.0f : 0.0f;
                    mat_data.use_emission_map = pbr_mat->get_emission_texture() ? 1.0f : 0.0f;
                    
                    void* mapped = material_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &mat_data, sizeof(mat_data));
                        material_buffer_->unmap();
                    }
                    
                    // Bind albedo texture (t0)
                    auto albedo_tex = pbr_mat->get_diffuse_texture();
                    if (albedo_tex && albedo_tex->texture_) {
                        cmd->bind_texture(albedo_tex->texture_, 0, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_white) {
                        cmd->bind_texture(fallback_white, 0, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    // Bind normal texture (t1)
                    auto normal_tex = pbr_mat->get_normal_texture();
                    if (normal_tex && normal_tex->texture_) {
                        cmd->bind_texture(normal_tex->texture_, 1, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_normal) {
                        cmd->bind_texture(fallback_normal, 1, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    // Bind ARM texture (t2) - preferred over individual maps
                    auto arm_tex = pbr_mat->get_arm_texture();
                    if (arm_tex && arm_tex->texture_) {
                        cmd->bind_texture(arm_tex->texture_, 2, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_black) {
                        cmd->bind_texture(fallback_black, 2, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    // Bind individual maps (t3-t6) when ARM is not available
                    auto roughness_tex = pbr_mat->get_roughness_texture();
                    if (roughness_tex && roughness_tex->texture_) {
                        cmd->bind_texture(roughness_tex->texture_, 3, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_black) {
                        cmd->bind_texture(fallback_black, 3, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    auto metallic_tex = pbr_mat->get_metallic_texture();
                    if (metallic_tex && metallic_tex->texture_) {
                        cmd->bind_texture(metallic_tex->texture_, 4, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_black) {
                        cmd->bind_texture(fallback_black, 4, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    auto ao_tex = pbr_mat->get_ao_texture();
                    if (ao_tex && ao_tex->texture_) {
                        cmd->bind_texture(ao_tex->texture_, 5, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_white) { // White = AO=1.0 (no occlusion)
                        cmd->bind_texture(fallback_white, 5, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    auto emission_tex = pbr_mat->get_emission_texture();
                    if (emission_tex && emission_tex->texture_) {
                        cmd->bind_texture(emission_tex->texture_, 6, SHADER_FREQUENCY_FRAGMENT);
                    } else if (fallback_black) {
                        cmd->bind_texture(fallback_black, 6, SHADER_FREQUENCY_FRAGMENT);
                    }
                } else {
                    // No material or not PBR - bind all fallbacks
                    if (fallback_white) {
                        cmd->bind_texture(fallback_white, 0, SHADER_FREQUENCY_FRAGMENT); // albedo
                        cmd->bind_texture(fallback_white, 5, SHADER_FREQUENCY_FRAGMENT); // ao
                    }
                    if (fallback_normal) {
                        cmd->bind_texture(fallback_normal, 1, SHADER_FREQUENCY_FRAGMENT); // normal
                    }
                    if (fallback_black) {
                        cmd->bind_texture(fallback_black, 2, SHADER_FREQUENCY_FRAGMENT); // arm
                        cmd->bind_texture(fallback_black, 3, SHADER_FREQUENCY_FRAGMENT); // roughness
                        cmd->bind_texture(fallback_black, 4, SHADER_FREQUENCY_FRAGMENT); // metallic
                        cmd->bind_texture(fallback_black, 6, SHADER_FREQUENCY_FRAGMENT); // emission
                    }
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
                
                if (batch.index_buffer) {
                    cmd->bind_index_buffer(batch.index_buffer, 0);
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
