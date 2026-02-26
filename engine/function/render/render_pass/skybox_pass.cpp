#include "engine/function/render/render_pass/skybox_pass.h"
#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogSkyboxPass, "SkyboxPass");

namespace render {

SkyboxPass::SkyboxPass() = default;

SkyboxPass::~SkyboxPass() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (per_frame_buffer_) per_frame_buffer_->destroy();
    if (per_object_buffer_) per_object_buffer_->destroy();
    if (params_buffer_) params_buffer_->destroy();
    if (cube_sampler_) cube_sampler_->destroy();
}

void SkyboxPass::init() {
    INFO(LogSkyboxPass, "Initializing SkyboxPass...");
    
    create_shaders();
    if (!vertex_shader_ || !fragment_shader_) {
        ERR(LogSkyboxPass, "Failed to create shaders");
        return;
    }
    
    create_uniform_buffers();
    if (!per_frame_buffer_ || !per_object_buffer_ || !params_buffer_) {
        ERR(LogSkyboxPass, "Failed to create uniform buffers");
        return;
    }
    
    create_samplers();
    if (!cube_sampler_) {
        ERR(LogSkyboxPass, "Failed to create sampler");
        return;
    }
    
    create_pipeline();
    if (!pipeline_) {
        ERR(LogSkyboxPass, "Failed to create pipeline");
        return;
    }
    
    ensure_cube_mesh();
    
    initialized_ = true;
    INFO(LogSkyboxPass, "SkyboxPass initialized successfully");
}

void SkyboxPass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load vertex shader
    auto vs_code = ShaderUtils::load_or_compile("skybox_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogSkyboxPass, "Failed to load/compile skybox vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogSkyboxPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Load fragment shader
    auto ps_code = ShaderUtils::load_or_compile("skybox_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (ps_code.empty()) {
        ERR(LogSkyboxPass, "Failed to load/compile skybox fragment shader");
        return;
    }
    
    RHIShaderInfo ps_info = {};
    ps_info.entry = "PSMain";
    ps_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    ps_info.code = ps_code;
    
    auto ps = backend->create_shader(ps_info);
    if (!ps) {
        ERR(LogSkyboxPass, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = ps;
    
    INFO(LogSkyboxPass, "Shaders created successfully");
}

void SkyboxPass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Per-frame buffer (b0)
    RHIBufferInfo frame_info = {};
    frame_info.size = sizeof(PerFrameData);
    frame_info.stride = 0;
    frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_frame_buffer_ = backend->create_buffer(frame_info);
    if (!per_frame_buffer_) {
        ERR(LogSkyboxPass, "Failed to create per-frame buffer");
        return;
    }
    
    // Per-object buffer (b1)
    RHIBufferInfo object_info = {};
    object_info.size = sizeof(PerObjectData);
    object_info.stride = 0;
    object_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    object_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    object_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    per_object_buffer_ = backend->create_buffer(object_info);
    if (!per_object_buffer_) {
        ERR(LogSkyboxPass, "Failed to create per-object buffer");
        return;
    }
    
    // Skybox params buffer (b2)
    RHIBufferInfo params_info = {};
    params_info.size = sizeof(SkyboxParams);
    params_info.stride = 0;
    params_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    params_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    params_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    params_buffer_ = backend->create_buffer(params_info);
    if (!params_buffer_) {
        ERR(LogSkyboxPass, "Failed to create params buffer");
        return;
    }
    
    INFO(LogSkyboxPass, "Uniform buffers created successfully");
}

void SkyboxPass::create_samplers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    RHISamplerInfo sampler_info = {};
    sampler_info.min_filter = FILTER_TYPE_LINEAR;
    sampler_info.mag_filter = FILTER_TYPE_LINEAR;
    sampler_info.mipmap_mode = MIPMAP_MODE_LINEAR;
    sampler_info.address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = ADDRESS_MODE_CLAMP_TO_EDGE;
    
    cube_sampler_ = backend->create_sampler(sampler_info);
    if (!cube_sampler_) {
        ERR(LogSkyboxPass, "Failed to create cube sampler");
    }
}

void SkyboxPass::create_pipeline() {
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
    
    // Input Layout: Position only
    pipe_info.vertex_input_state.vertex_elements.resize(1);
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].semantic_name = "POSITION";
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    
    // Rasterizer: No culling (we see inside the cube)
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    // Depth Stencil: Test but don't write
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = false;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    
    // Color attachment format
    auto render_system = EngineContext::render_system();
    if (render_system) {
        pipe_info.color_attachment_formats[0] = render_system->get_color_format();
        pipe_info.depth_stencil_attachment_format = render_system->get_depth_format();
    } else {
        pipe_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
        pipe_info.depth_stencil_attachment_format = FORMAT_D32_SFLOAT;
    }
    
    pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!pipeline_) {
        ERR(LogSkyboxPass, "Failed to create graphics pipeline");
    }
}

