#include "engine/function/render/render_resource/panorama_converter.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/core/log/Log.h"
#include <cstring>

DEFINE_LOG_TAG(LogPanoramaConverter, "PanoramaConverter");

// Push constants for compute shader
struct PushConstants {
    uint32_t face_index;
    uint32_t resolution;
    float padding[2];
};

PanoramaConverter::PanoramaConverter() = default;

PanoramaConverter::~PanoramaConverter() {
    cleanup();
}

void PanoramaConverter::cleanup() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (panorama_sampler_) panorama_sampler_->destroy();
    for (auto& buf : params_buffers_) if (buf) buf->destroy();
    params_buffers_.clear();
    
    pipeline_ = nullptr;
    root_signature_ = nullptr;
    panorama_sampler_ = nullptr;
    
    initialized_ = false;
}

bool PanoramaConverter::init() {
    if (initialized_) return true;
    
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogPanoramaConverter, "RHI backend not available");
        return false;
    }
    
    // Create compute shader
    if (!create_shaders()) {
        ERR(LogPanoramaConverter, "Failed to create shaders");
        return false;
    }
    
    // Create pipeline
    if (!create_pipeline()) {
        ERR(LogPanoramaConverter, "Failed to create pipeline");
        return false;
    }
    
    // Create sampler
    RHISamplerInfo sampler_info = {};
    sampler_info.min_filter = FILTER_TYPE_LINEAR;
    sampler_info.mag_filter = FILTER_TYPE_LINEAR;
    sampler_info.mipmap_mode = MIPMAP_MODE_LINEAR;
    sampler_info.address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = ADDRESS_MODE_CLAMP_TO_EDGE;
    panorama_sampler_ = backend->create_sampler(sampler_info);

    // Create 6 uniform buffers for each face's push constants replacement
    for (int i = 0; i < 6; i++) {
        RHIBufferInfo cb_info = {};
        cb_info.size = sizeof(PushConstants);
        cb_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
        cb_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
        params_buffers_.push_back(backend->create_buffer(cb_info));
    }
    
    initialized_ = true;
    INFO(LogPanoramaConverter, "PanoramaConverter initialized successfully");
    return true;
}

bool PanoramaConverter::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return false;
    
    // Compile compute shader from source
    const char* cs_source = R"(
        RWTexture2DArray<float4> output_cubemap : register(u0);
        Texture2D<float4> panorama_texture : register(t0);
        SamplerState panorama_sampler : register(s0);
        
        cbuffer PushConstantsCB : register(b0) {
            uint face_index;
            uint resolution;
        };
        
        float3 get_cubemap_direction(uint face, float2 uv) {
            float2 pos = uv * 2.0 - 1.0;
            float3 dir;
            switch (face) {
                case 0: dir = float3(1.0, -pos.y, -pos.x); break;
                case 1: dir = float3(-1.0, -pos.y, pos.x); break;
                case 2: dir = float3(pos.x, 1.0, pos.y); break;
                case 3: dir = float3(pos.x, -1.0, -pos.y); break;
                case 4: dir = float3(pos.x, -pos.y, 1.0); break;
                case 5: dir = float3(-pos.x, -pos.y, -1.0); break;
                default: dir = float3(0.0, 0.0, 1.0); break;
            }
            return normalize(dir);
        }
        
        float2 direction_to_equirect_uv(float3 dir) {
            float phi = atan2(dir.z, dir.x);
            float theta = asin(clamp(dir.y, -1.0, 1.0));
            float u = phi / (2.0 * 3.14159265359) + 0.5;
            float v = theta / 3.14159265359 + 0.5;
            return float2(u, v);
        }
        
        [numthreads(8, 8, 1)]
        void CSMain(uint3 dispatch_id : SV_DispatchThreadID) {
            uint2 xy = dispatch_id.xy;
            if (xy.x >= resolution || xy.y >= resolution) return;
            
            float2 uv = (float2(xy) + 0.5) / float2(resolution, resolution);
            float3 dir = get_cubemap_direction(face_index, uv);
            float2 panorama_uv = direction_to_equirect_uv(dir);
            float4 color = panorama_texture.SampleLevel(panorama_sampler, panorama_uv, 0);
            output_cubemap[int3(xy, face_index)] = color;
        }
    )";
    
    auto cs_code = backend->compile_shader(cs_source, "CSMain", "cs_5_0");
    if (cs_code.empty()) {
        ERR(LogPanoramaConverter, "Failed to compile compute shader");
        return false;
    }
    
    RHIShaderInfo cs_info = {};
    cs_info.entry = "CSMain";
    cs_info.frequency = SHADER_FREQUENCY_COMPUTE;
    cs_info.code = std::move(cs_code);
    
    compute_shader_ = backend->create_shader(cs_info);
    if (!compute_shader_) {
        ERR(LogPanoramaConverter, "Failed to create compute shader");
        return false;
    }
    
    return true;
}

bool PanoramaConverter::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogPanoramaConverter, "Cannot create pipeline: backend is null");
        return false;
    }
    if (!compute_shader_) {
        ERR(LogPanoramaConverter, "Cannot create pipeline: compute shader is null");
        return false;
    }
    
    // Create root signature with push constants
    RHIRootSignatureInfo root_info = {};
    PushConstantInfo pc_info = {};
    pc_info.size = sizeof(PushConstants);
    pc_info.frequency = SHADER_FREQUENCY_COMPUTE;
    root_info.add_push_constant(pc_info);
    
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) {
        ERR(LogPanoramaConverter, "Failed to create root signature");
        return false;
    }
    
    // Create compute pipeline
    RHIComputePipelineInfo pipe_info = {};
    pipe_info.compute_shader = compute_shader_;
    pipe_info.root_signature = root_signature_;
    
    pipeline_ = backend->create_compute_pipeline(pipe_info);
    if (!pipeline_) {
        ERR(LogPanoramaConverter, "Failed to create compute pipeline");
        return false;
    }
    
    INFO(LogPanoramaConverter, "Compute pipeline created successfully");
    return true;
}

