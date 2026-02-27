#include "engine/function/render/render_resource/render_resource_manager.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/core/utils/path_utils.h"

#include <cstdint>
#include <fstream>

// Helper function to create raw buffer for arrays
// Uses DYNAMIC memory for CPU write + SRV for GPU read (DX11 DYNAMIC cannot use UAV)
template<typename T>
static RHIBufferRef create_array_buffer(uint32_t count) {
    RHIBufferInfo info = {};
    info.size = sizeof(T) * count;
    info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;  // Dynamic, mappable
    // Use VERTEX_BUFFER flag to get a valid BindFlags (DYNAMIC requires at least one bind flag)
    // The buffer can still be used as raw buffer via byte address or structured buffer views
    info.type = RESOURCE_TYPE_VERTEX_BUFFER;  
    info.creation_flag = 0;
    return global_rhi_backend()->create_buffer(info);
}

#ifndef MAX_PER_FRAME_RESOURCE_SIZE
#define MAX_PER_FRAME_RESOURCE_SIZE 4096
#endif

#ifndef MAX_BINDLESS_RESOURCE_SIZE
#define MAX_BINDLESS_RESOURCE_SIZE 65536
#endif

DEFINE_LOG_TAG(LogRenderResourceManager, "RenderResourceManager");

RenderResourceManager::RenderResourceManager()
    : object_id_allocator_(MAX_PER_FRAME_OBJECT_SIZE),
      material_id_allocator_(MAX_PER_FRAME_RESOURCE_SIZE),
      point_light_id_allocator_(MAX_POINT_LIGHT_COUNT) {
    for (auto& allocator : bindless_id_allocators_) {
        allocator = IndexAllocator(MAX_BINDLESS_RESOURCE_SIZE);
    }
}

RenderResourceManager::~RenderResourceManager() {
    destroy();
}

void RenderResourceManager::init() {
    if (initialized_) return;

    INFO(LogRenderResourceManager, "Initializing RenderResourceManager...");

    init_per_frame_resources();
    init_global_resources();

    initialized_ = true;
    INFO(LogRenderResourceManager, "RenderResourceManager initialized successfully");
}

void RenderResourceManager::destroy() {
    if (!initialized_) return;

    INFO(LogRenderResourceManager, "Destroying RenderResourceManager...");

    // Clear shader cache
    shader_cache_.clear();

    // Clear bindless resources
    for (auto& resources : bindless_resources_) {
        resources.clear();
    }

    // Release textures
    depth_texture_.reset();
    velocity_texture_.reset();
    prev_depth_texture_.reset();
    
    // Release default fallback textures
    default_black_texture_.reset();
    default_white_texture_.reset();
    default_normal_texture_.reset();

    // Clear per-frame resources
    for (auto& resource : per_frame_resources_) {
        resource.reset();
    }

    material_buffer_rhi_.reset();

    initialized_ = false;
    INFO(LogRenderResourceManager, "RenderResourceManager destroyed");
}

void RenderResourceManager::init_per_frame_resources() {
    INFO(LogRenderResourceManager, "Initializing per-frame resources...");

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        per_frame_resources_[i] = std::make_unique<PerFrameResource>();
        
        per_frame_resources_[i]->camera_buffer = std::make_unique<Buffer<CameraInfo>>(RESOURCE_TYPE_UNIFORM_BUFFER);
        per_frame_resources_[i]->light_buffer = std::make_unique<Buffer<LightInfo>>(RESOURCE_TYPE_UNIFORM_BUFFER);
        // Create object buffer as raw buffer (array of ObjectInfo)
        per_frame_resources_[i]->object_buffer_rhi = create_array_buffer<ObjectInfo>(MAX_PER_FRAME_OBJECT_SIZE);
    }

    INFO(LogRenderResourceManager, "Per-frame resources initialized");
}

void RenderResourceManager::init_global_resources() {
    INFO(LogRenderResourceManager, "Initializing global resources...");

    global_setting_buffer_ = std::make_unique<Buffer<RenderGlobalSetting>>(RESOURCE_TYPE_UNIFORM_BUFFER);
    
    // Create material buffer for bindless material access (array of MaterialInfo)
    material_buffer_rhi_ = create_array_buffer<MaterialInfo>(MAX_PER_FRAME_RESOURCE_SIZE);

    default_black_texture_ = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_UNORM, Extent3D{1, 1, 1});
    if (default_black_texture_) {
        uint32_t black_pixel = 0xFF000000;
        default_black_texture_->set_data(&black_pixel, sizeof(black_pixel));
        default_black_texture_->set_name("Default_Black");
    }
    
    default_white_texture_ = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_UNORM, Extent3D{1, 1, 1});
    if (default_white_texture_) {
        uint32_t white_pixel = 0xFFFFFFFF;
        default_white_texture_->set_data(&white_pixel, sizeof(white_pixel));
        default_white_texture_->set_name("Default_White");
    }
    
    default_normal_texture_ = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_UNORM, Extent3D{1, 1, 1});
    if (default_normal_texture_) {
        uint32_t normal_pixel = 0xFFFF8080;
        default_normal_texture_->set_data(&normal_pixel, sizeof(normal_pixel));
        default_normal_texture_->set_name("Default_Normal");
    }

    INFO(LogRenderResourceManager, "Global resources initialized");
}

