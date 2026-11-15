#pragma once

#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/platform/dx12/dx12_gpu_profiler.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

class DX12Backend;
class DX12Queue;
class DX12Surface;
class DX12Swapchain;
class DX12CommandPool;
class DX12Buffer;
class DX12Texture;
class DX12TextureView;
class DX12Sampler;
class DX12Shader;
class DX12RootSignature;
class DX12RenderPass;
class DX12GraphicsPipeline;
class DX12ComputePipeline;
class DX12Fence;
class DX12Semaphore;
class DX12CommandContext;
class DX12CommandContextImmediate;

class DX12DescriptorAllocator {
public:
    void init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t max_descriptors, bool shader_visible);
    void destroy();

    uint32_t allocate(uint32_t count = 1);
    void free(uint32_t index);
    void reset(uint32_t reserved_offset = 0);

    D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(uint32_t index) const;
    ID3D12DescriptorHeap* get_heap() const { return heap_.Get(); }
    uint32_t get_descriptor_size() const { return descriptor_size_; }
    uint32_t get_max_descriptors() const { return max_descriptors_; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_ = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_ = { 0 };
    uint32_t descriptor_size_ = 0;
    uint32_t max_descriptors_ = 0;
    uint32_t current_offset_ = 0;
    bool shader_visible_ = false;
    std::vector<uint32_t> free_list_;
};

class DX12DynamicRingBuffer {
public:
    void init(ID3D12Device* device, uint64_t capacity);
    void destroy();

    bool allocate(uint64_t size, uint64_t alignment, void** out_cpu_ptr, D3D12_GPU_VIRTUAL_ADDRESS* out_gpu_addr);
    void reset();

    ID3D12Resource* get_resource() const { return buffer_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS get_gpu_base() const { return gpu_base_; }

private:
    ComPtr<ID3D12Resource> buffer_;
    uint8_t* mapped_ptr_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_base_ = 0;
    uint64_t capacity_ = 0;
    uint64_t head_ = 0;
};

class DX12Queue : public RHIQueue {
public:
    DX12Queue(const RHIQueueInfo& info, ID3D12CommandQueue* queue, DX12Backend& backend)
        : RHIQueue(info), queue_(queue), backend_(backend) {}

