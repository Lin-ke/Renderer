#include "platform_rhi.h"
#include "engine/platform/dx_shader_reflection.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <cstring>
#include <limits>

DEFINE_LOG_TAG(LogDX12, "DX12RHI");

namespace {
bool check_hr(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return true;
    ERR(LogDX12, "{} failed: 0x{:08X}", operation, uint32_t(hr));
    return false;
}
uint64_t align_up(uint64_t size, uint64_t alignment) { return (size + alignment - 1) & ~(alignment - 1); }
D3D12_RESOURCE_DESC buffer_desc(uint64_t size) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
ComPtr<ID3D12Resource> make_buffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heap_type) {
    ComPtr<ID3D12Resource> resource;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heap_type;
    auto desc = buffer_desc(size);
    auto state = heap_type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
    check_hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)), "Create buffer");
    return resource;
}
D3D12_COMPARISON_FUNC compare_func(CompareFunction func) {
    constexpr D3D12_COMPARISON_FUNC values[] = {D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_COMPARISON_FUNC_NOT_EQUAL, D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_ALWAYS};
    return func < COMPARE_FUNCTION_MAX_ENUM ? values[func] : D3D12_COMPARISON_FUNC_ALWAYS;
}
D3D12_DESCRIPTOR_RANGE_TYPE range_type(ResourceType type) {
    if (type == RESOURCE_TYPE_UNIFORM_BUFFER) return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    if (type == RESOURCE_TYPE_SAMPLER) return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    if (type == RESOURCE_TYPE_RW_TEXTURE || type == RESOURCE_TYPE_RW_BUFFER || type == RESOURCE_TYPE_RW_TEXEL_BUFFER) return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}
D3D12_SHADER_VISIBILITY visibility(ShaderFrequency frequency) {
    if (frequency == SHADER_FREQUENCY_VERTEX) return D3D12_SHADER_VISIBILITY_VERTEX;
    if (frequency == SHADER_FREQUENCY_FRAGMENT) return D3D12_SHADER_VISIBILITY_PIXEL;
    if (frequency == SHADER_FREQUENCY_GEOMETRY) return D3D12_SHADER_VISIBILITY_GEOMETRY;
    return D3D12_SHADER_VISIBILITY_ALL;
}
}

void DX12DescriptorAllocator::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!check_hr(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)), "Create descriptor heap")) return;
    descriptor_size_ = device->GetDescriptorHandleIncrementSize(type);
    max_descriptors_ = count;
    shader_visible_ = visible;
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    if (visible) gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
}
void DX12DescriptorAllocator::destroy() { heap_.Reset(); free_list_.clear(); current_offset_ = 0; }
uint32_t DX12DescriptorAllocator::allocate(uint32_t count) {
    if (count == 1 && !free_list_.empty()) { auto index = free_list_.back(); free_list_.pop_back(); return index; }
    if (count > max_descriptors_ - current_offset_) { ERR(LogDX12, "Descriptor heap exhausted ({} requested)", count); return UINT32_MAX; }
    uint32_t index = current_offset_;
    current_offset_ += count;
    return index;
}
void DX12DescriptorAllocator::free(uint32_t index) { if (index != UINT32_MAX) free_list_.push_back(index); }
void DX12DescriptorAllocator::reset(uint32_t offset) { current_offset_ = offset; free_list_.clear(); }
D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::get_cpu_handle(uint32_t index) const { return {cpu_start_.ptr + SIZE_T(index) * descriptor_size_}; }
D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::get_gpu_handle(uint32_t index) const { return {gpu_start_.ptr + UINT64(index) * descriptor_size_}; }

void DX12DynamicRingBuffer::init(ID3D12Device* device, uint64_t capacity) {
    capacity_ = capacity;
    buffer_ = make_buffer(device, capacity, D3D12_HEAP_TYPE_UPLOAD);
    if (buffer_) {
        D3D12_RANGE range = {0, 0};
        buffer_->Map(0, &range, reinterpret_cast<void**>(&mapped_ptr_));
        gpu_base_ = buffer_->GetGPUVirtualAddress();
    }
}
void DX12DynamicRingBuffer::destroy() { if (mapped_ptr_) buffer_->Unmap(0, nullptr); mapped_ptr_ = nullptr; buffer_.Reset(); }
bool DX12DynamicRingBuffer::allocate(uint64_t size, uint64_t alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu) {
    uint64_t offset = align_up(head_, alignment);
    if (!mapped_ptr_ || offset > capacity_ || size > capacity_ - offset) return false;
    *cpu = mapped_ptr_ + offset;
    *gpu = gpu_base_ + offset;
    head_ = offset + size;
    return true;
}
void DX12DynamicRingBuffer::reset() { head_ = 0; }

