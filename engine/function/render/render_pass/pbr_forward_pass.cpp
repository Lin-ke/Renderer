#include "engine/function/render/render_pass/pbr_forward_pass.h"
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
#include "engine/core/log/Log.h"

#include <cstring>

DEFINE_LOG_TAG(LogPBRForwardPass, "PBRForwardPass");

namespace render {

// PBR Vertex shader source (HLSL)
static const char* s_pbr_vertex_shader_src = R"(
cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
    
    float3 light_dir;
    float _padding2;
    float3 light_color;
    float light_intensity;
    
    float4 point_light_pos[4];
    float4 point_light_color[4];
    int point_light_count;
    float3 _padding3;
};

cbuffer PerObject : register(b1) {
    float4x4 model;
    float4x4 inv_model;
};

struct VSInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float4 world_tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
    float3 view_dir : TEXCOORD1;
};

VSOutput main(VSInput input) {
    VSOutput output;
    
    // Use default values if tangent/texcoord are not provided (zero)
    float4 tangent_val = input.tangent;
    if (length(tangent_val.xyz) < 0.001) {
        // Generate a default tangent perpendicular to normal
        float3 up_vec = abs(input.normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t = normalize(cross(up_vec, input.normal));
        tangent_val = float4(t, 1.0);
    }
    
    // Transform position to world space
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;
    
    // Transform to clip space
    float4 view_pos = mul(view, world_pos);
    output.position = mul(proj, view_pos);
    
    // Transform normal to world space (using inverse transpose)
    float3 world_normal = mul((float3x3)inv_model, input.normal);
    output.world_normal = normalize(world_normal);
    
    // Transform tangent to world space
    float3 world_tangent = mul((float3x3)model, tangent_val.xyz);
    output.world_tangent = float4(normalize(world_tangent), tangent_val.w);
    
    // Pass through texcoord (0,0 is fine for non-textured meshes)
    output.texcoord = input.texcoord;
    
    // View direction for specular
    output.view_dir = normalize(camera_pos - world_pos.xyz);
    
    return output;
}
)";

// PBR Fragment shader source (HLSL) - Based on ToyRenderer BRDF implementation
static const char* s_pbr_fragment_shader_src = R"(
cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float3 camera_pos;
    float _padding;
    
    float3 light_dir;
    float _padding2;
    float3 light_color;
    float light_intensity;
    
    float4 point_light_pos[4];
    float4 point_light_color[4];
    int point_light_count;
    float3 _padding3;
};

cbuffer Material : register(b2) {
    float4 albedo;
    float4 emission;
    float roughness;
    float metallic;
    float alpha_cutoff;
    int use_albedo_map;
    int use_normal_map;
    int use_arm_map;
    int use_emission_map;
    float2 _padding_mat;
};

Texture2D albedo_map : register(t0);
Texture2D normal_map : register(t1);
Texture2D arm_map : register(t2);    // AO/Roughness/Metallic
Texture2D emission_map : register(t3);

SamplerState default_sampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 world_normal : NORMAL0;
    float4 world_tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
    float3 view_dir : TEXCOORD1;
};

// Constants
static const float PI = 3.14159265359;

// PBR Functions (from ToyRenderer brdf.glsl)

// Fresnel Schlick
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Fresnel Schlick with roughness
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX Distribution
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

// Geometry Schlick GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Geometry Smith
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Lambert diffuse
float3 DiffuseLambert(float3 diffuseColor) {
    return diffuseColor * (1.0 / PI);
}

// Calculate tangent space normal
float3 GetNormalFromMap(PSInput input) {
    float3 tangent_normal = normal_map.Sample(default_sampler, input.texcoord).xyz * 2.0 - 1.0;
    
    float3 N = normalize(input.world_normal);
    float3 T = normalize(input.world_tangent.xyz);
    float3 B = cross(N, T) * input.world_tangent.w;
    
    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(TBN, tangent_normal));
}

// PBR BRDF calculation
float3 CalculateBRDF(float3 albedo_color, float roughness_val, float metallic_val, 
                      float3 N, float3 V, float3 L, float3 radiance) {
    float3 H = normalize(V + L);
    
    // Calculate dot products
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // F0 for dielectrics and metals
    float3 F0 = lerp((0.04).xxx, albedo_color, metallic_val);
    
    // Cook-Torrance BRDF
    float D = DistributionGGX(N, H, roughness_val);
    float G = GeometrySmith(N, V, L, roughness_val);
    float3 F = FresnelSchlick(VdotH, F0);
    
    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular = numerator / denominator;
    
    // Energy conservation
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic_val);
    
    // Diffuse
    float3 diffuse = DiffuseLambert(albedo_color);
    
    // Final color
    return (kD * diffuse + specular) * radiance * NdotL;
}

