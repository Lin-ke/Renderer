#pragma once

#include "engine/configs.h"
#include "engine/core/math/math.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/function/render/render_resource/buffer.h"
#include "engine/function/render/render_resource/texture.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>

//####TODO####: Full bindless resource system is not implemented yet

struct BindlessResourceInfo {
    ResourceType resource_type = RESOURCE_TYPE_NONE;
    RHIBufferRef buffer;
    RHITextureViewRef texture_view;
    RHISamplerRef sampler;
    uint64_t buffer_offset = 0;
    uint64_t buffer_range = 0;
};

enum BindlessSlot {
    BINDLESS_SLOT_POSITION = 0,
    BINDLESS_SLOT_NORMAL,
    BINDLESS_SLOT_TANGENT,
    BINDLESS_SLOT_TEXCOORD,
    BINDLESS_SLOT_COLOR,
    BINDLESS_SLOT_BONE_INDEX,
    BINDLESS_SLOT_BONE_WEIGHT,
    BINDLESS_SLOT_ANIMATION,
    BINDLESS_SLOT_INDEX,
    BINDLESS_SLOT_SAMPLER,
    BINDLESS_SLOT_TEXTURE_2D,
    BINDLESS_SLOT_TEXTURE_CUBE,
    BINDLESS_SLOT_TEXTURE_3D,
    BINDLESS_SLOT_MAX_ENUM,
};

/**
 * @brief Simple index allocator for managing resource IDs
 */
class IndexAllocator {
public:
    IndexAllocator(uint32_t max_size = 65536) : max_size_(max_size), next_index_(1) {}

    uint32_t allocate() {
        if (!free_indices_.empty()) {
            uint32_t index = free_indices_.front();
            free_indices_.pop();
            return index;
        }
        assert(next_index_ < max_size_ && "IndexAllocator: out of indices");
        return next_index_++;
    }

    void release(uint32_t index) {
        if (index > 0 && index < next_index_) {
            free_indices_.push(index);
        }
    }

private:
    uint32_t max_size_;
    uint32_t next_index_;
    std::queue<uint32_t> free_indices_;
};

/**
 * @brief RenderResourceManager manages global rendering resources
 * 
 * This class provides:
 * - ID allocation for materials, objects, lights, etc.
 * - Global buffer management (per-frame and multi-frame resources)
 * - Bindless resource allocation (simplified)
 * - Shader caching
 */
class RenderResourceManager {
public:
    RenderResourceManager();
    ~RenderResourceManager();

    void init();
    void destroy();

    // Object ID allocation
    uint32_t allocate_object_id() { return object_id_allocator_.allocate(); }
    void release_object_id(uint32_t id) { object_id_allocator_.release(id); }

    // Material ID allocation
    uint32_t allocate_material_id() { return material_id_allocator_.allocate(); }
    void release_material_id(uint32_t id) { material_id_allocator_.release(id); }

    // Light ID allocation
    uint32_t allocate_point_light_id() { return point_light_id_allocator_.allocate(); }
    void release_point_light_id(uint32_t id) { point_light_id_allocator_.release(id); }

    // Bindless ID allocation (simplified)
    uint32_t allocate_bindless_id(const BindlessResourceInfo& resource_info, BindlessSlot slot);
    void release_bindless_id(uint32_t id, BindlessSlot slot);

    // Resource setters
    void set_camera_info(const CameraInfo& camera_info);
    void set_object_info(const ObjectInfo& object_info, uint32_t object_id);
    void set_material_info(const MaterialInfo& material_info, uint32_t material_id);
    void set_directional_light_info(const DirectionalLightInfo& light_info, uint32_t cascade);
    void set_point_light_info(const PointLightInfo& light_info, uint32_t light_id);
    void set_global_setting(const RenderGlobalSetting& setting);

    // Shader cache
    RHIShaderRef get_or_create_shader(const std::string& path, ShaderFrequency frequency, 
                                       const std::string& entry = "main");

    // Global texture access
    TextureRef get_depth_texture() { return depth_texture_; }
    TextureRef get_velocity_texture() { return velocity_texture_; }
    
    // Default texture fallbacks
    TextureRef get_default_black_texture() { return default_black_texture_; }
    TextureRef get_default_white_texture() { return default_white_texture_; }
    TextureRef get_default_normal_texture() { return default_normal_texture_; }

    // Buffer access
    RHIBufferRef get_per_frame_camera_buffer();
    RHIBufferRef get_per_frame_object_buffer();

    // FRAMES_IN_FLIGHT is defined in configs.h

private:
    void init_global_resources();
    void init_per_frame_resources();

    // ID allocators
    IndexAllocator object_id_allocator_;
    IndexAllocator material_id_allocator_;
    IndexAllocator point_light_id_allocator_;
    std::array<IndexAllocator, BINDLESS_SLOT_MAX_ENUM> bindless_id_allocators_;

    // Shader cache
    std::unordered_map<std::string, RHIShaderRef> shader_cache_;

    // Per-frame resources (double buffered)
    struct PerFrameResource {
        std::unique_ptr<Buffer<CameraInfo>> camera_buffer;
        std::shared_ptr<Buffer<ObjectInfo>> object_buffer;
        std::unique_ptr<Buffer<LightInfo>> light_buffer;
    };
    std::array<std::unique_ptr<PerFrameResource>, FRAMES_IN_FLIGHT> per_frame_resources_;

    // Multi-frame resources (persistent)
    std::unique_ptr<Buffer<RenderGlobalSetting>> global_setting_buffer_;
    std::shared_ptr<Buffer<MaterialInfo>> material_buffer_;

    // Global textures
    TextureRef depth_texture_;
    TextureRef velocity_texture_;
    TextureRef prev_depth_texture_;
    
    TextureRef default_black_texture_;
    TextureRef default_white_texture_;
    TextureRef default_normal_texture_;

    std::array<std::unordered_map<uint32_t, BindlessResourceInfo>, BINDLESS_SLOT_MAX_ENUM> bindless_resources_;

    bool initialized_ = false;
};

using RenderResourceManagerRef = std::shared_ptr<RenderResourceManager>;
