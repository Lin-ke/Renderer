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

#include <cstring>

DEFINE_LOG_TAG(LogDeferredLighting, "DeferredLighting");

namespace render {

// Deferred lighting vertex shader - full screen quad
static const char* s_deferred_lighting_vs_src = R"(
struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float2 positions[4] = {
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2( 1.0,  1.0),
    float2(-1.0,  1.0)
};

static const float2 uvs[4] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0)
};

VSOutput main(uint vertex_id : SV_VertexID) {
    VSOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}
)";

// Deferred lighting fragment shader with PBR BRDF
static const char* s_deferred_lighting_fs_src = R"(
struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// G-Buffer textures
Texture2D g_albedo_ao : register(t0);
Texture2D g_normal_roughness : register(t1);
Texture2D g_material : register(t2);
Texture2D g_position_depth : register(t3);

SamplerState g_sampler : register(s0);

// Per-frame data
cbuffer PerFrame : register(b0) {
    float3 camera_pos;
    float _padding0;
    
    uint light_count;
    float _padding1[3];
    
    float3 main_light_dir;
    float _padding2;
    float3 main_light_color;
    float main_light_intensity;
    
    float4x4 inv_view_proj;
};

// Light structure
struct Light {
    float3 position;
    float _padding0;
    
    float3 color;
    float intensity;
    
    float3 direction;
    float range;
    
    uint type;
    float inner_angle;
    float outer_angle;
    float _padding1;
};

// PBR Constants
static const float PI = 3.14159265359;
static const float MIN_ROUGHNESS = 0.001;
static const float MAX_ROUGHNESS = 0.999;