uint32_t RenderResourceManager::allocate_bindless_id(const BindlessResourceInfo& resource_info, 
                                                      BindlessSlot slot) {
    assert(slot < BINDLESS_SLOT_MAX_ENUM && "Invalid bindless slot");

    uint32_t id = bindless_id_allocators_[slot].allocate();
    if (id != 0) {
        bindless_resources_[slot][id] = resource_info;
    }
    
    return id;
}

void RenderResourceManager::release_bindless_id(uint32_t id, BindlessSlot slot) {
    assert(slot < BINDLESS_SLOT_MAX_ENUM && "Invalid bindless slot");
    if (id == 0) return;  // id == 0 is valid case (resource not allocated)

    bindless_resources_[slot].erase(id);
    bindless_id_allocators_[slot].release(id);
}

void RenderResourceManager::set_camera_info(const CameraInfo& camera_info) {
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    if (per_frame_resources_[frame_index]->camera_buffer) {
        per_frame_resources_[frame_index]->camera_buffer->set_data(camera_info);
    }
}

void RenderResourceManager::set_object_info(const ObjectInfo& object_info, uint32_t object_id) {
    assert(object_id < MAX_PER_FRAME_OBJECT_SIZE && "Object ID out of range");
    
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    if (per_frame_resources_[frame_index]->object_buffer_rhi) {
        void* mapped = per_frame_resources_[frame_index]->object_buffer_rhi->map();
        if (mapped) {
            memcpy((uint8_t*)mapped + object_id * sizeof(ObjectInfo), &object_info, sizeof(ObjectInfo));
            per_frame_resources_[frame_index]->object_buffer_rhi->unmap();
        }
    }
}

void RenderResourceManager::set_material_info(const MaterialInfo& material_info, uint32_t material_id) {
    assert(material_id < MAX_PER_FRAME_RESOURCE_SIZE && "Material ID out of range");
    
    if (material_buffer_rhi_) {
        void* mapped = material_buffer_rhi_->map();
        if (mapped) {
            memcpy((uint8_t*)mapped + material_id * sizeof(MaterialInfo), &material_info, sizeof(MaterialInfo));
            material_buffer_rhi_->unmap();
        }
    }
}

void RenderResourceManager::set_directional_light_info(const DirectionalLightInfo& light_info, 
                                                        uint32_t cascade) {
    assert(cascade < DIRECTIONAL_SHADOW_CASCADE_LEVEL && "Cascade index out of range");
    
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    if (per_frame_resources_[frame_index]->light_buffer) {
        uint32_t offset = cascade * sizeof(DirectionalLightInfo);
        per_frame_resources_[frame_index]->light_buffer->set_data(&light_info, sizeof(DirectionalLightInfo), offset);
    }
}

void RenderResourceManager::set_point_light_info(const PointLightInfo& light_info, 
                                                  uint32_t light_id) {
    assert(light_id < MAX_POINT_LIGHT_COUNT && "Point light ID out of range");
    
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    if (per_frame_resources_[frame_index]->light_buffer) {
        uint32_t offset = POINT_LIGHT_OFFSET + light_id * sizeof(PointLightInfo);
        per_frame_resources_[frame_index]->light_buffer->set_data(&light_info, sizeof(PointLightInfo), offset);
    }
}

void RenderResourceManager::set_global_setting(const RenderGlobalSetting& setting) {
    if (global_setting_buffer_) {
        global_setting_buffer_->set_data(setting);
    }
}

RHIShaderRef RenderResourceManager::get_or_create_shader(const std::string& path, 
                                                          ShaderFrequency frequency,
                                                          const std::string& entry) {
    std::string key = path + "_" + std::to_string(frequency) + "_" + entry;
    
    auto iter = shader_cache_.find(key);
    if (iter != shader_cache_.end()) {
        return iter->second;
    }

    INFO(LogRenderResourceManager, "Shader not found in cache: {}", key);
    
    // Try to load from asset system
    try {
        std::string full_path = (utils::get_engine_path() / path).string();
        std::ifstream file(full_path, std::ios::binary);
        if (file) {
            std::vector<uint8_t> code((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
            
            if (!code.empty()) {
                RHIShaderInfo info = {};
                info.entry = entry;
                info.frequency = frequency;
                info.code = code;
                
                RHIShaderRef shader = EngineContext::rhi()->create_shader(info);
                if (shader) {
                    shader_cache_[key] = shader;
                    INFO(LogRenderResourceManager, "Shader loaded and cached: {}", key);
                    return shader;
                }
            }
        }
    } catch (const std::exception& e) {
        ERR(LogRenderResourceManager, "Failed to load shader: {} - {}", path, e.what());
    } catch (...) {
        ERR(LogRenderResourceManager, "Failed to load shader: {} - unknown error", path);
    }
    
    return nullptr;
}

RHIBufferRef RenderResourceManager::get_per_frame_camera_buffer() {
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    return per_frame_resources_[frame_index]->camera_buffer ? per_frame_resources_[frame_index]->camera_buffer->buffer_ : nullptr;
}

RHIBufferRef RenderResourceManager::get_per_frame_object_buffer() {
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    return per_frame_resources_[frame_index]->object_buffer_rhi;
}
