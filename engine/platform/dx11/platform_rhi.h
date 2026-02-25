#pragma once

#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

class DX11Backend;

/**
 * @brief DX11 implementation of RHIQueue
 */
class DX11Queue : public RHIQueue {
public:
    DX11Queue(const RHIQueueInfo& info) : RHIQueue(info) {}

    virtual void wait_idle() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHISurface
 */
class DX11Surface : public RHISurface {
public:
    DX11Surface(HWND hwnd) : hwnd_(hwnd) {}

    HWND get_hwnd() const { return hwnd_; }

    virtual void destroy() override final {}

private:
    HWND hwnd_;
};

/**
 * @brief DX11 implementation of RHISwapchain
 */
class DX11Swapchain : public RHISwapchain {
public:
    DX11Swapchain(const RHISwapchainInfo& info, std::shared_ptr<DX11Backend> backend);

    virtual uint32_t get_current_frame_index() override final {
        if (supports_frame_index_query_ && swap_chain_) {
            return swap_chain_->GetCurrentBackBufferIndex();
        }
        return current_index_;
    }
    virtual RHITextureRef get_texture(uint32_t index) override final { 
        if (index >= textures_.size()) return nullptr;
        return textures_[index]; 
    }
    virtual RHITextureRef get_new_frame(RHIFenceRef fence, RHISemaphoreRef signal_semaphore) override final;
    virtual void present(RHISemaphoreRef wait_semaphore) override final;

    ID3D11RenderTargetView* get_back_buffer_rtv(uint32_t index) {
        if (index >= back_buffer_rtvs_.size()) return nullptr;
        return back_buffer_rtvs_[index].Get();
    }

    virtual void destroy() override final;
    virtual void* raw_handle() override final { return swap_chain_.Get(); }

private:
    ComPtr<IDXGISwapChain3> swap_chain_;
    std::vector<RHITextureRef> textures_;
    std::vector<ComPtr<ID3D11RenderTargetView>> back_buffer_rtvs_;
    std::weak_ptr<DX11Backend> backend_;
    uint32_t current_index_ = 0;
    bool supports_frame_index_query_ = false;
};

/**
 * @brief DX11 implementation of RHICommandPool
 */
class DX11CommandPool : public RHICommandPool {
public:
    DX11CommandPool(const RHICommandPoolInfo& info) : RHICommandPool(info) {}

    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIBuffer
 */
class DX11Buffer : public RHIBuffer {
public:
    DX11Buffer(const RHIBufferInfo& info, std::shared_ptr<DX11Backend> backend);

    virtual bool init() override final;
    virtual void* map() override final;
    virtual void unmap() override final;

    virtual void destroy() override final;
    virtual void* raw_handle() override final { return buffer_.Get(); }

    ComPtr<ID3D11Buffer> get_handle() const { return buffer_; }

private:
    ComPtr<ID3D11Buffer> buffer_;
    std::weak_ptr<DX11Backend> backend_;
    void* mapped_data_ = nullptr;
};

/**
 * @brief DX11 implementation of RHITexture
 */
class DX11Texture : public RHITexture {
public:
    DX11Texture(const RHITextureInfo& info, std::shared_ptr<DX11Backend> backend, ComPtr<ID3D11Texture2D> handle = nullptr);

    virtual bool init() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return texture_.Get(); }

    ComPtr<ID3D11Texture2D> get_handle() const { return texture_; }
    
    ComPtr<ID3D11ShaderResourceView> get_srv() const { return srv_; }
    ComPtr<ID3D11ShaderResourceView> create_srv();
    ComPtr<ID3D11RenderTargetView> create_rtv();

private:
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    std::weak_ptr<DX11Backend> backend_;
};

/**
 * @brief DX11 implementation of RHITextureView
 */
class DX11TextureView : public RHITextureView {
public:
    DX11TextureView(const RHITextureViewInfo& info, std::shared_ptr<DX11Backend> backend);

    virtual void destroy() override final;
    virtual void* raw_handle() override final { return srv_.Get(); }

