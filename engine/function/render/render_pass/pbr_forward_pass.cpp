#include "engine/function/render/render_pass/pbr_forward_pass.h"
#include "engine/function/render/render_resource/material.h"
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
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogPBRForwardPass, "PBRForwardPass");

namespace render {

static std::vector<uint8_t> load_shader(const std::string& cso_name, const char* entry, const char* profile) {
    return render::ShaderUtils::load_or_compile(cso_name, nullptr, entry, profile);
}

PBRForwardPass::PBRForwardPass() = default;

PBRForwardPass::~PBRForwardPass() {
    if (solid_pipeline_) solid_pipeline_->destroy();
    if (wireframe_pipeline_) wireframe_pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
    if (material_buffer_) material_buffer_->destroy();
    if (default_sampler_) default_sampler_->destroy();
    if (default_normal_buffer_) default_normal_buffer_->destroy();
    if (default_tangent_buffer_) default_tangent_buffer_->destroy();
    if (default_texcoord_buffer_) default_texcoord_buffer_->destroy();
}

void PBRForwardPass::init() {
    INFO(LogPBRForwardPass, "Initializing PBRForwardPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogPBRForwardPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_ || !material_buffer_) {
        ERR(LogPBRForwardPass, "Failed to create uniform buffers");
        return;
    }
    
    create_samplers();
    if (!default_sampler_) {
        ERR(LogPBRForwardPass, "Failed to create samplers");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogPBRForwardPass, "Failed to create pipeline");
        return;
    }
    
    create_default_vertex_buffers();
    
    initialized_ = true;
    INFO(LogPBRForwardPass, "PBRForwardPass initialized successfully");
}

void PBRForwardPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load pre-compiled vertex shader
    auto vs_code = load_shader("pbr_forward_vs.cso", "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogPBRForwardPass, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogPBRForwardPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Load pre-compiled fragment shader
    auto fs_code = load_shader("pbr_forward_ps.cso", "PSMain", "ps_5_0");
    if (fs_code.empty()) {
        ERR(LogPBRForwardPass, "Failed to load/compile fragment shader");
        return;
    }
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "PSMain";
    fs_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    fs_info.code = fs_code;
    
    auto fs = backend->create_shader(fs_info);
    if (!fs) {
        ERR(LogPBRForwardPass, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = fs;
    
    INFO(LogPBRForwardPass, "PBR shaders created successfully");
}

void PBRForwardPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Create per-frame buffer (b0)
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(PBRPerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogPBRForwardPass, "Failed to create per-frame buffer");
        return;
    }
    
    // Create per-object buffer (b1)
    RHIBufferInfo object_info = {};
    object_info.size = sizeof(PBRPerObjectData);
    object_info.stride = 0;
    object_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    object_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    object_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_object_buffer_ = backend->create_buffer(object_info);
    if (!per_object_buffer_) {
        ERR(LogPBRForwardPass, "Failed to create per-object buffer");
        return;
    }
    
    // Create material buffer (b2)
    RHIBufferInfo material_info = {};
    material_info.size = sizeof(PBRMaterialData);
    material_info.stride = 0;
    material_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    material_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    material_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    material_buffer_ = backend->create_buffer(material_info);
    if (!material_buffer_) {
        ERR(LogPBRForwardPass, "Failed to create material buffer");
        return;
    }
    
    INFO(LogPBRForwardPass, "Uniform buffers created successfully");
}

void PBRForwardPass::create_samplers() {
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
        ERR(LogPBRForwardPass, "Failed to create default sampler");
    }
}