DX12Backend::DX12Backend(const RHIBackendInfo& info) : RHIBackend(info) {
    if (info.enable_debug) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    }
    if (!check_hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgi_factory_)), "Create DXGI factory")) return;
    for (UINT index = 0; dxgi_factory_->EnumAdapters1(index, &adapter_) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc;
        adapter_->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) break;
        adapter_.Reset();
    }
    if (!device_) {
        check_hr(dxgi_factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)), "Find WARP adapter");
        if (!check_hr(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "Create D3D12 device")) return;
    }
    device_.As(&info_queue_);
    D3D12_COMMAND_QUEUE_DESC queue = {};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!check_hr(device_->CreateCommandQueue(&queue, IID_PPV_ARGS(&command_queue_)), "Create command queue")) return;
    if (!check_hr(device_->CreateCommandAllocator(queue.Type, IID_PPV_ARGS(&command_allocator_)), "Create command allocator")) return;
    if (!check_hr(device_->CreateCommandList(0, queue.Type, command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_)), "Create command list")) return;
    command_list_->Close();
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&internal_fence_));
    internal_fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    rtv_allocator_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 32768, false);
    dsv_allocator_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16384, false);
    cpu_srv_uav_allocator_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 131072, false);
    cpu_sampler_allocator_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, false);
    gpu_srv_uav_heap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 262144, true);
    gpu_sampler_heap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
    gpu_srv_uav_heap_.reset(GPU_SRV_UAV_RESERVED_COUNT);
    dynamic_ring_buffer_.init(device_.Get(), 64ull * 1024 * 1024);
    create_default_descriptors();
    create_mipmap_pipelines();
}
DX12Backend::~DX12Backend() { destroy(); }
void DX12Backend::ensure_recording() {
    if (is_recording_ || !device_) return;
    if (!check_hr(command_allocator_->Reset(), "Reset command allocator")) return;
    if (!check_hr(command_list_->Reset(command_allocator_.Get(), nullptr), "Reset command list")) return;
    ID3D12DescriptorHeap* heaps[] = {gpu_srv_uav_heap_.get_heap(), gpu_sampler_heap_.get_heap()};
    command_list_->SetDescriptorHeaps(2, heaps);
    is_recording_ = true;
}
void DX12Backend::submit_and_wait() {
    if (!command_queue_) return;
    if (is_recording_) {
        if (!check_hr(command_list_->Close(), "Close command list")) return;
        ID3D12CommandList* lists[] = {command_list_.Get()};
        command_queue_->ExecuteCommandLists(1, lists);
        is_recording_ = false;
    }
    ++internal_fence_value_;
    if (!check_hr(command_queue_->Signal(internal_fence_.Get(), internal_fence_value_), "Signal queue fence")) return;
    if (internal_fence_->GetCompletedValue() < internal_fence_value_) {
        internal_fence_->SetEventOnCompletion(internal_fence_value_, internal_fence_event_);
        WaitForSingleObject(internal_fence_event_, INFINITE);
    }
    for (auto& callback : completion_callbacks_) callback();
    completion_callbacks_.clear();
    retained_resources_.clear();
    retained_uploads_.clear();
    gpu_srv_uav_heap_.reset(GPU_SRV_UAV_RESERVED_COUNT);
    gpu_sampler_heap_.reset();
    sampler_tables_.clear();
    dynamic_ring_buffer_.reset();
}
void DX12Backend::tick() { RHIBackend::tick(); }
void DX12Backend::destroy() {
    if (!device_) return;
    submit_and_wait();
    imgui_shutdown();
    immediate_command_.reset();
    RHIBackend::destroy();
    for (auto& resources : resource_map_) resources.clear();
    dynamic_ring_buffer_.destroy();
    rtv_allocator_.destroy(); dsv_allocator_.destroy(); cpu_srv_uav_allocator_.destroy(); cpu_sampler_allocator_.destroy();
    gpu_srv_uav_heap_.destroy(); gpu_sampler_heap_.destroy();
    command_list_.Reset(); command_allocator_.Reset(); command_queue_.Reset(); internal_fence_.Reset(); info_queue_.Reset();
    if (internal_fence_event_) CloseHandle(internal_fence_event_);
    internal_fence_event_ = nullptr;
    device_.Reset();
}
bool DX12Backend::check_debug_messages(const char* tag) {
    if (!info_queue_) return false;
    bool found = false;
    for (UINT64 i = 0; i < info_queue_->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0;
        info_queue_->GetMessage(i, nullptr, &size);
        std::vector<uint8_t> bytes(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        info_queue_->GetMessage(i, msg, &size);
        if (msg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            ERR(LogDX12, "{}: {}", tag ? tag : "D3D12", msg->pDescription);
            found = true;
        }
    }
    info_queue_->ClearStoredMessages();
    return found;
}
void DX12Backend::set_name(RHIResourceRef resource, const std::string& name) {
    if (!resource) return;
    resource->set_name(name);
    if (resource->get_type() == RHI_BUFFER || resource->get_type() == RHI_TEXTURE) {
        auto* native = static_cast<ID3D12Resource*>(resource->raw_handle());
        if (native) native->SetName(std::wstring(name.begin(), name.end()).c_str());
    }
}
void DX12Queue::wait_idle() { backend_.submit_and_wait(); }
RHIQueueRef DX12Backend::get_queue(const RHIQueueInfo& info) {
    // This first backend serializes graphics/compute/transfers on the direct queue.
    return std::make_shared<DX12Queue>(info, command_queue_.Get(), *this);
}
DX12Surface::DX12Surface(HWND hwnd) : hwnd_(hwnd) {
    RECT rect = {};
    GetClientRect(hwnd, &rect);
    extent_ = {uint32_t(rect.right - rect.left), uint32_t(rect.bottom - rect.top)};
}
RHISurfaceRef DX12Backend::create_surface(void* hwnd) { return std::make_shared<DX12Surface>(static_cast<HWND>(hwnd)); }
RHICommandPoolRef DX12Backend::create_command_pool(const RHICommandPoolInfo& info) { return std::make_shared<DX12CommandPool>(info); }
RHICommandContextRef DX12Backend::create_command_context(RHICommandPoolRef pool) { return std::make_shared<DX12CommandContext>(pool, shared_from_this()); }

DX12Swapchain::DX12Swapchain(const RHISwapchainInfo& info, std::shared_ptr<DX12Backend> backend) : RHISwapchain(info), backend_(backend) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = info.extent.width; desc.Height = info.extent.height;
    desc.Format = DX12Backend::to_dxgi_uav_format(info.format);
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = std::max(2u, info.image_count);
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain;
    auto hwnd = static_cast<DX12Surface*>(info.surface.get())->get_hwnd();
    if (!check_hr(backend->get_dxgi_factory()->CreateSwapChainForHwnd(backend->get_command_queue(), hwnd, &desc, nullptr, nullptr, &chain), "Create swapchain")) return;
    chain.As(&swap_chain_);
    backend->get_dxgi_factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    for (uint32_t i = 0; i < desc.BufferCount; ++i) {
        ComPtr<ID3D12Resource> resource;
        swap_chain_->GetBuffer(i, IID_PPV_ARGS(&resource));
        auto texture = std::make_shared<DX12Texture>(RHITextureInfo{.format = info.format,
            .extent = {info.extent.width, info.extent.height, 1}, .type = RESOURCE_TYPE_RENDER_TARGET}, backend, resource);
        texture->init();
        texture->set_all_subresource_states(D3D12_RESOURCE_STATE_PRESENT);
        back_buffers_.push_back(texture);
    }
    current_index_ = swap_chain_->GetCurrentBackBufferIndex();
    backend->set_active_swapchain(this);
}
DX12Swapchain::~DX12Swapchain() { destroy(); }
RHITextureRef DX12Swapchain::get_texture(uint32_t index) { return index < back_buffers_.size() ? back_buffers_[index] : nullptr; }
RHITextureRef DX12Swapchain::get_new_frame(RHIFenceRef fence, RHISemaphoreRef) {
    if (fence) fence->wait();
    if (!swap_chain_) return nullptr;
    current_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return get_texture(current_index_);
}
void DX12Swapchain::present(RHISemaphoreRef) {
    auto backend = backend_.lock();
    if (!backend || !swap_chain_) return;
    auto* back_buffer = static_cast<DX12Texture*>(back_buffers_[current_index_].get());
    if (back_buffer->get_subresource_state(0) != D3D12_RESOURCE_STATE_PRESENT) {
        backend->ensure_recording();
        back_buffer->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_PRESENT);
    }
    if (backend->is_recording()) backend->submit_and_wait();
    check_hr(swap_chain_->Present(1, 0), "Present");
    current_index_ = swap_chain_->GetCurrentBackBufferIndex();
}
void DX12Swapchain::destroy() {
    if (auto backend = backend_.lock()) { if (backend->get_active_swapchain() == this) backend->set_active_swapchain(nullptr); }
    back_buffers_.clear(); swap_chain_.Reset();
}
RHISwapchainRef DX12Backend::create_swapchain(const RHISwapchainInfo& info) {
    auto resource = std::make_shared<DX12Swapchain>(info, shared_from_this()); register_resource(resource); return resource;
}