// GGX Normal Distribution Function
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Geometry Function - Schlick approximation
float G_Schlick(float NoV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

// Smith Geometry Function
float G_Smith(float NoV, float NoL, float roughness) {
    return G_Schlick(NoV, roughness) * G_Schlick(NoL, roughness);
}

// Schlick Fresnel approximation
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel with roughness modification (for diffuse)
float3 F_Schlick_Roughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Lambertian diffuse
float3 Diffuse_Lambert(float3 albedo) {
    return albedo / PI;
}

// PBR BRDF calculation
float3 PBR_BRDF(float3 albedo, float roughness, float metallic, float3 N, float3 V, float3 L, out float3 F) {
    float3 H = normalize(V + L);
    
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    
    // Fresnel reflectance at normal incidence
    float3 F0 = lerp(0.04, albedo, metallic);
    
    // Fresnel
    F = F_Schlick(VoH, F0);
    
    // Normal Distribution
    float D = D_GGX(NoH, roughness);
    
    // Geometry
    float G = G_Smith(NoV, NoL, roughness);
    
    // Specular
    float3 numerator = D * G * F;
    float denominator = 4.0 * NoV * NoL + 0.0001;
    float3 specular = numerator / denominator;
    
    // Diffuse (energy conservation)
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * Diffuse_Lambert(albedo);
    
    return diffuse + specular;
}

// Directional light calculation
float3 CalcDirectionalLight(float3 albedo, float roughness, float metallic, float3 N, float3 V, float3 lightDir, float3 lightColor, float lightIntensity) {
    float3 L = normalize(-lightDir);
    float NoL = saturate(dot(N, L));
    
    float3 F;
    float3 brdf = PBR_BRDF(albedo, roughness, metallic, N, V, L, F);
    
    float3 radiance = lightColor * lightIntensity;
    return brdf * radiance * NoL;
}

// Point light calculation
float3 CalcPointLight(float3 albedo, float roughness, float metallic, float3 worldPos, float3 N, float3 V, Light light) {
    float3 L = light.position - worldPos;
    float dist = length(L);
    L = normalize(L);
    
    // Attenuation
    float attenuation = 1.0;
    if (light.range > 0.0) {
        attenuation = saturate(1.0 - (dist / light.range));
        attenuation *= attenuation;
    }
    
    float NoL = saturate(dot(N, L));
    
    float3 F;
    float3 brdf = PBR_BRDF(albedo, roughness, metallic, N, V, L, F);
    
    float3 radiance = light.color * light.intensity * attenuation;
    return brdf * radiance * NoL;
}

float4 main(PSInput input) : SV_TARGET {
    // Sample G-Buffer
    float4 albedo_ao = g_albedo_ao.Sample(g_sampler, input.uv);
    float4 normal_roughness = g_normal_roughness.Sample(g_sampler, input.uv);
    float4 material = g_material.Sample(g_sampler, input.uv);
    float4 position_depth = g_position_depth.Sample(g_sampler, input.uv);
    
    // Unpack G-Buffer data
    float3 albedo = albedo_ao.rgb;
    float ao = albedo_ao.a;
    
    float3 N = normalize(normal_roughness.rgb * 2.0 - 1.0);
    float roughness = clamp(normal_roughness.a, MIN_ROUGHNESS, MAX_ROUGHNESS);
    
    float metallic = material.r;
    float emission = material.g;
    float specular = material.b;
    
    float3 worldPos = position_depth.rgb;
    
    // Early out if no geometry
    if (position_depth.a == 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    
    // View direction
    float3 V = normalize(camera_pos - worldPos);
    
    // Ambient occlusion
    ao = 1.0;  //####TODO####: Use actual AO from G-Buffer
    
    // PBR Lighting
    float3 Lo = float3(0.0, 0.0, 0.0);
    
    // Main directional light
    if (main_light_intensity > 0.0) {
        Lo += CalcDirectionalLight(albedo, roughness, metallic, N, V, 
                                    main_light_dir, main_light_color, main_light_intensity);
    }
    
    //####TODO####: Additional point lights from SSBO
    
    // Ambient lighting (simplified IBL)
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = F_Schlick_Roughness(saturate(dot(N, V)), F0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    
    float3 irradiance = 0.03;  // Simplified ambient
    float3 diffuse = irradiance * albedo;
    float3 ambient = (kD * diffuse) * ao;
    
    // Emission
    float3 emission_color = albedo * emission;
    
    // Final color
    float3 color = ambient + Lo + emission_color;
    
    // HDR tone mapping (Reinhard)
    color = color / (color + 1.0);
    
    // Gamma correction
    color = pow(color, 1.0 / 2.2);
    
    return float4(color, 1.0);
}
)";

static std::vector<uint8_t> compile_shader_rhi(const char* source, const char* entry, 
                                                const char* profile) {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogDeferredLighting, "RHI backend not available for shader compilation");
        return {};
    }
    return backend->compile_shader(source, entry, profile);
}

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
    
    // Vertex shader
    auto vs_code = compile_shader_rhi(s_deferred_lighting_vs_src, "main", "vs_5_0");
    if (vs_code.empty()) return;
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "main";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogDeferredLighting, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Fragment shader
    auto fs_code = compile_shader_rhi(s_deferred_lighting_fs_src, "main", "ps_5_0");
    if (fs_code.empty()) return;
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "main";
    fs_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    fs_info.code = fs_code;
    
    auto fs = backend->create_shader(fs_info);
    if (!fs) {
        ERR(LogDeferredLighting, "Failed to create fragment shader");
        return;
    }
    
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = fs;
    
    INFO(LogDeferredLighting, "Shaders created successfully");
}

void DeferredLightingPass::create_quad_geometry() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Full-screen quad vertices (position + uv)
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f
    };
    
    uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
    
    // Create vertex buffer
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
    
    // Upload vertex data
    void* mapped = quad_vertex_buffer_->map();
    if (mapped) {
        memcpy(mapped, vertices, sizeof(vertices));
        quad_vertex_buffer_->unmap();
    }
    
    // Create index buffer
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
    
    // Upload index data
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
    
    // Get extent
    Extent2D extent = swapchain->get_extent();
    
    // Update per-frame data from camera and lights
    auto mesh_manager = render_system->get_mesh_manager();
    if (mesh_manager) {
        auto* camera = mesh_manager->get_active_camera();
        if (camera) {
            // Get light data
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
    
    // Import G-Buffer textures (these would be created by GBufferPass)
    // For now, we'll just output to the swapchain
    //####TODO####: Properly read G-Buffer textures from previous pass
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return;
    
    RDGTextureHandle color_target = builder.create_texture("DeferredLighting_Output")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
    
    // Create lighting pass
    builder.create_render_pass("DeferredLighting_Pass")
        .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
        .execute([this, extent](RDGPassContext context) {
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
            
            //####TODO####: Bind G-Buffer textures
            
            // Draw full-screen quad using vertex ID (no vertex buffer needed)
            cmd->draw(3, 1, 0, 0);
        })
        .finish();
}

} // namespace render