    ComPtr<ID3D11ShaderResourceView> get_srv() const { return srv_; }
    ComPtr<ID3D11RenderTargetView> get_rtv() const { return rtv_; }
    ComPtr<ID3D11DepthStencilView> get_dsv() const { return dsv_; }
    ComPtr<ID3D11DepthStencilView> get_dsv_read_only() const { return dsv_read_only_; }
    ComPtr<ID3D11UnorderedAccessView> get_uav() const { return uav_; }

private:
    ComPtr<ID3D11ShaderResourceView> srv_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11DepthStencilView> dsv_read_only_;
    ComPtr<ID3D11UnorderedAccessView> uav_;
};

/**
 * @brief DX11 implementation of RHISampler
 */
class DX11Sampler : public RHISampler {
public:
    DX11Sampler(const RHISamplerInfo& info, std::shared_ptr<DX11Backend> backend);

    virtual bool init() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return sampler_state_.Get(); }

private:
    ComPtr<ID3D11SamplerState> sampler_state_;
    std::weak_ptr<DX11Backend> backend_;
};

/**
 * @brief DX11 implementation of RHIShader
 */
class DX11Shader : public RHIShader {
public:
    DX11Shader(const RHIShaderInfo& info, std::shared_ptr<DX11Backend> backend);

    virtual bool init() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return shader_resource_.Get(); }

    ComPtr<ID3D11DeviceChild> get_shader() const { return shader_resource_; }
    ComPtr<ID3DBlob> get_blob() const { return blob_; }

private:
    ComPtr<ID3D11DeviceChild> shader_resource_;
    ComPtr<ID3DBlob> blob_;
    std::weak_ptr<DX11Backend> backend_;
};

/**
 * @brief DX11 implementation of RHIShaderBindingTable
 */
class DX11ShaderBindingTable : public RHIShaderBindingTable {
public:
    DX11ShaderBindingTable(const RHIShaderBindingTableInfo& info, DX11Backend& backend) : RHIShaderBindingTable(info) {}
    virtual void destroy() override final {}
};

/**
 * @brief DX11 implementation of RHITopLevelAccelerationStructure
 */
class DX11TopLevelAccelerationStructure : public RHITopLevelAccelerationStructure {
public:
    DX11TopLevelAccelerationStructure(const RHITopLevelAccelerationStructureInfo& info, DX11Backend& backend) : RHITopLevelAccelerationStructure(info) {}
    virtual void update(const std::vector<RHIAccelerationStructureInstanceInfo>& instance_infos) override final {}
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIBottomLevelAccelerationStructure
 */
class DX11BottomLevelAccelerationStructure : public RHIBottomLevelAccelerationStructure {
public:
    DX11BottomLevelAccelerationStructure(const RHIBottomLevelAccelerationStructureInfo& info, DX11Backend& backend) : RHIBottomLevelAccelerationStructure(info) {}
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIRootSignature
 */
class DX11RootSignature : public RHIRootSignature {
public:
    DX11RootSignature(const RHIRootSignatureInfo& info, std::shared_ptr<DX11Backend> backend) : RHIRootSignature(info) {}
    virtual bool init() override final { return true; }
    virtual RHIDescriptorSetRef create_descriptor_set(uint32_t set) override final;
    virtual void destroy() override final {}
};

/**
 * @brief DX11 implementation of RHIDescriptorSet
 */
class DX11DescriptorSet : public RHIDescriptorSet {
public:
    DX11DescriptorSet(DX11Backend& backend) {}
    virtual RHIDescriptorSet& update_descriptor(const RHIDescriptorUpdateInfo& info) override final { return *this; }
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIRenderPass
 */
class DX11RenderPass : public RHIRenderPass {
public:
    DX11RenderPass(const RHIRenderPassInfo& info, std::shared_ptr<DX11Backend> backend);
    virtual bool init() override final { return true; }
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIGraphicsPipeline
 */
class DX11GraphicsPipeline : public RHIGraphicsPipeline {
public:
    DX11GraphicsPipeline(const RHIGraphicsPipelineInfo& info, std::shared_ptr<DX11Backend> backend);
    virtual bool init() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return nullptr; }

