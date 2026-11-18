#include "engine/function/render/render_pass/draw_material.h"
#include "engine/function/render/render_pass/npr_forward_pass.h"
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
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogNPRForwardPass, "NPRForwardPass");

namespace render {

// Load pre-compiled shader or compile from source file
static std::vector<uint8_t> load_shader(const std::string& cso_name, const char* entry, const char* profile) {
    return render::ShaderUtils::load_or_compile(cso_name, nullptr, entry, profile);
}

NPRForwardPass::NPRForwardPass() = default;

NPRForwardPass::~NPRForwardPass() {
    if (solid_pipeline_) solid_pipeline_->destroy();
    if (wireframe_pipeline_) wireframe_pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
    if (material_buffer_) material_buffer_->destroy();
    if (default_sampler_) default_sampler_->destroy();
    if (clamp_sampler_) clamp_sampler_->destroy();
    if (default_normal_buffer_) default_normal_buffer_->destroy();
    if (default_tangent_buffer_) default_tangent_buffer_->destroy();
    if (default_texcoord_buffer_) default_texcoord_buffer_->destroy();
}

void NPRForwardPass::init() {
    INFO(LogNPRForwardPass, "Initializing NPRForwardPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogNPRForwardPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_ || !material_buffer_) {
        ERR(LogNPRForwardPass, "Failed to create uniform buffers");
        return;
    }
    
    create_samplers();
    if (!default_sampler_) {
        ERR(LogNPRForwardPass, "Failed to create samplers");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogNPRForwardPass, "Failed to create pipeline");
        return;
    }
    
    create_default_vertex_buffers();
    
    initialized_ = true;
    INFO(LogNPRForwardPass, "NPRForwardPass initialized successfully");
}

void NPRForwardPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load vertex shader
    auto vs_code = load_shader("npr_forward_vs.cso", "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogNPRForwardPass, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogNPRForwardPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Load fragment shader
    auto fs_code = load_shader("npr_forward_ps.cso", "PSMain", "ps_5_0");
    if (fs_code.empty()) {
        ERR(LogNPRForwardPass, "Failed to load/compile fragment shader");
        return;
    }
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "PSMain";
    fs_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    fs_info.code = fs_code;
    
    auto fs = backend->create_shader(fs_info);
    if (!fs) {
        ERR(LogNPRForwardPass, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = fs;
    
    INFO(LogNPRForwardPass, "NPR shaders created successfully");
}

void NPRForwardPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Create per-frame buffer (b0)
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(NPRPerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogNPRForwardPass, "Failed to create per-frame buffer");
        return;
    }
    
    // Create per-object buffer (b1)
    RHIBufferInfo object_info = {};
    object_info.size = sizeof(NPRPerObjectData);
    object_info.stride = 0;
    object_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    object_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    object_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_object_buffer_ = backend->create_buffer(object_info);
    if (!per_object_buffer_) {
        ERR(LogNPRForwardPass, "Failed to create per-object buffer");
        return;
    }
    
    // Create material buffer (b2)
    RHIBufferInfo material_info = {};
    material_info.size = sizeof(NPRMaterialData);
    material_info.stride = 0;
    material_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    material_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    material_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    material_buffer_ = backend->create_buffer(material_info);
    if (!material_buffer_) {
        ERR(LogNPRForwardPass, "Failed to create material buffer");
        return;
    }
    
    INFO(LogNPRForwardPass, "Uniform buffers created successfully");
}

void NPRForwardPass::create_samplers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Default sampler (repeat mode)
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
        ERR(LogNPRForwardPass, "Failed to create default sampler");
    }
    
    // Clamp sampler for ramp texture (clamp to edge)
    RHISamplerInfo clamp_sampler_info = {};
    clamp_sampler_info.min_filter = FILTER_TYPE_LINEAR;
    clamp_sampler_info.mag_filter = FILTER_TYPE_LINEAR;
    clamp_sampler_info.mipmap_mode = MIPMAP_MODE_LINEAR;
    clamp_sampler_info.address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE;
    clamp_sampler_info.address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE;
    clamp_sampler_info.address_mode_w = ADDRESS_MODE_CLAMP_TO_EDGE;
    clamp_sampler_info.max_anisotropy = 16.0f;
    
    clamp_sampler_ = backend->create_sampler(clamp_sampler_info);
    if (!clamp_sampler_) {
        ERR(LogNPRForwardPass, "Failed to create clamp sampler");
    }
}

void NPRForwardPass::create_default_vertex_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Create default normal buffer (all pointing up)
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
    
    // Create default tangent buffer
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
    
    // Create default texcoord buffer (all zeros)
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
    
    INFO(LogNPRForwardPass, "Default vertex buffers created");
}