void PBRForwardPass::create_default_vertex_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    std::vector<Vec3> default_normals(DEFAULT_VERTEX_COUNT, Vec3(0, 1, 0));
    RHIBufferInfo normal_info = {};
    normal_info.size = default_normals.size() * sizeof(Vec3);
    normal_info.stride = sizeof(Vec3);
    normal_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    normal_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    normal_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    default_normal_buffer_ = backend->create_buffer(normal_info);
    if (default_normal_buffer_) {
        void* mapped = default_normal_buffer_->map();
        if (mapped) {
            memcpy(mapped, default_normals.data(), normal_info.size);
            default_normal_buffer_->unmap();
        }
    }
    
    std::vector<Vec4> default_tangents(DEFAULT_VERTEX_COUNT, Vec4(0, 0, 0, 1));
    RHIBufferInfo tangent_info = {};
    tangent_info.size = default_tangents.size() * sizeof(Vec4);
    tangent_info.stride = sizeof(Vec4);
    tangent_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    tangent_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    tangent_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    default_tangent_buffer_ = backend->create_buffer(tangent_info);
    if (default_tangent_buffer_) {
        void* mapped = default_tangent_buffer_->map();
        if (mapped) {
            memcpy(mapped, default_tangents.data(), tangent_info.size);
            default_tangent_buffer_->unmap();
        }
    }
    
    std::vector<Vec2> default_texcoords(DEFAULT_VERTEX_COUNT, Vec2(0, 0));
    RHIBufferInfo texcoord_info = {};
    texcoord_info.size = default_texcoords.size() * sizeof(Vec2);
    texcoord_info.stride = sizeof(Vec2);
    texcoord_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    texcoord_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    texcoord_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    default_texcoord_buffer_ = backend->create_buffer(texcoord_info);
    if (default_texcoord_buffer_) {
        void* mapped = default_texcoord_buffer_->map();
        if (mapped) {
            memcpy(mapped, default_texcoords.data(), texcoord_info.size);
            default_texcoord_buffer_->unmap();
        }
    }
    
    INFO(LogPBRForwardPass, "Default vertex buffers created");
}

void PBRForwardPass::create_pipeline() {
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
    pipe_info.vertex_input_state.vertex_elements[2].semantic_name = "TANGENT";
    pipe_info.vertex_input_state.vertex_elements[2].format = FORMAT_R32G32B32A32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[2].offset = 0;
    pipe_info.vertex_input_state.vertex_elements[3].stream_index = 3;
    pipe_info.vertex_input_state.vertex_elements[3].semantic_name = "TEXCOORD";
    pipe_info.vertex_input_state.vertex_elements[3].format = FORMAT_R32G32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[3].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
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
    
    solid_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!solid_pipeline_) {
        ERR(LogPBRForwardPass, "Failed to create solid graphics pipeline");
        return;
    }
    
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_WIREFRAME;
    wireframe_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!wireframe_pipeline_) {
        ERR(LogPBRForwardPass, "Failed to create wireframe graphics pipeline");
        return;
    }
    
    pipeline_ = solid_pipeline_;
    
    INFO(LogPBRForwardPass, "PBR pipelines created successfully");
}

void PBRForwardPass::set_wireframe(bool enable) {
    if (wireframe_mode_ == enable) return;
    
    wireframe_mode_ = enable;
    pipeline_ = enable ? wireframe_pipeline_ : solid_pipeline_;
    
    INFO(LogPBRForwardPass, "Switched to {} mode", enable ? "wireframe" : "solid");
}