    void bind(ID3D11DeviceContext* context);

private:
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11RasterizerState> rasterizer_state_;
    ComPtr<ID3D11BlendState> blend_state_;
    ComPtr<ID3D11DepthStencilState> depth_stencil_state_;
    D3D11_PRIMITIVE_TOPOLOGY topology_;
    std::weak_ptr<DX11Backend> backend_;
};

/**
 * @brief DX11 implementation of RHIComputePipeline
 */
class DX11ComputePipeline : public RHIComputePipeline {
public:
    DX11ComputePipeline(const RHIComputePipelineInfo& info, std::shared_ptr<DX11Backend> backend);
    virtual bool init() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return nullptr; }

    void bind(ID3D11DeviceContext* context);

private:
    std::weak_ptr<DX11Backend> backend_;
};

/**
 * @brief DX11 implementation of RHIRayTracingPipeline
 */
class DX11RayTracingPipeline : public RHIRayTracingPipeline {
public:
    DX11RayTracingPipeline(const RHIRayTracingPipelineInfo& info, DX11Backend& backend) : RHIRayTracingPipeline(info) {}
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHIFence
 */
class DX11Fence : public RHIFence {
public:
    DX11Fence(bool signaled, std::shared_ptr<DX11Backend> backend);
    virtual bool init() override final;
    virtual void wait() override final;
    virtual void destroy() override final;
    virtual void* raw_handle() override final { return query_.Get(); }

    template<typename T>
    T* raw_handle_as() { return static_cast<T*>(query_.Get()); }

private:
    ComPtr<ID3D11Query> query_;
    std::weak_ptr<DX11Backend> backend_;
    bool signaled_;
};

/**
 * @brief DX11 implementation of RHISemaphore
 */
class DX11Semaphore : public RHISemaphore {
public:
    DX11Semaphore(DX11Backend& backend) {}
    virtual void destroy() override final {}
    virtual void* raw_handle() override final { return nullptr; }
};

/**
 * @brief DX11 implementation of RHICommandContext
 */
class DX11CommandContext : public RHICommandContext {
public:
    DX11CommandContext(RHICommandPoolRef pool, std::shared_ptr<DX11Backend> backend);

    virtual void destroy() override final {}
    virtual void begin_command() override final;
    virtual void end_command() override final;
    virtual void execute(RHIFenceRef fence, RHISemaphoreRef wait_semaphore, RHISemaphoreRef signal_semaphore) override final;

    virtual void texture_barrier(const RHITextureBarrier& barrier) override final;
    virtual void buffer_barrier(const RHIBufferBarrier& barrier) override final;

    virtual void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) override final;
    virtual void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    virtual void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) override final;
    virtual void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;

    virtual void generate_mips(RHITextureRef src) override final;
    virtual void push_event(const std::string& name, Color3 color) override final;
    virtual void pop_event() override final;

    virtual void begin_render_pass(RHIRenderPassRef render_pass) override final;
    virtual void end_render_pass() override final;

    virtual void set_viewport(Offset2D min, Offset2D max) override final;
    virtual void set_scissor(Offset2D min, Offset2D max) override final;
    virtual void set_depth_bias(float constant_bias, float slope_bias, float clamp_bias) override final;
    virtual void set_line_width(float width) override final;

    virtual void set_graphics_pipeline(RHIGraphicsPipelineRef pipeline) override final;
    virtual void set_compute_pipeline(RHIComputePipelineRef pipeline) override final;
    virtual void set_ray_tracing_pipeline(RHIRayTracingPipelineRef pipeline) override final;

    virtual void push_constants(void* data, uint16_t size, ShaderFrequency frequency) override final;
    virtual void bind_descriptor_set(RHIDescriptorSetRef descriptor, uint32_t set) override final;
    virtual void bind_constant_buffer(RHIBufferRef buffer, uint32_t slot, ShaderFrequency frequency) override final;
    virtual void bind_vertex_buffer(RHIBufferRef buffer, uint32_t stream_index, uint32_t offset) override final;
    virtual void bind_index_buffer(RHIBufferRef buffer, uint32_t offset) override final;
    virtual void bind_texture(RHITextureRef texture, uint32_t slot, ShaderFrequency frequency) override final;
    virtual void bind_rw_texture(RHITextureRef texture, uint32_t slot, uint32_t mip_level, ShaderFrequency frequency) override final;
    virtual void bind_sampler(RHISamplerRef sampler, uint32_t slot, ShaderFrequency frequency) override final;

