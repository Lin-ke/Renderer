#include "engine/function/render/graph/rdg_pool.h"
#include "engine/core/log/Log.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/rhi/rhi_structs.h"

DEFINE_LOG_TAG(LogRDG, "RDG");

RDGBufferPool::PooledBuffer RDGBufferPool::allocate(const RHIBufferInfo& info) {
    RDGBufferPool::PooledBuffer ret;

    auto& buffers = pooled_buffers_[info];
    for (auto iter = buffers.begin(); iter != buffers.end(); iter++) {
        if (iter->buffer->get_info().size >= info.size) {
            ret = *iter;
            buffers.erase(iter);
            pooled_size_--;
            return ret;
        }
    }

    //####TODO####: DEBUG only - INFO(LogRDG, "RHIBuffer not found in cache, creating new.");
    ret = {.buffer = EngineContext::rhi()->create_buffer(info), .state = RESOURCE_STATE_UNDEFINED};
    allocated_size_++;

    return ret;
}

void RDGBufferPool::release(const RDGBufferPool::PooledBuffer& pooled_buffer) {
    pooled_buffers_[pooled_buffer.buffer->get_info()].push_back(pooled_buffer);
    pooled_size_++;
}

RDGTexturePool::PooledTexture RDGTexturePool::allocate(const RHITextureInfo& info) {
    RDGTexturePool::PooledTexture ret;

    RHITextureInfo temp_info = info;
    if (temp_info.mip_levels == 0) temp_info.mip_levels = temp_info.extent.mip_size();

    auto& textures = pooled_textures_[temp_info];
    for (auto iter = textures.begin(); iter != textures.end(); iter++) {
        ret = *iter;
        textures.erase(iter);
        pooled_size_--;
        return ret;
    }

    //####TODO####: DEBUG only - INFO(LogRDG, "RHITexture not found in cache, creating new.");
    ret = {
        .texture = EngineContext::rhi()->create_texture(temp_info),
        .state = RESOURCE_STATE_UNDEFINED,
    };
    allocated_size_++;

    return ret;
}

void RDGTexturePool::release(const RDGTexturePool::PooledTexture& pooled_texture) {
    pooled_textures_[pooled_texture.texture->get_info()].push_back(pooled_texture);
    pooled_size_++;
}

RDGTextureViewPool::PooledTextureView RDGTextureViewPool::allocate(const RHITextureViewInfo& info) {
    RHITextureViewInfo actual_info = info;
    if (actual_info.subresource.aspect == TEXTURE_ASPECT_NONE)
        actual_info.subresource = actual_info.texture->get_default_subresource_range();

    RDGTextureViewPool::PooledTextureView ret;

    auto& texture_views = pooled_texture_views_[actual_info];
    for (auto iter = texture_views.begin(); iter != texture_views.end(); iter++) {
        ret = *iter;
        texture_views.erase(iter);
        pooled_size_--;
        return ret;
    }

    //####TODO####: DEBUG only - INFO(LogRDG, "RHITextureView not found in cache, creating new.");
    ret = {.texture_view = EngineContext::rhi()->create_texture_view(actual_info)};
    allocated_size_++;

    return ret;
}

void RDGTextureViewPool::release(const RDGTextureViewPool::PooledTextureView& pooled_texture_view) {
    pooled_texture_views_[pooled_texture_view.texture_view->get_info()].push_back(pooled_texture_view);
    pooled_size_++;
}

RDGDescriptorSetPool::PooledDescriptor RDGDescriptorSetPool::allocate(const RHIRootSignatureRef& root_signature, uint32_t set) {
    RDGDescriptorSetPool::PooledDescriptor ret;

    auto& descriptors = pooled_descriptors_[{root_signature->get_info(), set}];
    for (auto iter = descriptors.begin(); iter != descriptors.end(); iter++) {
        ret = *iter;
        descriptors.erase(iter);
        pooled_size_--;
        return ret;
    }

    //####TODO####: DEBUG only - INFO(LogRDG, "RHIDescriptorSet not found in cache, creating new.");
    ret = {.descriptor = root_signature->create_descriptor_set(set)};
    allocated_size_++;

    return ret;
}

void RDGDescriptorSetPool::release(const RDGDescriptorSetPool::PooledDescriptor& pooled_descriptor,
                                   const RHIRootSignatureRef& root_signature, uint32_t set) {
    pooled_descriptors_[{root_signature->get_info(), set}].push_back(pooled_descriptor);
    pooled_size_++;
}