void NPRForwardPass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;
    
    // Create root signature
    RHIRootSignatureInfo root_info = {};
    root_info.add_entry_from_reflect(vertex_shader_->shader_);
    root_info.add_entry_from_reflect(fragment_shader_->shader_);
    root_signature_ = backend->create_root_signature(root_info);
    if (root_signature_) descriptor_set_ = root_signature_->create_descriptor_set(0);
    if (!root_signature_) return;
    
    // Common pipeline configuration
    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    // Vertex input layout: position + normal + tangent + texcoord
    pipe_info.vertex_input_state.vertex_elements.resize(4);
    // Position - stream 0
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].semantic_name = "POSITION";
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    // Normal - stream 1
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].semantic_name = "NORMAL";
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    // Tangent - stream 2
    pipe_info.vertex_input_state.vertex_elements[2].stream_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].semantic_name = "TANGENT";
    pipe_info.vertex_input_state.vertex_elements[2].format = FORMAT_R32G32B32A32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[2].offset = 0;
    // TexCoord - stream 3
    pipe_info.vertex_input_state.vertex_elements[3].stream_index = 3;
    pipe_info.vertex_input_state.vertex_elements[3].semantic_name = "TEXCOORD";
    pipe_info.vertex_input_state.vertex_elements[3].format = FORMAT_R32G32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[3].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    // Enable depth testing
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = false;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    
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
    solid_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!solid_pipeline_) {
        ERR(LogNPRForwardPass, "Failed to create solid graphics pipeline");
        return;
    }
    
    // Create wireframe pipeline
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_WIREFRAME;
    wireframe_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!wireframe_pipeline_) {
        ERR(LogNPRForwardPass, "Failed to create wireframe graphics pipeline");
        return;
    }
    
    pipeline_ = solid_pipeline_;
    
    INFO(LogNPRForwardPass, "NPR pipelines created successfully");
}

void NPRForwardPass::set_wireframe(bool enable) {
    if (wireframe_mode_ == enable) return;
    
    wireframe_mode_ = enable;
    pipeline_ = enable ? wireframe_pipeline_ : solid_pipeline_;
    
    INFO(LogNPRForwardPass, "Switched to {} mode", enable ? "wireframe" : "solid");
}