    virtual void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override final;
    virtual void dispatch_indirect(RHIBufferRef argument_buffer, uint32_t argument_offset) override final;
    virtual void trace_rays(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override final;

    virtual void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override final;
    virtual void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) override final;
    virtual void draw_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) override final;
    virtual void draw_indexed_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) override final;

    virtual bool read_texture(RHITextureRef texture, void* data, uint32_t size) override final;

    virtual void imgui_create_fonts_texture() override final;
    virtual void imgui_render_draw_data() override final;
    
    // Check if command context is valid (backend and context are available)
    bool is_valid() const { return !backend_.expired() && context_; }

private:
    std::weak_ptr<DX11Backend> backend_;
    ComPtr<ID3D11DeviceContext> context_;
};

/**
 * @brief DX11 implementation of RHICommandContextImmediate
 * 
 * Uses immediate context for synchronous resource upload operations.
 * All commands are executed immediately on the GPU, and flush() waits
 * for GPU completion.
 */
class DX11CommandContextImmediate : public RHICommandContextImmediate {
public:
    DX11CommandContextImmediate(DX11Backend& backend);
    virtual ~DX11CommandContextImmediate() = default;

    virtual void flush() override final;

    virtual void texture_barrier(const RHITextureBarrier& barrier) override final;
    virtual void buffer_barrier(const RHIBufferBarrier& barrier) override final;

    virtual void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) override final;
    virtual void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    virtual void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) override final;
    virtual void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;

    virtual void generate_mips(RHITextureRef src) override final;

private:
    DX11Backend& backend_;
    
    // Get the immediate context for all operations
    ID3D11DeviceContext* get_context();
};

/**
 * @brief DX11 implementation of RHIBackend
 */
class DX11Backend : public RHIBackend, public std::enable_shared_from_this<DX11Backend> {
public:
    DX11Backend(const RHIBackendInfo& info);
    
    // Get a shared_ptr to this backend - resources should hold this instead of raw reference
    std::shared_ptr<DX11Backend> get_shared() { return shared_from_this(); }

    virtual void tick() override final;
    virtual void destroy() override final;

    virtual void set_name(RHIResourceRef resource, const std::string& name) override final;

    virtual void init_imgui(void* window_handle) override final;
    virtual void imgui_new_frame() override final;
    virtual void imgui_render() override final;
    virtual void imgui_shutdown() override final;

    virtual RHIQueueRef get_queue(const RHIQueueInfo& info) override final;
    virtual RHISurfaceRef create_surface(void* native_window_handle) override final;
    virtual RHISwapchainRef create_swapchain(const RHISwapchainInfo& info) override final;
    virtual RHICommandPoolRef create_command_pool(const RHICommandPoolInfo& info) override final;
    virtual RHICommandContextRef create_command_context(RHICommandPoolRef pool) override final;

    virtual RHIBufferRef create_buffer(const RHIBufferInfo& info) override final;
    virtual RHITextureRef create_texture(const RHITextureInfo& info) override final;
    virtual RHITextureViewRef create_texture_view(const RHITextureViewInfo& info) override final;
    virtual RHISamplerRef create_sampler(const RHISamplerInfo& info) override final;
    virtual RHIShaderRef create_shader(const RHIShaderInfo& info) override final;
    virtual RHIShaderBindingTableRef create_shader_binding_table(const RHIShaderBindingTableInfo& info) override final;
    virtual RHITopLevelAccelerationStructureRef create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo& info) override final;
    virtual RHIBottomLevelAccelerationStructureRef create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo& info) override final;

    virtual RHIRootSignatureRef create_root_signature(const RHIRootSignatureInfo& info) override final;

    virtual RHIRenderPassRef create_render_pass(const RHIRenderPassInfo& info) override final;
    virtual RHIGraphicsPipelineRef create_graphics_pipeline(const RHIGraphicsPipelineInfo& info) override final;
    virtual RHIComputePipelineRef create_compute_pipeline(const RHIComputePipelineInfo& info) override final;
    virtual RHIRayTracingPipelineRef create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo& info) override final;

    virtual RHIFenceRef create_fence(bool signaled) override final;
    virtual RHISemaphoreRef create_semaphore() override final;

    virtual RHICommandContextImmediateRef get_immediate_command() override final;

    virtual std::vector<uint8_t> compile_shader(const char* source, const char* entry, const char* profile) override final;

    virtual GPUProfilerRef create_gpu_profiler() override final;

    ComPtr<IDXGIFactory> get_factory() const { return factory_; }
    ComPtr<ID3D11Device> get_device() const { return device_; }
    ComPtr<ID3D11DeviceContext> get_context() const { return context_; }
    
    // Check if backend is still valid (device not destroyed)
    bool is_valid() const { return device_ != nullptr; }
    
    /// Platform override – reads ID3D11InfoQueue, logs, and __debugbreak().
    virtual bool check_debug_messages(const char* caller_tag = nullptr) override final;

