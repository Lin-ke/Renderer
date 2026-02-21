#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class RHICommandList;
using RHICommandListRef = std::shared_ptr<RHICommandList>;

class RHIResource : public std::enable_shared_from_this<RHIResource> {
public:
    RHIResource() = delete;
    RHIResource(RHIResourceType resource_type) : resource_type_(resource_type){};
    virtual ~RHIResource(){};

    inline RHIResourceType get_type() { return resource_type_; }

    virtual void* raw_handle() { return nullptr; };

    virtual void destroy(){}; // Called when destroyed

    inline const std::string& get_name() const { return name_; }
    inline void set_name(const std::string& name) { name_ = name; }

private:
    RHIResourceType resource_type_;
    std::string name_ = "";
    uint32_t last_use_tick_ = 0; // Last used frame

    friend class RHIBackend;
};

// Basic Resources

class RHIQueue : public RHIResource {
public:
    RHIQueue(const RHIQueueInfo& info) : RHIResource(RHI_QUEUE), info_(info) {}

    virtual void wait_idle() = 0;

protected:
    RHIQueueInfo info_;
};

class RHISurface : public RHIResource {
public:
    RHISurface() : RHIResource(RHI_SURFACE){};

    inline Extent2D get_extent() const { return extent_; }

protected:
    Extent2D extent_;
};

class RHISwapchain : public RHIResource {
public:
    RHISwapchain(const RHISwapchainInfo& info) : RHIResource(RHI_SWAPCHAIN), info_(info) {}

    virtual uint32_t get_current_frame_index() = 0;
    virtual RHITextureRef get_texture(uint32_t index) = 0;
    virtual RHITextureRef get_new_frame(RHIFenceRef fence, RHISemaphoreRef signal_semaphore) = 0;
    virtual void present(RHISemaphoreRef wait_semaphore) = 0;
    
    Extent2D get_extent() const { return info_.extent; }

protected:
    RHISwapchainInfo info_;
};

class RHICommandPool : public RHIResource {
public:
    RHICommandPool(const RHICommandPoolInfo& info) : RHIResource(RHI_COMMAND_POOL), info_(info) {}

    virtual RHICommandListRef create_command_list(bool bypass = true);

    void return_to_pool(RHICommandContextRef command_context) {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_contexts_.push(command_context);
    }

protected:
    RHICommandPoolInfo info_;

    std::queue<RHICommandContextRef> idle_contexts_ = {};
    std::vector<RHICommandContextRef> contexts_ = {};
    std::mutex mutex_;

    friend class RHICommandList;
};

// Buffers, Textures, Shaders, Acceleration Structures

class RHIBuffer : public RHIResource {
public:
    RHIBuffer(const RHIBufferInfo& info) : RHIResource(RHI_BUFFER), info_(info) {}

    virtual bool init() { return true; }

    virtual void* map() = 0;
    virtual void unmap() = 0;

    inline const RHIBufferInfo& get_info() const { return info_; }

protected:
    RHIBufferInfo info_;
};

class RHITextureView : public RHIResource {
public:
    RHITextureView(const RHITextureViewInfo& info) : RHIResource(RHI_TEXTURE_VIEW), info_(info) {}

    inline const RHITextureViewInfo& get_info() const { return info_; }

protected:
    RHITextureViewInfo info_;
};

class RHITexture : public RHIResource {
public:
    RHITexture(const RHITextureInfo& info) : RHIResource(RHI_TEXTURE), info_(info) {}

    virtual bool init() { return true; }

    virtual Extent3D mip_extent(uint32_t mip_level);

    inline const TextureSubresourceRange& get_default_subresource_range() const { return default_range_; }
    inline const TextureSubresourceLayers& get_default_subresource_layers() const { return default_layers_; }

    inline const RHITextureInfo& get_info() const { return info_; }

protected:
    RHITextureInfo info_;

    TextureSubresourceRange default_range_ = {};
    TextureSubresourceLayers default_layers_ = {};
};

class RHISampler : public RHIResource {
public:
    RHISampler(const RHISamplerInfo& info) : RHIResource(RHI_SAMPLER), info_(info) {}

    virtual bool init() { return true; }

    inline const RHISamplerInfo& get_info() const { return info_; }

protected:
    RHISamplerInfo info_;
};

class RHIShader : public RHIResource {
public:
    RHIShader(const RHIShaderInfo& info) : RHIResource(RHI_SHADER), info_(info) { frequency_ = info.frequency; }

