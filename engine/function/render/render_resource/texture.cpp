#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "engine/function/render/render_resource/texture.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"
#include <cassert>
#include <fstream>
#include <filesystem>

DECLARE_LOG_TAG(LogRenderResource);
DEFINE_LOG_TAG(LogRenderResource, "RenderResource");

TextureViewType texture_type_to_view_type(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return VIEW_TYPE_2D;
        case TextureType::Texture2DArray: return VIEW_TYPE_2D_ARRAY;
        case TextureType::TextureCube: return VIEW_TYPE_CUBE;
        case TextureType::Texture3D: return VIEW_TYPE_3D;
        default: 
            assert(false && "Invalid TextureType");
            return VIEW_TYPE_2D;
    }
}

void Texture::set_name(const std::string& name) {
    name_ = name;
    if (texture_ && EngineContext::rhi()) {
        EngineContext::rhi()->set_name(texture_, name);
    }
}

Texture::Texture(const std::string& virtual_path)
    : texture_type_(TextureType::Texture2D), format_(FORMAT_R8G8B8A8_SRGB), array_layer_(1) {
    paths_.push_back(virtual_path);
    name_ = std::filesystem::path(virtual_path).filename().string();
    set_uid(UID::from_hash(virtual_path));
    load_from_file();
}

Texture::Texture(const std::vector<std::string>& paths, TextureType type)
    : texture_type_(type), format_(FORMAT_R8G8B8A8_SRGB), array_layer_((uint32_t)paths.size()) {
    paths_ = paths;
    if (!paths.empty()) {
        name_ = std::filesystem::path(paths[0]).filename().string();
    }
    load_from_file();
}

Texture::Texture(TextureType type, RHIFormat format, Extent3D extent, uint32_t array_layer, uint32_t mip_levels)
    : texture_type_(type), format_(format), extent_(extent), array_layer_(array_layer) {
    mip_levels_ = mip_levels == 0 ? extent.mip_size() : mip_levels;
    init_rhi();
}

Texture::Texture(SkipInit, TextureType type, RHIFormat format, Extent3D extent, uint32_t array_layer, uint32_t mip_levels)
    : texture_type_(type), format_(format), extent_(extent), array_layer_(array_layer) {
    mip_levels_ = mip_levels == 0 ? extent.mip_size() : mip_levels;
    // Skip init_rhi() - caller is responsible for setting texture_ and texture_view_
}

Texture::~Texture() {
}

void Texture::on_load() {
    if (paths_.size() > 0) {
        load_from_file();
    } else {
        init_rhi();
    }
}

void Texture::on_save() {
    // Ensure paths_ contains virtual paths for portability
    ensure_virtual_paths();
}

void Texture::ensure_virtual_paths() {
    if (!EngineContext::asset()) {
        return;
    }
    
    for (auto& path : paths_) {
        // If path is already a virtual path, skip
        if (EngineContext::asset()->is_virtual_path(path)) {
            continue;
        }
        
        // Try to convert physical path to virtual path
        auto virtual_path_opt = EngineContext::asset()->get_virtual_path(path);
        if (virtual_path_opt) {
            path = virtual_path_opt->string();
        }
        // If conversion fails, keep the original path (will likely fail to load on other machines)
    }
}

void Texture::init_rhi() {
    if (!EngineContext::rhi()) {
        return;
    }
    ResourceType resource_type = (texture_type_ == TextureType::TextureCube) ? 
        (RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_TEXTURE) : RESOURCE_TYPE_TEXTURE;
    
    if (is_rw_format(format_)) {
        resource_type |= RESOURCE_TYPE_RW_TEXTURE;
    }
    
    if (is_depth_format(format_)) {
        resource_type |= RESOURCE_TYPE_DEPTH_STENCIL;
    }

    TextureAspectFlags aspects = TEXTURE_ASPECT_COLOR;
    if (is_depth_stencil_format(format_)) {
        aspects = TEXTURE_ASPECT_DEPTH_STENCIL;
    } else if (is_depth_format(format_)) {
        aspects = TEXTURE_ASPECT_DEPTH;
    } else if (is_stencil_format(format_)) {
        aspects = TEXTURE_ASPECT_STENCIL;
    }

    RHITextureInfo info = {};
    info.format = format_;
    info.extent = extent_;
    info.array_layers = array_layer_;
    info.mip_levels = mip_levels_;
    info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    info.type = resource_type;
    info.creation_flag = TEXTURE_CREATION_NONE;
    
    texture_ = EngineContext::rhi()->create_texture(info);
    if (!texture_) {
        return;
    }
    
    if (!name_.empty()) {
        EngineContext::rhi()->set_name(texture_, name_);
    }

    RHITextureViewInfo view_info = {};
    view_info.texture = texture_;
    view_info.format = format_;
    view_info.view_type = texture_type_to_view_type(texture_type_);
    view_info.subresource = { aspects, 0, mip_levels_, 0, array_layer_ };
    
    texture_view_ = EngineContext::rhi()->create_texture_view(view_info);

    // Register with RenderResourceManager for bindless ID if available
    if (EngineContext::render_resource()) {
        BindlessResourceInfo res_info = {};
        res_info.texture_view = texture_view_;
        texture_id_ = EngineContext::render_resource()->allocate_bindless_id(res_info, BINDLESS_SLOT_TEXTURE_2D);
        INFO(LogRenderResource, "Allocated texture ID: {} for {}", texture_id_, name_);
    }
}