private:
    ComPtr<IDXGIFactory> factory_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11InfoQueue> info_queue_;
    RHICommandContextImmediateRef immediate_context_;

    struct StagingTextureKey {
        uint32_t width, height;
        DXGI_FORMAT format;
        bool operator==(const StagingTextureKey& o) const { return width == o.width && height == o.height && format == o.format; }
    };
    struct StagingTextureHash {
        size_t operator()(const StagingTextureKey& k) const {
            return std::hash<uint32_t>()(k.width) ^ std::hash<uint32_t>()(k.height) ^ std::hash<uint32_t>()(k.format);
        }
    };
    std::unordered_map<StagingTextureKey, ComPtr<ID3D11Texture2D>, StagingTextureHash> staging_texture_cache_;
};

template<class T>
struct DX11ResourceTraits {};

#define DX11_RESOURCE_TRAIT(RHIType, ConcreteType) \
template<> struct DX11ResourceTraits<RHIType> { \
    typedef ConcreteType type; \
    typedef std::shared_ptr<ConcreteType> pointer_type; \
};

DX11_RESOURCE_TRAIT(RHIQueue, DX11Queue)
DX11_RESOURCE_TRAIT(RHISurface, DX11Surface)
DX11_RESOURCE_TRAIT(RHISwapchain, DX11Swapchain)
DX11_RESOURCE_TRAIT(RHICommandPool, DX11CommandPool)
DX11_RESOURCE_TRAIT(RHIBuffer, DX11Buffer)
DX11_RESOURCE_TRAIT(RHITexture, DX11Texture)
DX11_RESOURCE_TRAIT(RHITextureView, DX11TextureView)
DX11_RESOURCE_TRAIT(RHISampler, DX11Sampler)
DX11_RESOURCE_TRAIT(RHIShader, DX11Shader)
DX11_RESOURCE_TRAIT(RHIShaderBindingTable, DX11ShaderBindingTable)
DX11_RESOURCE_TRAIT(RHITopLevelAccelerationStructure, DX11TopLevelAccelerationStructure)
DX11_RESOURCE_TRAIT(RHIBottomLevelAccelerationStructure, DX11BottomLevelAccelerationStructure)
DX11_RESOURCE_TRAIT(RHIRootSignature, DX11RootSignature)
DX11_RESOURCE_TRAIT(RHIDescriptorSet, DX11DescriptorSet)
DX11_RESOURCE_TRAIT(RHIRenderPass, DX11RenderPass)
DX11_RESOURCE_TRAIT(RHIGraphicsPipeline, DX11GraphicsPipeline)
DX11_RESOURCE_TRAIT(RHIComputePipeline, DX11ComputePipeline)
DX11_RESOURCE_TRAIT(RHIRayTracingPipeline, DX11RayTracingPipeline)
DX11_RESOURCE_TRAIT(RHIFence, DX11Fence)
DX11_RESOURCE_TRAIT(RHISemaphore, DX11Semaphore)

template<typename RHIType>
static inline typename DX11ResourceTraits<RHIType>::type* resource_cast(RHIType* resource) {
    return static_cast<typename DX11ResourceTraits<RHIType>::type*>(resource);
}

template<typename RHIType>
static inline typename DX11ResourceTraits<RHIType>::pointer_type resource_cast(std::shared_ptr<RHIType> resource) {
    return std::static_pointer_cast<typename DX11ResourceTraits<RHIType>::type>(resource);
}