    void wait_idle() override final;
    void* raw_handle() override final { return queue_; }

private:
    ID3D12CommandQueue* queue_ = nullptr;
    DX12Backend& backend_;
};

class DX12Surface : public RHISurface {
public:
    explicit DX12Surface(HWND hwnd);
    HWND get_hwnd() const { return hwnd_; }
    void destroy() override final {}

private:
    HWND hwnd_ = nullptr;
};

class DX12Swapchain : public RHISwapchain {
public:
    DX12Swapchain(const RHISwapchainInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12Swapchain() override;

    uint32_t get_current_frame_index() override final { return current_index_; }
    RHITextureRef get_texture(uint32_t index) override final;
    RHITextureRef get_new_frame(RHIFenceRef fence, RHISemaphoreRef signal_semaphore) override final;
    void present(RHISemaphoreRef wait_semaphore) override final;
    void destroy() override final;
    void* raw_handle() override final { return swap_chain_.Get(); }

private:
    ComPtr<IDXGISwapChain3> swap_chain_;
    std::vector<RHITextureRef> back_buffers_;
    uint32_t current_index_ = 0;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12CommandPool : public RHICommandPool {
public:
    explicit DX12CommandPool(const RHICommandPoolInfo& info) : RHICommandPool(info) {}
    void destroy() override final {}
    void* raw_handle() override final { return nullptr; }
};

class DX12Buffer : public RHIBuffer {
public:
    DX12Buffer(const RHIBufferInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12Buffer() override;

    bool init() override final;
    void* map() override final;
    void unmap() override final;
    void unmap_written_range(uint64_t offset, uint64_t size) override final;
    std::vector<uint8_t> snapshot_data() const override { return cpu_shadow_; }
    void complete_readback(std::vector<uint8_t> data) { cpu_shadow_ = std::move(data); }
    void destroy() override final;
    void* raw_handle() override final { return resource_.Get(); }

    ID3D12Resource* get_resource() const { return resource_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS get_current_gpu_address() const { return current_gpu_address_; }

    D3D12_RESOURCE_STATES get_state() const { return state_; }
    void set_state(D3D12_RESOURCE_STATES state) { state_ = state; }
    void transition(ID3D12GraphicsCommandList* cmd_list, D3D12_RESOURCE_STATES new_state);

private:
    ComPtr<ID3D12Resource> resource_;
    D3D12_HEAP_TYPE heap_type_ = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
    uint8_t* persistent_mapped_ptr_ = nullptr;
    void* active_map_ptr_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS current_gpu_address_ = 0;
    std::vector<uint8_t> cpu_shadow_;

    std::weak_ptr<DX12Backend> backend_;
};

class DX12Texture : public RHITexture {
public:
    DX12Texture(const RHITextureInfo& info, std::shared_ptr<DX12Backend> backend, ComPtr<ID3D12Resource> existing_resource = nullptr);
    ~DX12Texture() override;

    bool init() override final;
    void destroy() override final;
    void* raw_handle() override final { return resource_.Get(); }

    ID3D12Resource* get_resource() const { return resource_.Get(); }
    uint32_t get_array_layers() const { return array_layers_; }
    uint32_t get_subresource_index(uint32_t mip_slice, uint32_t array_slice) const {
        return mip_slice + array_slice * info_.mip_levels;
    }
    uint32_t get_total_subresources() const { return info_.mip_levels * array_layers_; }

    D3D12_RESOURCE_STATES get_subresource_state(uint32_t subresource) const;
    void set_subresource_state(uint32_t subresource, D3D12_RESOURCE_STATES state);
    void set_all_subresource_states(D3D12_RESOURCE_STATES state);

    void transition(ID3D12GraphicsCommandList* cmd_list, D3D12_RESOURCE_STATES new_state,
                    uint32_t base_mip = 0, uint32_t mip_count = 0,
                    uint32_t base_layer = 0, uint32_t layer_count = 0);

    D3D12_CPU_DESCRIPTOR_HANDLE get_rtv(uint32_t mip = 0, uint32_t base_layer = 0, uint32_t layer_count = 1);
    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv(bool read_only = false, uint32_t mip = 0, uint32_t base_layer = 0, uint32_t layer_count = 1);
    D3D12_CPU_DESCRIPTOR_HANDLE get_srv(TextureViewType view_type = VIEW_TYPE_MAX_ENUM, uint32_t base_mip = 0, uint32_t mip_count = 0, uint32_t base_layer = 0, uint32_t layer_count = 0);
    D3D12_CPU_DESCRIPTOR_HANDLE get_uav(uint32_t mip = 0, uint32_t base_layer = 0, uint32_t layer_count = 0, bool array_view = false);

private:
    ComPtr<ID3D12Resource> resource_;
    uint32_t array_layers_ = 1;
    std::vector<D3D12_RESOURCE_STATES> subresource_states_;

    std::vector<uint32_t> extra_rtv_indices_;
    std::vector<uint32_t> extra_dsv_indices_;
    std::vector<uint32_t> extra_srv_indices_;
    std::vector<uint32_t> extra_uav_indices_;
    std::map<std::array<uint32_t, 6>, D3D12_CPU_DESCRIPTOR_HANDLE> view_cache_;

    std::weak_ptr<DX12Backend> backend_;
};

class DX12TextureView : public RHITextureView {
public:
    DX12TextureView(const RHITextureViewInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12TextureView() override;

    void destroy() override final;
    void* raw_handle() override final;

    D3D12_CPU_DESCRIPTOR_HANDLE get_rtv() const { return rtv_handle_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv(bool read_only = false) const { return read_only ? dsv_ro_handle_ : dsv_handle_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_srv() const { return srv_handle_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_uav() const { return uav_handle_; }
    bool has_imgui_descriptor() const { return imgui_gpu_handle_.ptr != 0; }

    DX12Texture* get_dx12_texture() const { return static_cast<DX12Texture*>(info_.texture.get()); }

private:
    D3D12_GPU_DESCRIPTOR_HANDLE imgui_gpu_handle_ = {0};
    uint32_t imgui_srv_index_ = UINT32_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_ro_handle_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE srv_handle_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE uav_handle_ = { 0 };

    std::weak_ptr<DX12Backend> backend_;
};

class DX12Sampler : public RHISampler {
public:
    DX12Sampler(const RHISamplerInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12Sampler() override;

    bool init() override final;
    void destroy() override final;
    void* raw_handle() override final { return reinterpret_cast<void*>(handle_.ptr); }

    D3D12_CPU_DESCRIPTOR_HANDLE get_handle() const { return handle_; }

private:
    uint32_t descriptor_index_ = UINT32_MAX;
    D3D12_CPU_DESCRIPTOR_HANDLE handle_ = { 0 };
    std::weak_ptr<DX12Backend> backend_;
};

class DX12Shader : public RHIShader {
public:
    DX12Shader(const RHIShaderInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12Shader() override;

    bool init() override final;
    void destroy() override final;
    void* raw_handle() override final { return bytecode_.empty() ? nullptr : bytecode_.data(); }

    D3D12_SHADER_BYTECODE get_bytecode() const {
        return { bytecode_.data(), bytecode_.size() };
    }
    const std::vector<uint8_t>& get_raw_bytecode() const { return bytecode_; }

private:
    void reflect_shader();
    std::vector<uint8_t> bytecode_;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12RootSignature : public RHIRootSignature {
public:
    DX12RootSignature(const RHIRootSignatureInfo& info, std::shared_ptr<DX12Backend> backend);
    bool init() override;
    RHIDescriptorSetRef create_descriptor_set(uint32_t set) override;
    void destroy() override { signature_.Reset(); }
    void* raw_handle() override { return signature_.Get(); }
    ID3D12RootSignature* get_signature() const { return signature_.Get(); }
private:
    ComPtr<ID3D12RootSignature> signature_;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12RenderPass : public RHIRenderPass {
public:
    DX12RenderPass(const RHIRenderPassInfo& info, std::shared_ptr<DX12Backend> backend)
        : RHIRenderPass(info), backend_(backend) {}

    bool init() override final { return true; }
    void destroy() override final {}
    void* raw_handle() override final { return nullptr; }

private:
    std::weak_ptr<DX12Backend> backend_;
};

class DX12GraphicsPipeline : public RHIGraphicsPipeline {
public:
    DX12GraphicsPipeline(const RHIGraphicsPipelineInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12GraphicsPipeline() override;

    bool init() override final;
    void destroy() override final;
    void* raw_handle() override final { return pso_.Get(); }

    ID3D12PipelineState* get_pso() const { return pso_.Get(); }
    D3D_PRIMITIVE_TOPOLOGY get_topology() const { return topology_; }
    const std::vector<uint32_t>& get_vertex_strides() const { return vertex_strides_; }

private:
    ComPtr<ID3D12PipelineState> pso_;
    D3D_PRIMITIVE_TOPOLOGY topology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    std::vector<uint32_t> vertex_strides_;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12ComputePipeline : public RHIComputePipeline {
public:
    DX12ComputePipeline(const RHIComputePipelineInfo& info, std::shared_ptr<DX12Backend> backend);
    ~DX12ComputePipeline() override;

    bool init() override final;
    void destroy() override final;
    void* raw_handle() override final { return pso_.Get(); }

    ID3D12PipelineState* get_pso() const { return pso_.Get(); }

private:
    ComPtr<ID3D12PipelineState> pso_;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12Fence : public RHIFence {
public:
    DX12Fence(bool signaled, std::shared_ptr<DX12Backend> backend);
    ~DX12Fence() override;

    bool init() override final;
    void wait() override final;
    void destroy() override final;
    void* raw_handle() override final { return fence_.Get(); }

    void signal(ID3D12CommandQueue* queue);

private:
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    uint64_t current_value_ = 0;
    bool signaled_ = false;
    std::weak_ptr<DX12Backend> backend_;
};

class DX12Semaphore : public RHISemaphore {
public:
    explicit DX12Semaphore(DX12Backend& backend) : RHISemaphore(), backend_(backend) {}
    void destroy() override final {}
    void* raw_handle() override final { return nullptr; }

private:
    DX12Backend& backend_;
};

class DX12CommandContext : public RHICommandContext {
public:
    DX12CommandContext(RHICommandPoolRef pool, std::shared_ptr<DX12Backend> backend);
    ~DX12CommandContext() override = default;

    void destroy() override final {}
    void begin_command() override final;
    void end_command() override final;
    void execute(RHIFenceRef fence, RHISemaphoreRef wait_semaphore, RHISemaphoreRef signal_semaphore) override final;

    void texture_barrier(const RHITextureBarrier& barrier) override final;
    void buffer_barrier(const RHIBufferBarrier& barrier) override final;

    void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) override final;
    void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) override final;
    void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    void generate_mips(RHITextureRef src) override final;

    void push_event(const std::string& name, Color3 color) override final;
    void pop_event() override final;

    void begin_render_pass(RHIRenderPassRef render_pass) override final;
    void end_render_pass() override final;

    void set_viewport(Offset2D min, Offset2D max) override final;
    void set_scissor(Offset2D min, Offset2D max) override final;
    void set_depth_bias(float constant_bias, float slope_bias, float clamp_bias) override final;
    void set_line_width(float width) override final;

    void set_graphics_pipeline(RHIGraphicsPipelineRef graphics_pipeline) override final;
    void set_compute_pipeline(RHIComputePipelineRef compute_pipeline) override final;
    void set_ray_tracing_pipeline(RHIRayTracingPipelineRef ray_tracing_pipeline) override final;

    void push_constants(void* data, uint16_t size, ShaderFrequency frequency) override final;
    void bind_descriptor_set(RHIDescriptorSetRef descriptor, uint32_t set) override final;
    void bind_vertex_buffer(RHIBufferRef vertex_buffer, uint32_t stream_index, uint32_t offset) override final;
    void bind_index_buffer(RHIBufferRef index_buffer, uint32_t offset) override final;

    void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override final;
    void dispatch_indirect(RHIBufferRef argument_buffer, uint32_t argument_offset) override final;
    void trace_rays(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override final;

    void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override final;
    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) override final;
    void draw_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) override final;
    void draw_indexed_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) override final;

    bool read_texture(RHITextureRef texture, void* data, uint32_t size) override final;

    void imgui_create_fonts_texture() override final;
    void imgui_render_draw_data() override final;

private:
    bool flush_graphics_bindings();
    bool flush_compute_bindings();

    std::weak_ptr<DX12Backend> backend_;
    DX12GraphicsPipeline* current_graphics_pipeline_ = nullptr;
    DX12ComputePipeline* current_compute_pipeline_ = nullptr;
    DX12RenderPass* current_render_pass_ = nullptr;
    DX12Texture* current_depth_texture_ = nullptr;
    bool current_depth_read_only_ = false;

    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> bound_sets_ = {};
    bool flush_bindings(bool compute);
    struct VertexStreamBinding {
        DX12Buffer* buffer = nullptr;
        uint32_t offset = 0;
        bool active = false;
    };
    std::array<VertexStreamBinding, 8> vertex_streams_ = {};
};

class DX12CommandContextImmediate : public RHICommandContextImmediate {
public:
    explicit DX12CommandContextImmediate(DX12Backend& backend);
    ~DX12CommandContextImmediate() override = default;

    void flush() override final;
    void texture_barrier(const RHITextureBarrier& barrier) override final;
    void buffer_barrier(const RHIBufferBarrier& barrier) override final;
    void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) override final;
    void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) override final;
    void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) override final;
    void generate_mips(RHITextureRef src) override final;

private:
    DX12CommandContext* ensure_context();
    std::shared_ptr<DX12CommandContext> cmd_context_;
    DX12Backend& backend_;
};

class DX12Backend : public RHIBackend, public std::enable_shared_from_this<DX12Backend> {
public:
    explicit DX12Backend(const RHIBackendInfo& info);
    ~DX12Backend() override;

    void tick() override final;
    void destroy() override final;
    void set_name(RHIResourceRef resource, const std::string& name) override final;

    void init_imgui(void* window_handle) override final;
    void imgui_new_frame() override final;
    void imgui_render() override final;
    void imgui_shutdown() override final;
    void prepare_imgui_resources();

    RHIQueueRef get_queue(const RHIQueueInfo& info) override final;
    RHISurfaceRef create_surface(void* native_window_handle) override final;
    RHISwapchainRef create_swapchain(const RHISwapchainInfo& info) override final;
    RHICommandPoolRef create_command_pool(const RHICommandPoolInfo& info) override final;
    RHICommandContextRef create_command_context(RHICommandPoolRef pool) override final;

    RHIBufferRef create_buffer(const RHIBufferInfo& info) override final;
    RHIBufferRef create_static_buffer(const RHIBufferInfo& info, const void* data) override final;
    RHITextureRef create_texture(const RHITextureInfo& info) override final;
    RHITextureViewRef create_texture_view(const RHITextureViewInfo& info) override final;
    RHISamplerRef create_sampler(const RHISamplerInfo& info) override final;
    RHIShaderRef create_shader(const RHIShaderInfo& info) override final;
    RHIShaderBindingTableRef create_shader_binding_table(const RHIShaderBindingTableInfo& info) override final;
    RHITopLevelAccelerationStructureRef create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo& info) override final;
    RHIBottomLevelAccelerationStructureRef create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo& info) override final;

    RHIRootSignatureRef create_root_signature(const RHIRootSignatureInfo& info) override final;

    RHIRenderPassRef create_render_pass(const RHIRenderPassInfo& info) override final;
    RHIGraphicsPipelineRef create_graphics_pipeline(const RHIGraphicsPipelineInfo& info) override final;
    RHIComputePipelineRef create_compute_pipeline(const RHIComputePipelineInfo& info) override final;
    RHIRayTracingPipelineRef create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo& info) override final;

    RHIFenceRef create_fence(bool signaled) override final;
    RHISemaphoreRef create_semaphore() override final;

    RHICommandContextImmediateRef get_immediate_command() override final;

    std::vector<uint8_t> compile_shader(const char* source, const char* entry, const char* profile) override final;

    GPUProfilerRef create_gpu_profiler() override final;

    bool is_valid() const override final { return device_ && command_queue_ && command_list_ && internal_fence_ && internal_fence_event_; }
    bool check_debug_messages(const char* caller_tag = nullptr) override final;

    ID3D12Device* get_device() const { return device_.Get(); }
    IDXGIFactory4* get_dxgi_factory() const { return dxgi_factory_.Get(); }
    ID3D12CommandQueue* get_command_queue() const { return command_queue_.Get(); }
    ID3D12GraphicsCommandList* get_command_list() const { return command_list_.Get(); }

    void ensure_recording();
    bool is_recording() const { return is_recording_; }
    void submit_and_wait();

    DX12DescriptorAllocator& get_rtv_allocator() { return rtv_allocator_; }
    DX12DescriptorAllocator& get_dsv_allocator() { return dsv_allocator_; }
    DX12DescriptorAllocator& get_cpu_srv_uav_allocator() { return cpu_srv_uav_allocator_; }
    DX12DescriptorAllocator& get_cpu_sampler_allocator() { return cpu_sampler_allocator_; }
    DX12DescriptorAllocator& get_gpu_srv_uav_heap() { return gpu_srv_uav_heap_; }
    DX12DescriptorAllocator& get_gpu_sampler_heap() { return gpu_sampler_heap_; }
    DX12DynamicRingBuffer& get_dynamic_ring_buffer() { return dynamic_ring_buffer_; }

    D3D12_CPU_DESCRIPTOR_HANDLE get_null_srv_2d() const { return null_srv_2d_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_null_srv_cube() const { return null_srv_cube_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_null_uav_2d() const { return null_uav_2d_; }
    D3D12_CPU_DESCRIPTOR_HANDLE get_default_sampler() const { return default_sampler_handle_; }

    ID3D12RootSignature* get_mipmap_root_sig() const { return mipmap_root_sig_.Get(); }
    ID3D12PipelineState* get_mipmap_pso_array() const { return mipmap_pso_array_.Get(); }

    DX12Swapchain* get_active_swapchain() const { return active_swapchain_; }
    void set_active_swapchain(DX12Swapchain* swapchain) { active_swapchain_ = swapchain; }

    static constexpr uint32_t GPU_SRV_UAV_RESERVED_COUNT = 8192;
    void retain(RHIResourceRef resource) { retained_resources_.push_back(resource); }
    void retain_upload(ComPtr<ID3D12Resource> resource) { retained_uploads_.push_back(resource); }
    void on_complete(std::function<void()> callback) { completion_callbacks_.push_back(std::move(callback)); }

    static DXGI_FORMAT to_dxgi_format(RHIFormat format);
    static DXGI_FORMAT to_dxgi_resource_format(RHIFormat format, ResourceType usages);
    static DXGI_FORMAT to_dxgi_srv_format(RHIFormat format);
    static DXGI_FORMAT to_dxgi_dsv_format(RHIFormat format);
    static DXGI_FORMAT to_dxgi_uav_format(RHIFormat format);
    static uint32_t get_format_byte_size(RHIFormat format);
    static D3D12_RESOURCE_STATES to_d3d12_resource_state(RHIResourceState state);

    uint32_t allocate_imgui_srv_index();
    void free_imgui_srv_index(uint32_t index);
    uint32_t sampler_table(D3D12_CPU_DESCRIPTOR_HANDLE handle);

private:
    void create_default_descriptors();
    void create_mipmap_pipelines();

    ComPtr<IDXGIFactory4> dxgi_factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12InfoQueue> info_queue_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<ID3D12CommandAllocator> command_allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    ComPtr<ID3D12Fence> internal_fence_;
    HANDLE internal_fence_event_ = nullptr;
    uint64_t internal_fence_value_ = 0;
    bool is_recording_ = false;

    DX12DescriptorAllocator rtv_allocator_;
    DX12DescriptorAllocator dsv_allocator_;
    DX12DescriptorAllocator cpu_srv_uav_allocator_;
    DX12DescriptorAllocator cpu_sampler_allocator_;
    DX12DescriptorAllocator gpu_srv_uav_heap_;
    DX12DescriptorAllocator gpu_sampler_heap_;
    uint32_t imgui_next_srv_index_ = 1;

    DX12DynamicRingBuffer dynamic_ring_buffer_;

    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_2d_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_cube_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE null_uav_2d_ = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE default_sampler_handle_ = { 0 };

    ComPtr<ID3D12RootSignature> mipmap_root_sig_;
    ComPtr<ID3D12PipelineState> mipmap_pso_array_;

    RHICommandContextImmediateRef immediate_command_;
    DX12Swapchain* active_swapchain_ = nullptr;
    bool imgui_initialized_ = false;
    std::vector<RHIResourceRef> retained_resources_;
    std::vector<ComPtr<ID3D12Resource>> retained_uploads_;
    std::vector<std::function<void()>> completion_callbacks_;
    std::vector<uint32_t> imgui_free_srv_indices_;
    std::unordered_map<SIZE_T, uint32_t> sampler_tables_;
};
