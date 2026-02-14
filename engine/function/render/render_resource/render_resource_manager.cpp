#include "engine/function/render/render_resource/render_resource_manager.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/data/render_structs.h"

#include <cstdint>
#include <fstream>

//####TODO####: Move these to config header
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

    // Clear per-frame resources
    for (auto& resource : per_frame_resources_) {
        resource.reset();
    }

    material_buffer_.reset();

    initialized_ = false;
    INFO(LogRenderResourceManager, "RenderResourceManager destroyed");
}

void RenderResourceManager::init_per_frame_resources() {
    INFO(LogRenderResourceManager, "Initializing per-frame resources...");

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        per_frame_resources_[i] = std::make_unique<PerFrameResource>();
        
        per_frame_resources_[i]->camera_buffer = std::make_unique<Buffer<CameraInfo>>(RESOURCE_TYPE_UNIFORM_BUFFER);
        per_frame_resources_[i]->light_buffer = std::make_unique<Buffer<LightInfo>>(RESOURCE_TYPE_UNIFORM_BUFFER);
        
        //####TODO####: Create large array buffers for object/light data
        // For now, skip buffer creation as single-element Buffer is too small
        // In the future, create proper array buffers or use dynamic sizing
    }

    INFO(LogRenderResourceManager, "Per-frame resources initialized");
}

void RenderResourceManager::init_global_resources() {
    INFO(LogRenderResourceManager, "Initializing global resources...");

    global_setting_buffer_ = std::make_unique<Buffer<RenderGlobalSetting>>(RESOURCE_TYPE_UNIFORM_BUFFER);

    //####TODO####: Create material buffer with proper array size
    // For now, skip large buffer creation - single Buffer<MaterialInfo> is insufficient
    // Need ArrayBuffer or dynamic buffer allocation

    //####TODO####: Create global textures when Texture class supports procedural creation
    // depth_texture_ = std::make_shared<Texture>(...);
    // velocity_texture_ = std::make_shared<Texture>(...);

    INFO(LogRenderResourceManager, "Global resources initialized");
}

uint32_t RenderResourceManager::allocate_bindless_id(const BindlessResourceInfo& resource_info, 
                                                      BindlessSlot slot) {
    if (slot >= BINDLESS_SLOT_MAX_ENUM) return 0;

    uint32_t id = bindless_id_allocators_[slot].allocate();
    if (id != 0) {
        bindless_resources_[slot][id] = resource_info;
        
        //####TODO####: Update descriptor set when bindless system is implemented
        // This would update the global descriptor set with the new resource
    }
    
    return id;
}

void RenderResourceManager::release_bindless_id(uint32_t id, BindlessSlot slot) {
    if (slot >= BINDLESS_SLOT_MAX_ENUM || id == 0) return;

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
    if (object_id >= MAX_PER_FRAME_OBJECT_SIZE) return;
    
    //####TODO####: Write to GPU buffer when array buffer system is implemented
    // For now, just store the data locally or skip
}

void RenderResourceManager::set_material_info(const MaterialInfo& material_info, uint32_t material_id) {
    if (material_id >= MAX_PER_FRAME_RESOURCE_SIZE) return;
    
    //####TODO####: Write to GPU buffer when array buffer system is implemented
    // For now, material data is stored in Material class and submitted per-draw
}

void RenderResourceManager::set_directional_light_info(const DirectionalLightInfo& light_info, 
                                                        uint32_t cascade) {
    if (cascade >= DIRECTIONAL_SHADOW_CASCADE_LEVEL) return;
    
    //####TODO####: Write to GPU buffer when light buffer system is implemented
}

void RenderResourceManager::set_point_light_info(const PointLightInfo& light_info, 
                                                  uint32_t light_id) {
    if (light_id >= MAX_POINT_LIGHT_COUNT) return;
    
    //####TODO####: Write to GPU buffer when light buffer system is implemented
}

void RenderResourceManager::set_global_setting(const RenderGlobalSetting& setting) {
    if (global_setting_buffer_) {
        global_setting_buffer_->set_data(setting);
    }
}

RHIShaderRef RenderResourceManager::get_or_create_shader(const std::string& path, 
                                                          ShaderFrequency frequency,
                                                          const std::string& entry) {
    // Use path + frequency + entry as unique key
    std::string key = path + "_" + std::to_string(frequency) + "_" + entry;
    
    auto iter = shader_cache_.find(key);
    if (iter != shader_cache_.end()) {
        return iter->second;
    }

    //####TODO####: Load shader binary from file when shader compilation pipeline is ready
    // For now, return nullptr as we don't have compiled shader binaries
    INFO(LogRenderResourceManager, "Shader not found in cache: {}", key);
    
    // Try to load from asset system
    try {
        std::string full_path = std::string(ENGINE_PATH) + "/" + path;
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
    } catch (...) {
        ERR(LogRenderResourceManager, "Failed to load shader: {}", path);
    }
    
    return nullptr;
}

RHIBufferRef RenderResourceManager::get_per_frame_camera_buffer() {
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    return per_frame_resources_[frame_index]->camera_buffer ? per_frame_resources_[frame_index]->camera_buffer->buffer_ : nullptr;
}

RHIBufferRef RenderResourceManager::get_per_frame_object_buffer() {
    uint32_t frame_index = EngineContext::current_frame_index() % FRAMES_IN_FLIGHT;
    return per_frame_resources_[frame_index]->object_buffer->buffer_;
}