// Point light falloff
float PointLightFalloff(float dist, float radius) {
    return pow(saturate(1.0 - pow(dist / radius, 4)), 2) / (dist * dist + 1.0);
}

float4 main(PSInput input) : SV_TARGET {
    // Get base color
    float3 albedo_color = albedo.rgb;
    if (use_albedo_map) {
        float4 sampled = albedo_map.Sample(default_sampler, input.texcoord);
        albedo_color *= sampled.rgb;
        // Alpha test
        if (alpha_cutoff > 0.0 && sampled.a < alpha_cutoff) {
            discard;
        }
    }
    
    // Get material properties
    float roughness_val = roughness;
    float metallic_val = metallic;
    float ao = 1.0;
    
    if (use_arm_map) {
        float3 arm = arm_map.Sample(default_sampler, input.texcoord).rgb;
        ao = arm.r;
        roughness_val *= arm.g;
        metallic_val *= arm.b;
    }
    
    // Get normal
    float3 N = normalize(input.world_normal);
    if (use_normal_map) {
        N = GetNormalFromMap(input);
    }
    
    float3 V = normalize(input.view_dir);
    
    // Start with emission
    float3 final_color = emission.rgb;
    if (use_emission_map) {
        final_color *= emission_map.Sample(default_sampler, input.texcoord).rgb;
    }
    
    // Directional light
    {
        float3 L = normalize(-light_dir);
        float3 radiance = light_color * light_intensity;
        final_color += CalculateBRDF(albedo_color, roughness_val, metallic_val, N, V, L, radiance);
    }
    
    // Point lights
    for (int i = 0; i < point_light_count; i++) {
        float3 light_pos = point_light_pos[i].xyz;
        float light_range = point_light_pos[i].w;
        float3 light_col = point_light_color[i].rgb;
        float light_int = point_light_color[i].a;
        
        float3 L = normalize(light_pos - input.world_pos);
        float distance = length(light_pos - input.world_pos);
        
        float attenuation = PointLightFalloff(distance, light_range);
        float3 radiance = light_col * light_int * attenuation;
        
        final_color += CalculateBRDF(albedo_color, roughness_val, metallic_val, N, V, L, radiance);
    }
    
    // Ambient (simple ambient term)
    float3 ambient = 0.03 * albedo_color * ao;
    final_color += ambient;
    
    // Tone mapping (simple Reinhard)
    final_color = final_color / (final_color + 1.0);
    
    // Gamma correction
    final_color = pow(final_color, 1.0 / 2.2);
    
    return float4(final_color, 1.0);
}
)";

static std::vector<uint8_t> compile_shader_rhi(const char* source, const char* entry, 
                                                  const char* profile) {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogPBRForwardPass, "RHI backend not available for shader compilation");
        return {};
    }
    return backend->compile_shader(source, entry, profile);
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
    
    // Compile vertex shader
    auto vs_code = compile_shader_rhi(s_pbr_vertex_shader_src, "main", "vs_5_0");
    if (vs_code.empty()) return;
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "main";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    
    auto vs = backend->create_shader(vs_info);
    if (!vs) {
        ERR(LogPBRForwardPass, "Failed to create vertex shader");
        return;
    }
    
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = vs;
    
    // Compile fragment shader
    auto fs_code = compile_shader_rhi(s_pbr_fragment_shader_src, "main", "ps_5_0");
    if (fs_code.empty()) return;
    
    RHIShaderInfo fs_info = {};
    fs_info.entry = "main";
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
    
    // Create default tangent buffer (all zeros = will trigger default generation in shader)
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
    
    INFO(LogPBRForwardPass, "Default vertex buffers created");
}