void SkyboxPass::ensure_cube_mesh() {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogSkyboxPass, "Cannot create cube mesh: RHI backend not available");
        return;
    }
    
    // Cube vertices (positions only, 8 corners)
    // Using corners at (-1, -1, -1) to (1, 1, 1)
    std::vector<Vec3> vertices = {
        // Front face (z = -1)
        Vec3(-1.0f, -1.0f, -1.0f),  // 0
        Vec3( 1.0f, -1.0f, -1.0f),  // 1
        Vec3( 1.0f,  1.0f, -1.0f),  // 2
        Vec3(-1.0f,  1.0f, -1.0f),  // 3
        // Back face (z = 1)
        Vec3(-1.0f, -1.0f,  1.0f),  // 4
        Vec3( 1.0f, -1.0f,  1.0f),  // 5
        Vec3( 1.0f,  1.0f,  1.0f),  // 6
        Vec3(-1.0f,  1.0f,  1.0f),  // 7
    };
    
    // Cube indices (36 indices for 12 triangles, counter-clockwise when looking from outside)
    std::vector<uint32_t> indices = {
        // Front face (facing -z)
        0, 2, 1,
        0, 3, 2,
        // Back face (facing +z)
        4, 5, 6,
        4, 6, 7,
        // Left face (facing -x)
        4, 7, 3,
        4, 3, 0,
        // Right face (facing +x)
        1, 2, 6,
        1, 6, 5,
        // Bottom face (facing -y)
        0, 1, 5,
        0, 5, 4,
        // Top face (facing +y)
        3, 7, 6,
        3, 6, 2,
    };
    
    // Create vertex buffer
    RHIBufferInfo vb_info = {};
    vb_info.size = vertices.size() * sizeof(Vec3);
    vb_info.stride = sizeof(Vec3);
    vb_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    vb_info.type = RESOURCE_TYPE_VERTEX_BUFFER;
    vb_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    RHIBufferRef vb = backend->create_buffer(vb_info);
    if (!vb) {
        ERR(LogSkyboxPass, "Failed to create cube vertex buffer");
        return;
    }
    
    // Upload vertex data
    void* vb_mapped = vb->map();
    if (vb_mapped) {
        memcpy(vb_mapped, vertices.data(), vb_info.size);
        vb->unmap();
    } else {
        ERR(LogSkyboxPass, "Failed to map cube vertex buffer");
        return;
    }
    
    // Create index buffer
    RHIBufferInfo ib_info = {};
    ib_info.size = indices.size() * sizeof(uint32_t);
    ib_info.stride = sizeof(uint32_t);
    ib_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    ib_info.type = RESOURCE_TYPE_INDEX_BUFFER;
    ib_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    
    RHIBufferRef ib = backend->create_buffer(ib_info);
    if (!ib) {
        ERR(LogSkyboxPass, "Failed to create cube index buffer");
        return;
    }
    
    // Upload index data
    void* ib_mapped = ib->map();
    if (ib_mapped) {
        memcpy(ib_mapped, indices.data(), ib_info.size);
        ib->unmap();
    } else {
        ERR(LogSkyboxPass, "Failed to map cube index buffer");
        return;
    }
    
    // Create Mesh and set data
    cube_mesh_ = std::make_shared<Mesh>();
    cube_mesh_->set_data(vertices, indices);
    
    INFO(LogSkyboxPass, "Procedural cube mesh created successfully ({} vertices, {} indices)", 
         vertices.size(), indices.size());
}

void SkyboxPass::build(RDGBuilder& builder) {
    // Empty default implementation
}