    virtual bool init() { return true; }

    ShaderFrequency get_frequency() const { return frequency_; }
    const ShaderReflectInfo& get_reflect_info() const { return reflect_info_; }
    const RHIShaderInfo& get_info() const { return info_; }

private:
    ShaderFrequency frequency_;

protected:
    RHIShaderInfo info_;
    ShaderReflectInfo reflect_info_;
};

class RHIShaderBindingTable : public RHIResource {
public:
    RHIShaderBindingTable(const RHIShaderBindingTableInfo& info) : RHIResource(RHI_SHADER_BINDING_TABLE), info_(info) {}

    const RHIShaderBindingTableInfo& get_info() const { return info_; }

protected:
    RHIShaderBindingTableInfo info_;
};

class RHITopLevelAccelerationStructure : public RHIResource {
public:
    RHITopLevelAccelerationStructure(const RHITopLevelAccelerationStructureInfo& info)
        : RHIResource(RHI_TOP_LEVEL_ACCELERATION_STRUCTURE), info_(info) {}

    virtual void update(const std::vector<RHIAccelerationStructureInstanceInfo>& instance_infos) = 0;

    const RHITopLevelAccelerationStructureInfo& get_info() const { return info_; }

protected:
    RHITopLevelAccelerationStructureInfo info_;
};

class RHIBottomLevelAccelerationStructure : public RHIResource {
public:
    RHIBottomLevelAccelerationStructure(const RHIBottomLevelAccelerationStructureInfo& info)
        : RHIResource(RHI_BOTTOM_LEVEL_ACCELERATION_STRUCTURE), info_(info) {}

    const RHIBottomLevelAccelerationStructureInfo& get_info() const { return info_; }

protected:
    RHIBottomLevelAccelerationStructureInfo info_;
};

// Root Signature, Descriptors

class RHIRootSignature : public RHIResource {
public:
    RHIRootSignature(const RHIRootSignatureInfo& info) : RHIResource(RHI_ROOT_SIGNATURE), info_(info) {}

    virtual bool init() { return true; }

    virtual RHIDescriptorSetRef create_descriptor_set(uint32_t set) = 0;

    const RHIRootSignatureInfo& get_info() { return info_; }

protected:
    RHIRootSignatureInfo info_;
};

class RHIDescriptorSet : public RHIResource {
public:
    RHIDescriptorSet() : RHIResource(RHI_DESCRIPTOR_SET) {}

    virtual RHIDescriptorSet& update_descriptor(const RHIDescriptorUpdateInfo& descriptor_update_info) = 0;

    RHIDescriptorSet& update_descriptors(const std::vector<RHIDescriptorUpdateInfo>& descriptor_update_infos) {
        for (auto& info : descriptor_update_infos)
            update_descriptor(info);
        return *this;
    };
};

// Pipeline State

class RHIRenderPass : public RHIResource {
public:
    RHIRenderPass(const RHIRenderPassInfo& info) : RHIResource(RHI_RENDER_PASS), info_(info) {}

    virtual bool init() { return true; }

    const RHIRenderPassInfo& get_info() { return info_; }

protected:
    RHIRenderPassInfo info_;
};

class RHIGraphicsPipeline : public RHIResource {
public:
    RHIGraphicsPipeline(const RHIGraphicsPipelineInfo& info) : RHIResource(RHI_GRAPHICS_PIPELINE), info_(info) {}

    virtual bool init() { return true; }

    const RHIGraphicsPipelineInfo& get_info() { return info_; }

protected:
    RHIGraphicsPipelineInfo info_;
};

class RHIComputePipeline : public RHIResource {
public:
    RHIComputePipeline(const RHIComputePipelineInfo& info) : RHIResource(RHI_COMPUTE_PIPELINE), info_(info) {}

protected:
    RHIComputePipelineInfo info_;
};

class RHIRayTracingPipeline : public RHIResource {
public:
    RHIRayTracingPipeline(const RHIRayTracingPipelineInfo& info) : RHIResource(RHI_RAY_TRACING_PIPELINE), info_(info) {}

protected:
    RHIRayTracingPipelineInfo info_;
};

// Synchronization

class RHIFence : public RHIResource {
public:
    RHIFence() : RHIResource(RHI_FENCE) {}

    virtual bool init() { return true; }

    virtual void wait() = 0;
};

class RHISemaphore : public RHIResource {
public:
    RHISemaphore() : RHIResource(RHI_SEMAPHORE) {}
};