void PBRForwardPass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;
    
    // Create root signature
    RHIRootSignatureInfo root_info = {};
    root_signature_ = backend->create_root_signature(root_info);
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
    pipe_info.vertex_input_state.vertex_elements[0].attribute_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;
    // Normal - stream 1
    pipe_info.vertex_input_state.vertex_elements[1].stream_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].attribute_index = 1;
    pipe_info.vertex_input_state.vertex_elements[1].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[1].offset = 0;
    // Tangent - stream 2
    pipe_info.vertex_input_state.vertex_elements[2].stream_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].attribute_index = 2;
    pipe_info.vertex_input_state.vertex_elements[2].format = FORMAT_R32G32B32A32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[2].offset = 0;
    // TexCoord - stream 3
    pipe_info.vertex_input_state.vertex_elements[3].stream_index = 3;
    pipe_info.vertex_input_state.vertex_elements[3].attribute_index = 3;
    pipe_info.vertex_input_state.vertex_elements[3].format = FORMAT_R32G32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[3].offset = 0;
    
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;  // Disable culling like ForwardPass
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;
    
    // Disable depth testing - RDG render pass doesn't have depth attachment bound
    // (This matches ForwardPass behavior; depth testing would require depth buffer in RDG)
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
    solid_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!solid_pipeline_) {
        ERR(LogPBRForwardPass, "Failed to create solid graphics pipeline");
        return;
    }
    
    // Create wireframe pipeline
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_WIREFRAME;
    wireframe_pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!wireframe_pipeline_) {
        ERR(LogPBRForwardPass, "Failed to create wireframe graphics pipeline");
        return;
    }
    
    // Start with solid pipeline
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
    if (!initialized_ || !pipeline_ || !cmd) return;

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

    // Bind common resources
    cmd->set_graphics_pipeline(pipeline_);
    cmd->bind_constant_buffer(material_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
    cmd->bind_sampler(default_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);

    // Update per-object buffer
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

    // Update material buffer
    if (material_buffer_ && batch.material) {
        PBRMaterialData mat_data;
        mat_data.albedo = batch.material->get_diffuse();
        mat_data.emission = batch.material->get_emission();
        mat_data.roughness = batch.material->get_roughness();
        mat_data.metallic = batch.material->get_metallic();
        mat_data.alpha_cutoff = batch.material->get_alpha_clip();
        mat_data.use_albedo_map = batch.material->get_diffuse_texture() ? 1 : 0;
        mat_data.use_normal_map = batch.material->get_normal_texture() ? 1 : 0;
        mat_data.use_arm_map = batch.material->get_arm_texture() ? 1 : 0;
        mat_data.use_emission_map = 0; // TODO
        
        void* mapped = material_buffer_->map();
        if (mapped) {
            memcpy(mapped, &mat_data, sizeof(mat_data));
            material_buffer_->unmap();
        }
    }

    // Bind textures
    if (batch.material) {
        auto albedo_tex = batch.material->get_diffuse_texture();
        if (albedo_tex && albedo_tex->texture_) {
            cmd->bind_texture(albedo_tex->texture_, 0, SHADER_FREQUENCY_FRAGMENT);
        }
        
        auto normal_tex = batch.material->get_normal_texture();
        if (normal_tex && normal_tex->texture_) {
            cmd->bind_texture(normal_tex->texture_, 1, SHADER_FREQUENCY_FRAGMENT);
        }
        
        auto arm_tex = batch.material->get_arm_texture();
        if (arm_tex && arm_tex->texture_) {
            cmd->bind_texture(arm_tex->texture_, 2, SHADER_FREQUENCY_FRAGMENT);
        }
    }

    // Bind vertex buffers - use defaults if not provided
    if (batch.vertex_buffer) {
        cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
    }
    if (batch.normal_buffer) {
        cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
    }
    // Always bind tangent buffer (use default if mesh doesn't have one)
    if (batch.tangent_buffer) {
        cmd->bind_vertex_buffer(batch.tangent_buffer, 2, 0);
    } else if (default_tangent_buffer_) {
        cmd->bind_vertex_buffer(default_tangent_buffer_, 2, 0);
    }
    // Always bind texcoord buffer (use default if mesh doesn't have one)
    if (batch.texcoord_buffer) {
        cmd->bind_vertex_buffer(batch.texcoord_buffer, 3, 0);
    } else if (default_texcoord_buffer_) {
        cmd->bind_vertex_buffer(default_texcoord_buffer_, 3, 0);
    }

    // Draw
    if (batch.index_buffer) {
        cmd->bind_index_buffer(batch.index_buffer, 0);
        cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
    }
}