void Texture::load_from_file() {
    if (texture_type_ == TextureType::TextureCube && paths_.size() != 6) {
        ERR(LogRenderResource, "Wrong file num with texture type cube!");
        return;
    }
    if (texture_type_ == TextureType::Texture3D) {
        ERR(LogRenderResource, "3D texture file is not supported for now!");
        return;
    }

    bool rhi_initialized = false;
    std::vector<RHIBufferRef> staging_buffers;
    
    auto immediate_command = EngineContext::rhi()->get_immediate_command();
    
    for (uint32_t i = 0; i < (uint32_t)paths_.size(); ++i) {
        std::string physical_path = paths_[i];
        if (EngineContext::asset()) {
            auto path_opt = EngineContext::asset()->get_physical_path(paths_[i]);
            if (path_opt) {
                physical_path = path_opt->string();
            }
        }

        std::ifstream file(physical_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ERR(LogRenderResource, "Failed to open texture file: {}", physical_path);
            continue;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            ERR(LogRenderResource, "Failed to read texture file: {}", physical_path);
            continue;
        }

        int width, height, channels;
        stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)buffer.data(), (int)size, &width, &height, &channels, 4);
        if (!pixels) {
            ERR(LogRenderResource, "Failed to load image from memory: {}", physical_path);
            continue;
        }

        if (!rhi_initialized) {
            extent_ = {(uint32_t)width, (uint32_t)height, 1};
            mip_levels_ = extent_.mip_size();
            INFO(LogRenderResource, "Texture init: {}x{}, mip_levels={}", width, height, mip_levels_);
            init_rhi();
            rhi_initialized = true;
        }

        if (EngineContext::rhi()) {
            uint32_t row_pitch = width * 4;
            uint32_t aligned_row_pitch = (row_pitch + 255) & ~255;
            uint32_t total_size = aligned_row_pitch * height;
            
            RHIBufferInfo staging_info = {};
            staging_info.size = total_size;
            staging_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
            staging_info.type = RESOURCE_TYPE_BUFFER;
            staging_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;

            RHIBufferRef staging_buffer = EngineContext::rhi()->create_buffer(staging_info);
            void* mapped = staging_buffer->map();
            
            if (aligned_row_pitch == row_pitch) {
                memcpy(mapped, pixels, total_size);
            } else {
                uint8_t* dst = static_cast<uint8_t*>(mapped);
                uint8_t* src = pixels;
                for (int y = 0; y < height; ++y) {
                    memcpy(dst + y * aligned_row_pitch, src + y * row_pitch, row_pitch);
                }
            }
            
            staging_buffer->unmap();
            
            immediate_command->texture_barrier({
                texture_,
                RESOURCE_STATE_UNDEFINED,
                RESOURCE_STATE_TRANSFER_DST,
                {TEXTURE_ASPECT_COLOR, 0, mip_levels_, i, 1}
            });

            immediate_command->copy_buffer_to_texture(
                staging_buffer,
                0,
                texture_,
                {TEXTURE_ASPECT_COLOR, 0, i, 1}
            );
            
            staging_buffers.push_back(staging_buffer);
        }

        stbi_image_free(pixels);
    }

    if (rhi_initialized && EngineContext::rhi()) {
        immediate_command->texture_barrier({
            texture_,
            RESOURCE_STATE_TRANSFER_DST,
            RESOURCE_STATE_TRANSFER_SRC,
            {TEXTURE_ASPECT_COLOR, 0, mip_levels_, 0, array_layer_}
        });

        immediate_command->generate_mips(texture_);

        immediate_command->texture_barrier({
            texture_,
            RESOURCE_STATE_TRANSFER_SRC,
            RESOURCE_STATE_SHADER_RESOURCE,
            {TEXTURE_ASPECT_COLOR, 0, mip_levels_, 0, array_layer_}
        });

        immediate_command->flush();
        staging_buffers.clear();
    }
}

void Texture::set_data(void* data, uint32_t size) {
    if (!EngineContext::rhi()) {
        return;
    }
    RHIBufferInfo staging_info = {};
    staging_info.size = size;
    staging_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
    staging_info.type = RESOURCE_TYPE_BUFFER;
    staging_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;

    RHIBufferRef staging_buffer = EngineContext::rhi()->create_buffer(staging_info);
    void* mapped_data = staging_buffer->map();
    memcpy(mapped_data, data, size);
    staging_buffer->unmap();

    auto immediate_command = EngineContext::rhi()->get_immediate_command();
    
    immediate_command->texture_barrier({
        texture_,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_TRANSFER_DST,
        {TEXTURE_ASPECT_COLOR, 0, mip_levels_, 0, array_layer_}
    });

    immediate_command->copy_buffer_to_texture(
        staging_buffer,
        0,
        texture_,
        {TEXTURE_ASPECT_COLOR, 0, 0, array_layer_}
    );

    immediate_command->texture_barrier({
        texture_,
        RESOURCE_STATE_TRANSFER_DST,
        RESOURCE_STATE_SHADER_RESOURCE,
        {TEXTURE_ASPECT_COLOR, 0, mip_levels_, 0, array_layer_}
    });

    immediate_command->flush();
}

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

CEREAL_REGISTER_TYPE(Texture);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Texture);