void SkyboxPass::build(RDGBuilder& builder, RDGTextureHandle color_target,
                       RDGTextureHandle depth_target, const Mat4& view, const Mat4& proj,
                       const std::vector<SkyboxComponent*>& skyboxes) {
    if (!initialized_ || !pipeline_) {
        WARN(LogSkyboxPass, "SkyboxPass not initialized");
        return;
    }
    
    if (skyboxes.empty()) {
        return;
    }
    
    if (!cube_mesh_ || !cube_mesh_->get_vertex_buffer() || !cube_mesh_->get_index_buffer()) {
        WARN(LogSkyboxPass, "No valid cube mesh for skybox rendering");
        return;
    }
    
    // Per-frame log removed to avoid stutter from log buffer flush
    
    // Prepare per-frame data (remove translation from view)
    per_frame_data_.view = view;
    per_frame_data_.proj = proj;
    // Translation removal is done in shader
    per_frame_dirty_ = true;
    
    // Get vertex/index buffers
    auto vertex_buffer = cube_mesh_->get_vertex_buffer()->position_buffer_;
    auto index_buffer = cube_mesh_->get_index_buffer()->buffer_;
    uint32_t index_count = cube_mesh_->get_index_count();
    
    if (!vertex_buffer || !index_buffer) {
        WARN(LogSkyboxPass, "Invalid mesh buffers for skybox");
        return;
    }
    
    // Build render pass for each skybox
    int skybox_index = 0;
    for (auto* skybox : skyboxes) {
        skybox_index++;
        if (!skybox) {
            WARN(LogSkyboxPass, "Skybox {} is null", skybox_index);
            continue;
        }
        
        auto material = skybox->get_material();
        if (!material) {
            WARN(LogSkyboxPass, "Skybox {} has no material", skybox_index);
            continue;
        }
        

        
        // Ensure cube texture is ready
        if (!material->ensure_cube_texture_ready()) {
            WARN(LogSkyboxPass, "Skybox {} cube texture not ready", skybox_index);
            continue;
        }
        

        
        auto cube_texture = material->get_cube_texture();
        if (!cube_texture) {
            WARN(LogSkyboxPass, "Skybox {} cube_texture wrapper is null", skybox_index);
            continue;
        }
        if (!cube_texture->texture_) {
            WARN(LogSkyboxPass, "Skybox {} cube_texture->texture_ (RHI) is null", skybox_index);
            continue;
        }

        
        // Get intensity from material
        float intensity = material->get_intensity();
        
        // Build skybox at camera position with specified scale
        float scale = skybox->get_skybox_scale();
        Mat4 model = Mat4::Identity();
        model.m[0][0] = scale;
        model.m[1][1] = scale;
        model.m[2][2] = scale;
        
        // Store data for lambda capture
        float captured_intensity = intensity;
        Mat4 captured_model = model;
        RHITextureRef captured_cube_tex = cube_texture->texture_;
        
        // Create render pass
        builder.create_render_pass("SkyboxPass")
            .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
            .depth_stencil(depth_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_DONT_CARE, 
                          1.0f, 0, {}, true)  // Read-only depth
            .execute([this, captured_model, captured_intensity, captured_cube_tex,
                     vertex_buffer, index_buffer, index_count](RDGPassContext context) {
                RHICommandListRef cmd = context.command;
                if (!cmd) {
                    WARN(LogSkyboxPass, "Execute lambda: command is null");
                    return;
                }
                
                cmd->set_graphics_pipeline(pipeline_);
                
                // Update and bind per-frame buffer
                if (per_frame_buffer_) {
                    if (per_frame_dirty_) {
                        void* mapped = per_frame_buffer_->map();
                        if (mapped) {
                            memcpy(mapped, &per_frame_data_, sizeof(PerFrameData));
                            per_frame_buffer_->unmap();
                        }
                        per_frame_dirty_ = false;
                    }
                    cmd->bind_constant_buffer(per_frame_buffer_, 0,
                        static_cast<ShaderFrequency>(SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
                }
                
                // Update and bind per-object buffer
                if (per_object_buffer_) {
                    PerObjectData obj_data;
                    obj_data.model = captured_model;
                    obj_data.inv_model = captured_model.inverse();
                    
                    void* mapped = per_object_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &obj_data, sizeof(PerObjectData));
                        per_object_buffer_->unmap();
                    }
                    cmd->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
                }
                
                // Update and bind params buffer
                if (params_buffer_) {
                    SkyboxParams params;
                    params.intensity = captured_intensity;
                    
                    void* mapped = params_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &params, sizeof(SkyboxParams));
                        params_buffer_->unmap();
                    }
                    cmd->bind_constant_buffer(params_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
                }

                  
                
                // Bind cube texture and sampler
                cmd->bind_texture(captured_cube_tex, 0, SHADER_FREQUENCY_FRAGMENT);
                cmd->bind_sampler(cube_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);
                
                // Bind vertex and index buffers
                cmd->bind_vertex_buffer(vertex_buffer, 0, 0);
                cmd->bind_index_buffer(index_buffer, 0);
                
                // Draw the cube
                cmd->draw_indexed(index_count, 1, 0, 0, 0);
            })
            .finish();
    }
}

} // namespace render