void PBRForwardPass::set_per_frame_data(const Mat4& view, const Mat4& proj, 
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

void PBRForwardPass::add_point_light(const Vec3& pos, const Vec3& color, 
                                       float intensity, float range) {
    if (per_frame_data_.point_light_count >= 4) return;
    
    int idx = per_frame_data_.point_light_count;
    per_frame_data_.point_light_pos[idx] = Vec4(pos.x(), pos.y(), pos.z(), range);
    per_frame_data_.point_light_color[idx] = Vec4(color.x(), color.y(), color.z(), intensity);
    per_frame_data_.point_light_count++;
    per_frame_dirty_ = true;
}

void PBRForwardPass::clear_point_lights() {
    per_frame_data_.point_light_count = 0;
    per_frame_dirty_ = true;
}

void PBRForwardPass::draw_batch(RHICommandContextRef cmd, const DrawBatch& batch) {
    if (!initialized_ || !pipeline_ || !cmd) {
        ERR(LogPBRForwardPass, "Draw batch failed: initialized={}, pipeline={}, cmd={}", 
            initialized_, (pipeline_ != nullptr), (cmd != nullptr));
        return;
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

    cmd->set_graphics_pipeline(pipeline_);
    cmd->bind_constant_buffer(material_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
    cmd->bind_sampler(default_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);

    if (per_object_buffer_) {
        PBRPerObjectData object_data;
        object_data.model = batch.model_matrix;
        object_data.inv_model = batch.inv_model_matrix;
        void* mapped = per_object_buffer_->map();
        if (mapped) {
            memcpy(mapped, &object_data, sizeof(object_data));
            per_object_buffer_->unmap();
        }
        cmd->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
    }

    auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(batch.material);
    if (material_buffer_ && pbr_mat) {
        PBRMaterialData mat_data;
        mat_data.albedo = pbr_mat->get_diffuse();
        mat_data.emission = pbr_mat->get_emission();
        mat_data.alpha_cutoff = pbr_mat->get_alpha_clip();
        mat_data.roughness = pbr_mat->get_roughness();
        mat_data.metallic = pbr_mat->get_metallic();
        mat_data.use_arm_map = pbr_mat->get_arm_texture() ? 1 : 0;
        mat_data.use_albedo_map = pbr_mat->get_diffuse_texture() ? 1 : 0;
        mat_data.use_normal_map = pbr_mat->get_normal_texture() ? 1 : 0;
        mat_data.use_emission_map = 0;
        
        void* mapped = material_buffer_->map();
        if (mapped) {
            memcpy(mapped, &mat_data, sizeof(mat_data));
            material_buffer_->unmap();
        }
    }

    // Bind textures with fallback
    auto* render_system = EngineContext::render_system();
    RHITextureRef fallback_white = render_system ? render_system->get_fallback_white_texture() : nullptr;
    RHITextureRef fallback_black = render_system ? render_system->get_fallback_black_texture() : nullptr;
    RHITextureRef fallback_normal = render_system ? render_system->get_fallback_normal_texture() : nullptr;
    
    if (pbr_mat) {
        auto albedo_tex = pbr_mat->get_diffuse_texture();
        if (albedo_tex && albedo_tex->texture_) {
            cmd->bind_texture(albedo_tex->texture_, 0, SHADER_FREQUENCY_FRAGMENT);
        } else if (fallback_white) {
            cmd->bind_texture(fallback_white, 0, SHADER_FREQUENCY_FRAGMENT);
        }
        
        auto normal_tex = pbr_mat->get_normal_texture();
        if (normal_tex && normal_tex->texture_) {
            cmd->bind_texture(normal_tex->texture_, 1, SHADER_FREQUENCY_FRAGMENT);
        } else if (fallback_normal) {
            cmd->bind_texture(fallback_normal, 1, SHADER_FREQUENCY_FRAGMENT);
        }
        
        auto arm_tex = pbr_mat->get_arm_texture();
        if (arm_tex && arm_tex->texture_) {
            cmd->bind_texture(arm_tex->texture_, 2, SHADER_FREQUENCY_FRAGMENT);
        } else if (fallback_black) {
            // ARM: Black = AO=0, Roughness=0, Metallic=0 (default non-metal, smooth)
            cmd->bind_texture(fallback_black, 2, SHADER_FREQUENCY_FRAGMENT);
        }
    } else {
        // No material, bind all fallbacks
        if (fallback_white) {
            cmd->bind_texture(fallback_white, 0, SHADER_FREQUENCY_FRAGMENT);
        }
        if (fallback_normal) {
            cmd->bind_texture(fallback_normal, 1, SHADER_FREQUENCY_FRAGMENT);
        }
        if (fallback_black) {
            cmd->bind_texture(fallback_black, 2, SHADER_FREQUENCY_FRAGMENT);
        }
    }

    if (batch.vertex_buffer) {
        cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
    }
    if (batch.normal_buffer) {
        cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
    } else if (default_normal_buffer_) {
        cmd->bind_vertex_buffer(default_normal_buffer_, 1, 0);
    }
    if (batch.tangent_buffer) {
        cmd->bind_vertex_buffer(batch.tangent_buffer, 2, 0);
    } else if (default_tangent_buffer_) {
        cmd->bind_vertex_buffer(default_tangent_buffer_, 2, 0);
    }
    if (batch.texcoord_buffer) {
        cmd->bind_vertex_buffer(batch.texcoord_buffer, 3, 0);
    } else if (default_texcoord_buffer_) {
        cmd->bind_vertex_buffer(default_texcoord_buffer_, 3, 0);
    }

    if (batch.index_buffer) {
        cmd->bind_index_buffer(batch.index_buffer, 0);
        cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
    }
}

void PBRForwardPass::execute_batches(RHICommandListRef cmd, const std::vector<DrawBatch>& batches) {
    if (!initialized_ || !pipeline_ || !cmd) {
        ERR(LogPBRForwardPass, "Execute batches failed: initialized={}, pipeline={}, cmd={}", 
            initialized_, (pipeline_ != nullptr), (cmd != nullptr));
        return;
    }

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

    cmd->bind_constant_buffer(material_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
    cmd->bind_sampler(default_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);

    for (const auto& batch : batches) {
        if (per_object_buffer_) {
            PBRPerObjectData object_data;
            object_data.model = batch.model_matrix;
            object_data.inv_model = batch.inv_model_matrix;
            void* mapped = per_object_buffer_->map();
            if (mapped) {
                memcpy(mapped, &object_data, sizeof(object_data));
                per_object_buffer_->unmap();
            }
            cmd->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
        }

        auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(batch.material);
        if (material_buffer_ && pbr_mat) {
            PBRMaterialData mat_data;
            mat_data.albedo = pbr_mat->get_diffuse();
            mat_data.emission = pbr_mat->get_emission();
            mat_data.alpha_cutoff = pbr_mat->get_alpha_clip();
            mat_data.roughness = pbr_mat->get_roughness();
            mat_data.metallic = pbr_mat->get_metallic();
            mat_data.use_arm_map = pbr_mat->get_arm_texture() ? 1 : 0;
            mat_data.use_albedo_map = pbr_mat->get_diffuse_texture() ? 1 : 0;
            mat_data.use_normal_map = pbr_mat->get_normal_texture() ? 1 : 0;
            mat_data.use_emission_map = 0;
            
            void* mapped = material_buffer_->map();
            if (mapped) {
                memcpy(mapped, &mat_data, sizeof(mat_data));
                material_buffer_->unmap();
            }
        }

        if (pbr_mat) {
            auto albedo_tex = pbr_mat->get_diffuse_texture();
            if (albedo_tex && albedo_tex->texture_) {
                cmd->bind_texture(albedo_tex->texture_, 0, SHADER_FREQUENCY_FRAGMENT);
            }
            
            auto normal_tex = pbr_mat->get_normal_texture();
            if (normal_tex && normal_tex->texture_) {
                cmd->bind_texture(normal_tex->texture_, 1, SHADER_FREQUENCY_FRAGMENT);
            }
            
            auto arm_tex = pbr_mat->get_arm_texture();
            if (arm_tex && arm_tex->texture_) {
                cmd->bind_texture(arm_tex->texture_, 2, SHADER_FREQUENCY_FRAGMENT);
            }
        }

        if (batch.vertex_buffer) {
            cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
        }
        if (batch.normal_buffer) {
            cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
        } else if (default_normal_buffer_) {
            cmd->bind_vertex_buffer(default_normal_buffer_, 1, 0);
        }
        if (batch.tangent_buffer) {
            cmd->bind_vertex_buffer(batch.tangent_buffer, 2, 0);
        } else if (default_tangent_buffer_) {
            cmd->bind_vertex_buffer(default_tangent_buffer_, 2, 0);
        }
        if (batch.texcoord_buffer) {
            cmd->bind_vertex_buffer(batch.texcoord_buffer, 3, 0);
        } else if (default_texcoord_buffer_) {
            cmd->bind_vertex_buffer(default_texcoord_buffer_, 3, 0);
        }

        if (batch.index_buffer) {
            cmd->bind_index_buffer(batch.index_buffer, 0);
            cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
        }
    }
}

void PBRForwardPass::build(RDGBuilder& builder, RDGTextureHandle color_target, 
                           std::optional<RDGTextureHandle> depth_target,
                           const std::vector<DrawBatch>& batches) {
    if (!initialized_ || !pipeline_) {
        ERR(LogPBRForwardPass, "Build failed: not initialized or no pipeline");
        return;
    }
    
    // Create render pass
    auto rp_builder = builder.create_render_pass("PBRForwardPass")
        .color(0, color_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.1f, 0.1f, 0.2f, 1.0f});
    
    // Add depth attachment if available
    // Use LOAD to read depth prepass results, depth write is still enabled for early-z
    if (depth_target.has_value()) {
        rp_builder.depth_stencil(depth_target.value(), ATTACHMENT_LOAD_OP_LOAD, 
                                 ATTACHMENT_STORE_OP_DONT_CARE, 1.0f, 0);
        
        // Declare dependency on depth texture (as input attachment)
        rp_builder.read(0, 0, 0, depth_target.value(), VIEW_TYPE_2D, 
                        TextureSubresourceRange{TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1});
    }
    
    // Capture batches and this pointer for execute lambda
    rp_builder.execute([this, batches](RDGPassContext context) {
        RHICommandListRef cmd = context.command;
        if (!cmd) return;
        
        // Execute all batches
        execute_batches(cmd, batches);
    })
    .finish();
}

} // namespace render