void NPRForwardPass::set_per_frame_data(const Mat4& view, const Mat4& proj, 
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

void NPRForwardPass::draw_batch(RHICommandContextRef cmd, const DrawBatch& batch, const Extent2D& extent) {
    if (!initialized_ || !pipeline_ || !cmd) {
        ERR(LogNPRForwardPass, "Draw batch failed: initialized={}, pipeline={}, cmd={}", 
            initialized_, (pipeline_ != nullptr), (cmd != nullptr));
        return;
    }

    // Set viewport and scissor - always set them to ensure valid rendering state
    cmd->set_viewport({0, 0}, {extent.width, extent.height});
    cmd->set_scissor({0, 0}, {extent.width, extent.height});

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
        descriptor_set_->write_buffer(0, per_frame_buffer_);
    }

    // Bind common resources
    cmd->set_graphics_pipeline(pipeline_);
    descriptor_set_->write_buffer(2, material_buffer_);
    descriptor_set_->write_sampler(0, default_sampler_);
    descriptor_set_->write_sampler(1, clamp_sampler_);
    
    // Bind depth texture for screen space rim light (slot 4)
    if (depth_texture_) {
        descriptor_set_->write_texture(4, depth_texture_);
    }

    // Update per-object buffer
    if (per_object_buffer_) {
        NPRPerObjectData object_data;
        object_data.model = batch.model_matrix;
        object_data.inv_model = batch.inv_model_matrix;
        void* mapped = per_object_buffer_->map();
        if (mapped) {
            memcpy(mapped, &object_data, sizeof(object_data));
            per_object_buffer_->unmap();
        }
        descriptor_set_->write_buffer(1, per_object_buffer_);
    }

    // Update material buffer
    auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(batch.material);
    if (material_buffer_ && npr_mat) {
        NPRMaterialData mat_data = {};
        mat_data.albedo = npr_mat->get_diffuse();
        mat_data.emission = npr_mat->get_emission();
        
        // NPR parameters
        float lambert_clamp = npr_mat->get_lambert_clamp();
        float ramp_tex_offset = npr_mat->get_ramp_offset();
        float rim_threshold = npr_mat->get_rim_threshold();
        float rim_strength = npr_mat->get_rim_strength();
        float rim_width = npr_mat->get_rim_width();
        Vec3 rim_color = npr_mat->get_rim_color();
        float use_light_map = npr_mat->get_light_map_texture() ? 1.0f : 0.0f;
        float use_ramp_map = npr_mat->get_ramp_texture() ? 1.0f : 0.0f;
        
        auto diffuse_tex = npr_mat->get_diffuse_texture();
        float use_albedo_map = diffuse_tex ? 1.0f : 0.0f;
        float use_normal_map = npr_mat->get_normal_texture() ? 1.0f : 0.0f;
        float face_mode = npr_mat->get_face_mode() ? 1.0f : 0.0f;
        
        // Use helper to set all NPR params
        set_npr_params(mat_data, 
            lambert_clamp, ramp_tex_offset, rim_threshold, rim_strength,
            rim_width, use_albedo_map, use_normal_map, use_light_map,
            rim_color, use_ramp_map, face_mode);
        
        void* mapped = material_buffer_->map();
        if (mapped) {
            memcpy(mapped, &mat_data, sizeof(mat_data));
            material_buffer_->unmap();
        }
    }

    // Bind textures (all from NPRMaterial) with fallback
    auto* render_system = EngineContext::render_system();
    RHITextureRef fallback_white = render_system ? render_system->get_fallback_white_texture() : nullptr;
    RHITextureRef fallback_normal = render_system ? render_system->get_fallback_normal_texture() : nullptr;
    RHITextureRef fallback_black = render_system ? render_system->get_fallback_black_texture() : nullptr;
    
    // Bind depth texture fallback if not set
    if (!depth_texture_ && fallback_black) {
        WARN(LogNPRForwardPass, "Depth texture not set for NPR pass, using fallback black");
        descriptor_set_->write_texture(4, fallback_black);
    }
    
    if (npr_mat) {
        auto albedo_tex = npr_mat->get_diffuse_texture();
        if (albedo_tex && albedo_tex->texture_) {
            descriptor_set_->write_texture(0, albedo_tex->texture_);
        } else {
            WARN(LogNPRForwardPass, "NPR Material '{}' is missing diffuse/albedo texture, using fallback white", 
                 npr_mat->get_name());
            if (fallback_white) {
                descriptor_set_->write_texture(0, fallback_white);
            }
        }
        
        auto normal_tex = npr_mat->get_normal_texture();
        if (normal_tex && normal_tex->texture_) {
            descriptor_set_->write_texture(1, normal_tex->texture_);
        } else {
            // WARN(LogNPRForwardPass, "NPR Material '{}' is missing normal texture, using fallback", 
                //  npr_mat->get_name());
            if (fallback_normal) {
                descriptor_set_->write_texture(1, fallback_normal);
            }
        }
        
        auto light_map_tex = npr_mat->get_light_map_texture();
        if (light_map_tex && light_map_tex->texture_) {
            descriptor_set_->write_texture(2, light_map_tex->texture_);
        } else {
            // WARN(LogNPRForwardPass, "NPR Material '{}' is missing light map texture, using fallback white", 
                //  npr_mat->get_name());
            if (fallback_white) {
                descriptor_set_->write_texture(2, fallback_white);
            }
        }
        
        auto ramp_tex = npr_mat->get_ramp_texture();
        if (ramp_tex && ramp_tex->texture_) {
            descriptor_set_->write_texture(3, ramp_tex->texture_);
        } else {
            // WARN(LogNPRForwardPass, "NPR Material '{}' is missing ramp texture, using fallback white", 
                //  npr_mat->get_name());
            if (fallback_white) {
                descriptor_set_->write_texture(3, fallback_white);
            }
        }
    } else {
        // No material, bind all fallbacks
        WARN(LogNPRForwardPass, "Draw batch has no NPR material, using all fallback textures");
        if (fallback_white) {
            descriptor_set_->write_texture(0, fallback_white);
            descriptor_set_->write_texture(2, fallback_white);
            descriptor_set_->write_texture(3, fallback_white);
        }
        if (fallback_normal) {
            descriptor_set_->write_texture(1, fallback_normal);
        }
    }

    // Bind vertex buffers
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

    // Draw
    if (batch.index_buffer) {
        cmd->bind_index_buffer(batch.index_buffer, 0);
        cmd->bind_descriptor_set(descriptor_set_, 0);
        cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
    }
}

void NPRForwardPass::execute_batches(RHICommandListRef cmd, const std::vector<DrawBatch>& batches, const Extent2D& extent) {
    record_batches(cmd, batches, extent, per_frame_data_, pipeline_, depth_texture_);
}