std::vector<uint8_t> DX12Backend::compile_shader(const char* source, const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (backend_info_.enable_debug) flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile, flags, 0, &code, &errors);
    if (FAILED(hr)) { ERR(LogDX12, "Shader compile: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error"); return {}; }
    auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
    return {bytes, bytes + code->GetBufferSize()};
}
DX12Shader::DX12Shader(const RHIShaderInfo& info, std::shared_ptr<DX12Backend> backend) : RHIShader(info), backend_(backend) {}
DX12Shader::~DX12Shader() { destroy(); }
bool DX12Shader::init() { bytecode_ = info_.code; reflect_shader(); return !bytecode_.empty(); }
void DX12Shader::reflect_shader() { reflect_info_ = reflect_dx_shader(bytecode_, info_.frequency); }
void DX12Shader::destroy() { bytecode_.clear(); }

DX12RootSignature::DX12RootSignature(const RHIRootSignatureInfo& info, std::shared_ptr<DX12Backend> backend) : RHIRootSignature(info), backend_(backend) {}
bool DX12RootSignature::init() {
    auto backend = backend_.lock();
    if (!backend || !info_.is_valid()) return false;
    const auto& entries = info_.get_entries();
    if (entries.size() > 64) return false;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(entries.size());
    std::vector<D3D12_ROOT_PARAMETER> parameters(entries.size() + info_.get_push_constants().size());
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        ranges[i] = {range_type(entry.type), entry.size, entry.binding, entry.set, 0};
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable = {1, &ranges[i]};
        parameters[i].ShaderVisibility = visibility(entry.frequency);
    }
    for (size_t i = 0; i < info_.get_push_constants().size(); ++i) {
        const auto& constant = info_.get_push_constants()[i];
        auto& parameter = parameters[entries.size() + i];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants = {constant.binding, constant.set, constant.size / 4};
        parameter.ShaderVisibility = visibility(constant.frequency);
    }
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = UINT(parameters.size()); desc.pParameters = parameters.data();
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (!check_hr(hr, "Serialize root signature")) { if (errors) ERR(LogDX12, "{}", static_cast<const char*>(errors->GetBufferPointer())); return false; }
    return check_hr(backend->get_device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature_)), "Create root signature");
}
RHIDescriptorSetRef DX12RootSignature::create_descriptor_set(uint32_t set) {
    if (set >= MAX_DESCRIPTOR_SETS) return nullptr;
    return std::make_shared<RHIDescriptorSet>(std::static_pointer_cast<RHIRootSignature>(shared_from_this()), set);
}