TextureRef PanoramaConverter::convert(TextureRef panorama, uint32_t resolution) {
    if (!initialized_ || !panorama) {
        return nullptr;
    }
    
    if (panorama->get_texture_type() != TextureType::Texture2D) {
        ERR(LogPanoramaConverter, "Input must be a 2D panorama texture");
        return nullptr;
    }
    
    auto backend = EngineContext::rhi();
    if (!backend) return nullptr;
    
    // Create output cubemap texture with UAV flag for compute shader
    Extent3D extent = { resolution, resolution, 1 };
    
    RHITextureInfo cubemap_info = {};
    cubemap_info.format = FORMAT_R16G16B16A16_SFLOAT;
    cubemap_info.extent = extent;
    cubemap_info.array_layers = 6;
    cubemap_info.mip_levels = 1;
    cubemap_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    cubemap_info.type = RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE;
    
    RHITextureRef cubemap_texture = backend->create_texture(cubemap_info);
    if (!cubemap_texture) {
        ERR(LogPanoramaConverter, "Failed to create cubemap texture");
        return nullptr;
    }
    
    // Create a command pool and context for synchronous execution
    RHICommandPoolRef pool = backend->create_command_pool({nullptr});
    if (!pool) {
        ERR(LogPanoramaConverter, "Failed to create command pool");
        return nullptr;
    }
    
    RHICommandContextRef ctx = backend->create_command_context(pool);
    if (!ctx) {
        ERR(LogPanoramaConverter, "Failed to create command context");
        return nullptr;
    }
    
    INFO(LogPanoramaConverter, "Starting panorama to cubemap conversion ({}x{})...", resolution, resolution);
    
    ctx->begin_command();
    
    // Execute compute shader for each face
    for (uint32_t face = 0; face < 6; face++) {
        // Transition resources (barriers)
        RHITextureBarrier panorama_barrier = {
            .texture = panorama->texture_,
            .src_state = RESOURCE_STATE_SHADER_RESOURCE,
            .dst_state = RESOURCE_STATE_SHADER_RESOURCE,
            .subresource = {TEXTURE_ASPECT_COLOR, 0, 1, 0, 1}
        };
        ctx->texture_barrier(panorama_barrier);
        
        RHITextureBarrier cubemap_barrier = {
            .texture = cubemap_texture,
            .src_state = RESOURCE_STATE_UNORDERED_ACCESS,
            .dst_state = RESOURCE_STATE_UNORDERED_ACCESS,
            .subresource = {TEXTURE_ASPECT_COLOR, 0, 1, face, 1}
        };
        ctx->texture_barrier(cubemap_barrier);
        
        // Set pipeline and bind resources
        ctx->set_compute_pipeline(pipeline_);
        ctx->bind_sampler(panorama_sampler_, 0, SHADER_FREQUENCY_COMPUTE);
        ctx->bind_texture(panorama->texture_, 0, SHADER_FREQUENCY_COMPUTE);
        ctx->bind_rw_texture(cubemap_texture, 0, 0, SHADER_FREQUENCY_COMPUTE);
        
        // Push constants (Using Constant Buffer to support DX11)
        PushConstants pc = { face, resolution, {0, 0} };
        if (params_buffers_.size() > face && params_buffers_[face]) {
            void* mapped = params_buffers_[face]->map();
            if (mapped) {
                memcpy(mapped, &pc, sizeof(pc));
                params_buffers_[face]->unmap();
            }
            ctx->bind_constant_buffer(params_buffers_[face], 0, SHADER_FREQUENCY_COMPUTE);
        } else {
            ctx->push_constants(&pc, sizeof(pc), SHADER_FREQUENCY_COMPUTE); // Fallback
        }
        
        // Dispatch compute shader
        uint32_t groups_x = (resolution + 7) / 8;
        uint32_t groups_y = (resolution + 7) / 8;
        ctx->dispatch(groups_x, groups_y, 1);
        
        INFO(LogPanoramaConverter, "Dispatched face {} ({}x{} thread groups)", face, groups_x, groups_y);
    }
    
    ctx->end_command();
    
    // Flush commands and wait for GPU
    auto fence = backend->create_fence(false);
    ctx->execute(fence, nullptr, nullptr);
    fence->wait();
    
    INFO(LogPanoramaConverter, "Compute conversion completed and synced with GPU");
    
    // Create Texture wrapper using skip-init constructor to avoid double resource creation
    auto cubemap = std::make_shared<Texture>(
        Texture::SkipInitTag,
        TextureType::TextureCube,
        FORMAT_R16G16B16A16_SFLOAT,
        extent,
        6,
        1
    );
    cubemap->texture_ = cubemap_texture;
    
    // ★ 设置 debug name，方便在 RenderDoc 中识别
    std::string debug_name = "SkyboxCubemap_" + std::to_string(resolution) + "x" + std::to_string(resolution);
    cubemap->set_name(debug_name);
    backend->set_name(cubemap_texture, debug_name + "_RHI");
    
    // Create texture view
    RHITextureViewInfo view_info = {};
    view_info.texture = cubemap_texture;
    view_info.format = FORMAT_R16G16B16A16_SFLOAT;
    view_info.view_type = VIEW_TYPE_CUBE;
    view_info.subresource = { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 };
    cubemap->texture_view_ = backend->create_texture_view(view_info);
    
    INFO(LogPanoramaConverter, "Successfully converted panorama to {}x{} cubemap (name: {})", 
         resolution, resolution, debug_name);
    return cubemap;
}