void PBRForwardPass::build(RDGBuilder& builder) {
    if (!initialized_ || !pipeline_) {
        return;
    }
    
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;
    
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return;
    
    // Update per-frame data from camera and lights
    auto mesh_manager = render_system->get_mesh_manager();
    if (mesh_manager) {
        auto* camera = mesh_manager->get_active_camera();
        if (camera) {
            // Default light values
            Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);
            Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
            float light_intensity = 1.0f;
            
            // Get directional light from world
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
    
    // Import swapchain texture
    RDGTextureHandle color_target = builder.create_texture("PBRForwardPass_Color")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();
    
    // Create render pass
    builder.create_render_pass("PBRForwardPass_Main")
        .color(0, color_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 
               Color4{0.1f, 0.1f, 0.2f, 1.0f})
        .execute([this, render_system](RDGPassContext context) {
            auto mesh_manager = render_system->get_mesh_manager();
            if (!mesh_manager) return;
            
            RHICommandListRef cmd = context.command;
            if (!cmd) return;
            
            // Set viewport and scissor
            Extent2D extent = render_system->get_swapchain()->get_extent();
            cmd->set_viewport({0, 0}, {extent.width, extent.height});
            cmd->set_scissor({0, 0}, {extent.width, extent.height});
            cmd->set_graphics_pipeline(pipeline_);
            
            // Update and bind per-frame buffer
            if (per_frame_buffer_ && per_frame_dirty_) {
                void* mapped = per_frame_buffer_->map();
                if (mapped) {
                    memcpy(mapped, &per_frame_data_, sizeof(per_frame_data_));
                    per_frame_buffer_->unmap();
                }
                per_frame_dirty_ = false;
            }
            cmd->bind_constant_buffer(per_frame_buffer_, 0, 
                static_cast<ShaderFrequency>(SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
            
            // Bind material buffer (will be updated per material)
            cmd->bind_constant_buffer(material_buffer_, 2, SHADER_FREQUENCY_FRAGMENT);
            
            // Bind default sampler
            cmd->bind_sampler(default_sampler_, 0, SHADER_FREQUENCY_FRAGMENT);
            
            // Get batches and draw
            std::vector<DrawBatch> batches;
            mesh_manager->collect_draw_batches(batches);
            
            for (const auto& batch : batches) {
                // Update per-object buffer
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
                
                // Update material buffer
                if (material_buffer_ && batch.material) {
                    PBRMaterialData mat_data;
                    mat_data.albedo = batch.material->get_diffuse();
                    mat_data.emission = batch.material->get_emission();
                    mat_data.roughness = batch.material->get_roughness();
                    mat_data.metallic = batch.material->get_metallic();
                    mat_data.alpha_cutoff = batch.material->get_alpha_clip();
                    mat_data.use_albedo_map = batch.material->get_diffuse_texture() ? 1 : 0;
                    mat_data.use_normal_map = batch.material->get_normal_texture() ? 1 : 0;
                    mat_data.use_arm_map = batch.material->get_arm_texture() ? 1 : 0;
                    mat_data.use_emission_map = 0; // TODO
                    
                    void* mapped = material_buffer_->map();
                    if (mapped) {
                        memcpy(mapped, &mat_data, sizeof(mat_data));
                        material_buffer_->unmap();
                    }
                }
                
                // Bind textures
                if (batch.material) {
                    auto albedo_tex = batch.material->get_diffuse_texture();
                    if (albedo_tex && albedo_tex->texture_) {
                        cmd->bind_texture(albedo_tex->texture_, 0, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    auto normal_tex = batch.material->get_normal_texture();
                    if (normal_tex && normal_tex->texture_) {
                        cmd->bind_texture(normal_tex->texture_, 1, SHADER_FREQUENCY_FRAGMENT);
                    }
                    
                    auto arm_tex = batch.material->get_arm_texture();
                    if (arm_tex && arm_tex->texture_) {
                        cmd->bind_texture(arm_tex->texture_, 2, SHADER_FREQUENCY_FRAGMENT);
                    }
                }
                
                // Bind vertex buffers - use defaults if not provided
                if (batch.vertex_buffer) {
                    cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
                }
                if (batch.normal_buffer) {
                    cmd->bind_vertex_buffer(batch.normal_buffer, 1, 0);
                }
                // Always bind tangent buffer (use default if mesh doesn't have one)
                if (batch.tangent_buffer) {
                    cmd->bind_vertex_buffer(batch.tangent_buffer, 2, 0);
                } else if (default_tangent_buffer_) {
                    cmd->bind_vertex_buffer(default_tangent_buffer_, 2, 0);
                }
                // Always bind texcoord buffer (use default if mesh doesn't have one)
                if (batch.texcoord_buffer) {
                    cmd->bind_vertex_buffer(batch.texcoord_buffer, 3, 0);
                } else if (default_texcoord_buffer_) {
                    cmd->bind_vertex_buffer(default_texcoord_buffer_, 3, 0);
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

} // namespace render