#define DX12_CREATE(method, base, concrete, info_type) \
base DX12Backend::method(const info_type& info) { \
    if (!device_) return nullptr; \
    auto resource = std::make_shared<concrete>(info, shared_from_this()); \
    if (!resource->init()) return nullptr; \
    register_resource(resource); return resource; \
}
DX12_CREATE(create_buffer, RHIBufferRef, DX12Buffer, RHIBufferInfo)
DX12_CREATE(create_texture, RHITextureRef, DX12Texture, RHITextureInfo)
DX12_CREATE(create_sampler, RHISamplerRef, DX12Sampler, RHISamplerInfo)
DX12_CREATE(create_shader, RHIShaderRef, DX12Shader, RHIShaderInfo)
DX12_CREATE(create_root_signature, RHIRootSignatureRef, DX12RootSignature, RHIRootSignatureInfo)
DX12_CREATE(create_render_pass, RHIRenderPassRef, DX12RenderPass, RHIRenderPassInfo)
DX12_CREATE(create_graphics_pipeline, RHIGraphicsPipelineRef, DX12GraphicsPipeline, RHIGraphicsPipelineInfo)
DX12_CREATE(create_compute_pipeline, RHIComputePipelineRef, DX12ComputePipeline, RHIComputePipelineInfo)
#undef DX12_CREATE
RHIShaderBindingTableRef DX12Backend::create_shader_binding_table(const RHIShaderBindingTableInfo&) { ERR(LogDX12, "DXR is unsupported"); return nullptr; }
RHITopLevelAccelerationStructureRef DX12Backend::create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo&) { ERR(LogDX12, "DXR is unsupported"); return nullptr; }
RHIBottomLevelAccelerationStructureRef DX12Backend::create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo&) { ERR(LogDX12, "DXR is unsupported"); return nullptr; }
RHIRayTracingPipelineRef DX12Backend::create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo&) { ERR(LogDX12, "DXR is unsupported"); return nullptr; }