void NPRForwardPass::record_batches(RHICommandListRef cmd, const std::vector<DrawBatch>& batches, const Extent2D& extent,
                                    const NPRPerFrameData& frame_data, const RHIGraphicsPipelineRef& pipeline,
                                    const RHITextureRef& depth_texture) {
    if (!initialized_ || !pipeline || !cmd) {
        ERR(LogNPRForwardPass, "Execute batches failed: initialized={}, pipeline={}, cmd={}", 
            initialized_, (pipeline != nullptr), (cmd != nullptr));
        return;
    }

    // Set viewport and scissor - always set them to ensure valid rendering state
    cmd->set_viewport({0, 0}, {extent.width, extent.height});
    cmd->set_scissor({0, 0}, {extent.width, extent.height});

    cmd->set_graphics_pipeline(pipeline);
    
    // Update and bind per-frame buffer
    if (per_frame_buffer_) {
        if (void* mapped = per_frame_buffer_->map()) {
            memcpy(mapped, &frame_data, sizeof(frame_data));
            per_frame_buffer_->unmap();
        }
        descriptor_set_->write_buffer(0, per_frame_buffer_);
    }

    descriptor_set_->write_buffer(2, material_buffer_);
    descriptor_set_->write_sampler(0, default_sampler_);
    descriptor_set_->write_sampler(1, clamp_sampler_);
    
    // Bind depth texture for screen space rim light (slot 4)
    if (depth_texture) {
        descriptor_set_->write_texture(4, depth_texture);
    } else {
        auto* render_system = EngineContext::render_system();
        RHITextureRef fallback_black = render_system ? render_system->get_fallback_black_texture() : nullptr;
        WARN(LogNPRForwardPass, "Depth texture not available for NPR pass, using fallback black");
        if (fallback_black) {
            descriptor_set_->write_texture(4, fallback_black);
        }
    }

    // Draw all batches
    for (const auto& batch : batches) {
        // Update per-object buffer
        if (per_object_buffer_) {
            NPRPerObjectData object_data;
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
        if (material_buffer_ && material.npr) {
            if (void* mapped = material_buffer_->map()) {
                memcpy(mapped, &*material.npr, sizeof(NPRMaterialData));
                material_buffer_->unmap();
            }
        }

        auto* render_system = EngineContext::render_system();
        const auto white = render_system ? render_system->get_fallback_white_texture() : nullptr;
        const auto normal = render_system ? render_system->get_fallback_normal_texture() : nullptr;
        const std::array fallbacks{white, normal, white, white};
        for (uint32_t slot = 0; slot < fallbacks.size(); ++slot) {
            const auto& texture = material.npr ? material.textures[slot] : fallbacks[slot];
            descriptor_set_->write_texture(slot, texture ? texture : fallbacks[slot]);
        }

        // Bind vertex buffers
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

        // Draw
        if (batch.index_buffer) {
            cmd->bind_index_buffer(batch.index_buffer, 0);
            cmd->bind_descriptor_set(descriptor_set_, 0);
            cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
        }
    }
}

void NPRForwardPass::build(RDGBuilder& builder, RDGTextureHandle color_target, 
                           RDGTextureHandle depth_target,
                           const std::vector<DrawBatch>& batches) {
    if (!initialized_ || !pipeline_) {
        ERR(LogNPRForwardPass, "Build failed: not initialized or no pipeline");
        return;
    }
    
    // Get extent from render system (color_target doesn't have direct extent access)
    Extent2D extent = {1280, 720};  // Default fallback
    auto* render_system = EngineContext::render_system();
    if (render_system && render_system->get_swapchain()) {
        extent = render_system->get_swapchain()->get_extent();
    }
    
    // Create render pass
    auto rp_builder = builder.create_render_pass("NPRForwardPass")
        .root_signature(root_signature_)
        .descriptor_set(0, descriptor_set_)
        .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.1f, 0.1f, 0.2f, 1.0f});
    
    rp_builder.depth_stencil(depth_target, ATTACHMENT_LOAD_OP_LOAD, 
                             ATTACHMENT_STORE_OP_DONT_CARE, 1.0f, 0, {}, true);
    
    rp_builder.read(0, 4, 0, depth_target, VIEW_TYPE_2D,
                    TextureSubresourceRange{TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1});
    
    rp_builder.execute([this, batches, depth_target, extent, frame_data = per_frame_data_, pipeline = pipeline_](RDGPassContext context) {
        record_batches(context.command, batches, extent, frame_data, pipeline, context.builder->resolve(depth_target));
    })
    .finish();
}

} // namespace render
