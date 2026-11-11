#include "platform_rhi.h"
#include <algorithm>
#include <cstring>

DEFINE_LOG_TAG(LogDX12Resources, "DX12Resources");

DX12Buffer::DX12Buffer(const RHIBufferInfo& info, std::shared_ptr<DX12Backend> backend) : RHIBuffer(info), backend_(backend) {}
DX12Buffer::~DX12Buffer() { destroy(); }
bool DX12Buffer::init() {
    auto backend = backend_.lock();
    if (!backend || !info_.size) return false;
    heap_type_ = info_.memory_usage == MEMORY_USAGE_GPU_TO_CPU ? D3D12_HEAP_TYPE_READBACK :
        (info_.memory_usage == MEMORY_USAGE_GPU_ONLY ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD);
    state_ = heap_type_ == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST :
        (heap_type_ == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON);
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = heap_type_;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (info_.type & RESOURCE_TYPE_UNIFORM_BUFFER) ? (info_.size + 255) & ~255ull : info_.size;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (info_.type & RESOURCE_TYPE_RW_BUFFER) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(backend->get_device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state_, nullptr, IID_PPV_ARGS(&resource_)))) return false;
    current_gpu_address_ = resource_->GetGPUVirtualAddress();
    if (heap_type_ == D3D12_HEAP_TYPE_UPLOAD) {
        D3D12_RANGE read = {0, 0};
        if (FAILED(resource_->Map(0, &read, reinterpret_cast<void**>(&persistent_mapped_ptr_)))) return false;
        cpu_shadow_.resize(info_.size);
    }
    return true;
}
void* DX12Buffer::map() {
    if (active_map_ptr_ || !resource_) return nullptr;
    if (heap_type_ == D3D12_HEAP_TYPE_UPLOAD) active_map_ptr_ = cpu_shadow_.data();
    else if (heap_type_ == D3D12_HEAP_TYPE_READBACK) {
        auto backend = backend_.lock();
        if (!backend) return nullptr;
        backend->submit_and_wait();
        if (!cpu_shadow_.empty()) { active_map_ptr_ = cpu_shadow_.data(); return active_map_ptr_; }
        D3D12_RANGE range = {0, SIZE_T(info_.size)};
        if (FAILED(resource_->Map(0, &range, &active_map_ptr_))) return nullptr;
    }
    return active_map_ptr_;
}
void DX12Buffer::unmap() {
    if (!active_map_ptr_) return;
    if (heap_type_ == D3D12_HEAP_TYPE_UPLOAD) memcpy(persistent_mapped_ptr_, cpu_shadow_.data(), size_t(info_.size));
    else if (cpu_shadow_.empty()) { D3D12_RANGE written = {0, 0}; resource_->Unmap(0, &written); }
    active_map_ptr_ = nullptr;
}
void DX12Buffer::destroy() {
    if (resource_ && persistent_mapped_ptr_) resource_->Unmap(0, nullptr);
    persistent_mapped_ptr_ = nullptr; active_map_ptr_ = nullptr; resource_.Reset();
}
void DX12Buffer::transition(ID3D12GraphicsCommandList* command, D3D12_RESOURCE_STATES state) {
    if (heap_type_ != D3D12_HEAP_TYPE_DEFAULT) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    if (state == state_) {
        if (state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) return;
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barrier.UAV.pResource = resource_.Get();
    } else {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state_, state};
    }
    command->ResourceBarrier(1, &barrier);
    state_ = state;
}

DX12Texture::DX12Texture(const RHITextureInfo& info, std::shared_ptr<DX12Backend> backend, ComPtr<ID3D12Resource> resource)
    : RHITexture(info), resource_(resource), backend_(backend) {}
DX12Texture::~DX12Texture() { destroy(); }
bool DX12Texture::init() {
    auto backend = backend_.lock();
    if (!backend || !info_.extent.width || !info_.extent.height) return false;
    if (!info_.mip_levels) info_.mip_levels = info_.extent.mip_size();
    // sRGB SRVs use a typeless resource and linear UNORM UAVs for mip generation.
    if (info_.mip_levels > 1 && info_.format == FORMAT_R8G8B8A8_SRGB) info_.type |= RESOURCE_TYPE_RW_TEXTURE;
    array_layers_ = (info_.creation_flag & TEXTURE_CREATION_FORCE_3D) ? 1 : info_.array_layers;
    default_range_ = {is_depth_format(info_.format) ? TEXTURE_ASPECT_DEPTH : TEXTURE_ASPECT_COLOR, 0, info_.mip_levels, 0, array_layers_};
    default_layers_ = {default_range_.aspect, 0, 0, array_layers_};
    subresource_states_.assign(get_total_subresources(), D3D12_RESOURCE_STATE_COMMON);
    if (resource_) return true;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = (info_.creation_flag & TEXTURE_CREATION_FORCE_3D) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = info_.extent.width; desc.Height = info_.extent.height;
    desc.DepthOrArraySize = UINT16(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? info_.extent.depth : array_layers_);
    desc.MipLevels = UINT16(info_.mip_levels);
    desc.Format = DX12Backend::to_dxgi_resource_format(info_.format, info_.type);
    desc.SampleDesc.Count = 1;
    if (info_.type & RESOURCE_TYPE_RENDER_TARGET) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (info_.type & RESOURCE_TYPE_DEPTH_STENCIL) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (info_.type & RESOURCE_TYPE_RW_TEXTURE) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    return SUCCEEDED(backend->get_device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource_)));
}
void DX12Texture::destroy() {
    if (auto backend = backend_.lock()) {
        auto release = [](auto& allocator, auto& indices) { for (auto index : indices) allocator.free(index); indices.clear(); };
        release(backend->get_rtv_allocator(), extra_rtv_indices_);
        release(backend->get_dsv_allocator(), extra_dsv_indices_);
        release(backend->get_cpu_srv_uav_allocator(), extra_srv_indices_);
        release(backend->get_cpu_srv_uav_allocator(), extra_uav_indices_);
    }
    view_cache_.clear();
    resource_.Reset();
}
D3D12_RESOURCE_STATES DX12Texture::get_subresource_state(uint32_t index) const { return subresource_states_[index]; }
void DX12Texture::set_subresource_state(uint32_t index, D3D12_RESOURCE_STATES state) { subresource_states_[index] = state; }
void DX12Texture::set_all_subresource_states(D3D12_RESOURCE_STATES state) { std::fill(subresource_states_.begin(), subresource_states_.end(), state); }
void DX12Texture::transition(ID3D12GraphicsCommandList* command, D3D12_RESOURCE_STATES state, uint32_t mip, uint32_t mips, uint32_t layer, uint32_t layers) {
    if (!mips) mips = info_.mip_levels - mip;
    if (!layers) layers = array_layers_ - layer;
    if (mip + mips > info_.mip_levels || layer + layers > array_layers_) return;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    bool uav = false;
    for (uint32_t l = layer; l < layer + layers; ++l) for (uint32_t m = mip; m < mip + mips; ++m) {
        auto index = get_subresource_index(m, l);
        auto old = subresource_states_[index];
        if (old == state) { uav |= state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS; continue; }
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource_.Get(), index, old, state};
        barriers.push_back(barrier);
        subresource_states_[index] = state;
    }
    if (uav) { D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barrier.UAV.pResource = resource_.Get(); barriers.push_back(barrier); }
    if (!barriers.empty()) command->ResourceBarrier(UINT(barriers.size()), barriers.data());
}
D3D12_CPU_DESCRIPTOR_HANDLE DX12Texture::get_rtv(uint32_t mip, uint32_t layer, uint32_t layers) {
    std::array<uint32_t, 6> key = {0, mip, layer, layers, 0, 0};
    if (auto iter = view_cache_.find(key); iter != view_cache_.end()) return iter->second;
    auto backend = backend_.lock();
    if (!backend) return {};
    auto& allocator = backend->get_rtv_allocator();
    auto index = allocator.allocate(); if (index == UINT32_MAX) return {};
    extra_rtv_indices_.push_back(index);
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = DX12Backend::to_dxgi_format(info_.format);
    if (array_layers_ > 1) { desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray = {mip, layer, layers, 0}; }
    else { desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; desc.Texture2D.MipSlice = mip; }
    backend->get_device()->CreateRenderTargetView(resource_.Get(), &desc, allocator.get_cpu_handle(index));
    view_cache_[key] = allocator.get_cpu_handle(index);
    return allocator.get_cpu_handle(index);
}
D3D12_CPU_DESCRIPTOR_HANDLE DX12Texture::get_dsv(bool read_only, uint32_t mip, uint32_t layer, uint32_t layers) {
    std::array<uint32_t, 6> key = {1, mip, layer, layers, uint32_t(read_only), 0};
    if (auto iter = view_cache_.find(key); iter != view_cache_.end()) return iter->second;
    auto backend = backend_.lock(); if (!backend) return {};
    auto& allocator = backend->get_dsv_allocator();
    auto index = allocator.allocate(); if (index == UINT32_MAX) return {};
    extra_dsv_indices_.push_back(index);
    D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
    desc.Format = DX12Backend::to_dxgi_dsv_format(info_.format);
    if (read_only) desc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    if (array_layers_ > 1) { desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray = {mip, layer, layers}; }
    else { desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; desc.Texture2D.MipSlice = mip; }
    backend->get_device()->CreateDepthStencilView(resource_.Get(), &desc, allocator.get_cpu_handle(index));
    view_cache_[key] = allocator.get_cpu_handle(index);
    return allocator.get_cpu_handle(index);
}
D3D12_CPU_DESCRIPTOR_HANDLE DX12Texture::get_srv(TextureViewType type, uint32_t mip, uint32_t mips, uint32_t layer, uint32_t layers) {
    std::array<uint32_t, 6> key = {2, mip, mips, layer, layers, uint32_t(type)};
    if (auto iter = view_cache_.find(key); iter != view_cache_.end()) return iter->second;
    auto backend = backend_.lock(); if (!backend) return {};
    auto& allocator = backend->get_cpu_srv_uav_allocator();
    auto index = allocator.allocate(); if (index == UINT32_MAX) return {};
    extra_srv_indices_.push_back(index);
    if (!mips) mips = info_.mip_levels - mip;
    if (!layers) layers = array_layers_ - layer;
    if (type == VIEW_TYPE_MAX_ENUM || type == VIEW_TYPE_UNDEFINED)
        type = (info_.type & RESOURCE_TYPE_TEXTURE_CUBE) ? VIEW_TYPE_CUBE : VIEW_TYPE_2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DX12Backend::to_dxgi_srv_format(info_.format);
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (resource_->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; desc.Texture3D = {mip, mips, 0};
    } else if (type == VIEW_TYPE_CUBE && layers == 6) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; desc.TextureCube = {mip, mips, 0};
    } else if (type == VIEW_TYPE_CUBE_ARRAY) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; desc.TextureCubeArray = {mip, mips, layer, layers / 6, 0};
    } else if (array_layers_ > 1 || type == VIEW_TYPE_2D_ARRAY) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray = {mip, mips, layer, layers, 0, 0};
    } else {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; desc.Texture2D = {mip, mips, 0, 0};
    }
    backend->get_device()->CreateShaderResourceView(resource_.Get(), &desc, allocator.get_cpu_handle(index));
    view_cache_[key] = allocator.get_cpu_handle(index);
    return allocator.get_cpu_handle(index);
}
D3D12_CPU_DESCRIPTOR_HANDLE DX12Texture::get_uav(uint32_t mip, uint32_t layer, uint32_t layers, bool array_view) {
    std::array<uint32_t, 6> key = {3, mip, layer, layers, uint32_t(array_view), 0};
    if (auto iter = view_cache_.find(key); iter != view_cache_.end()) return iter->second;
    auto backend = backend_.lock(); if (!backend) return {};
    auto& allocator = backend->get_cpu_srv_uav_allocator();
    auto index = allocator.allocate(); if (index == UINT32_MAX) return {};
    extra_uav_indices_.push_back(index);
    if (!layers) layers = array_layers_ - layer;
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = DX12Backend::to_dxgi_uav_format(info_.format);
    if (resource_->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; desc.Texture3D = {mip, 0, mip_extent(mip).depth};
    } else if (array_layers_ > 1 || array_view) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray = {mip, layer, layers, 0};
    } else { desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; desc.Texture2D.MipSlice = mip; }
    backend->get_device()->CreateUnorderedAccessView(resource_.Get(), nullptr, &desc, allocator.get_cpu_handle(index));
    view_cache_[key] = allocator.get_cpu_handle(index);
    return allocator.get_cpu_handle(index);
}

DX12TextureView::DX12TextureView(const RHITextureViewInfo& info, std::shared_ptr<DX12Backend> backend) : RHITextureView(info), backend_(backend) {
    auto* texture = get_dx12_texture();
    const auto& resource_info = texture->get_info();
    if (info_.format == FORMAT_UKNOWN) info_.format = resource_info.format;
    auto& range = info_.subresource;
    if (!range.level_count) range.level_count = resource_info.mip_levels - range.base_mip_level;
    if (!range.layer_count) range.layer_count = texture->get_array_layers() - range.base_array_layer;
    if (resource_info.type & RESOURCE_TYPE_RENDER_TARGET)
        rtv_handle_ = texture->get_rtv(range.base_mip_level, range.base_array_layer, range.layer_count);
    if (resource_info.type & RESOURCE_TYPE_DEPTH_STENCIL) {
        dsv_handle_ = texture->get_dsv(false, range.base_mip_level, range.base_array_layer, range.layer_count);
        dsv_ro_handle_ = texture->get_dsv(true, range.base_mip_level, range.base_array_layer, range.layer_count);
    }
    if (resource_info.type & (RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_DEPTH_STENCIL))
        srv_handle_ = texture->get_srv(info_.view_type, range.base_mip_level, range.level_count, range.base_array_layer, range.layer_count);
    if (resource_info.type & RESOURCE_TYPE_RW_TEXTURE)
        uav_handle_ = texture->get_uav(range.base_mip_level, range.base_array_layer, range.layer_count, info_.view_type == VIEW_TYPE_2D_ARRAY);
}
DX12TextureView::~DX12TextureView() { destroy(); }
void DX12TextureView::destroy() {
    if (auto backend = backend_.lock()) backend->free_imgui_srv_index(imgui_srv_index_);
    imgui_srv_index_ = UINT32_MAX; imgui_gpu_handle_ = {};
}
void* DX12TextureView::raw_handle() {
    auto backend = backend_.lock();
    if (!backend || !srv_handle_.ptr) return nullptr;
    if (imgui_srv_index_ == UINT32_MAX) {
        imgui_srv_index_ = backend->allocate_imgui_srv_index();
        if (imgui_srv_index_ == UINT32_MAX) return nullptr;
        auto& heap = backend->get_gpu_srv_uav_heap();
        backend->get_device()->CopyDescriptorsSimple(1, heap.get_cpu_handle(imgui_srv_index_), srv_handle_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        imgui_gpu_handle_ = heap.get_gpu_handle(imgui_srv_index_);
    }
    return reinterpret_cast<void*>(imgui_gpu_handle_.ptr);
}
RHITextureViewRef DX12Backend::create_texture_view(const RHITextureViewInfo& info) {
    if (!info.texture) return nullptr;
    const auto& texture = info.texture->get_info();
    const auto& range = info.subresource;
    if (range.base_mip_level >= texture.mip_levels || range.level_count > texture.mip_levels - range.base_mip_level ||
        range.base_array_layer >= texture.array_layers || range.layer_count > texture.array_layers - range.base_array_layer) return nullptr;
    auto view = std::make_shared<DX12TextureView>(info, shared_from_this());
    register_resource(view);
    return view;
}

DX12Sampler::DX12Sampler(const RHISamplerInfo& info, std::shared_ptr<DX12Backend> backend) : RHISampler(info), backend_(backend) {}
DX12Sampler::~DX12Sampler() { destroy(); }
bool DX12Sampler::init() {
    auto backend = backend_.lock(); if (!backend) return false;
    auto& allocator = backend->get_cpu_sampler_allocator();
    descriptor_index_ = allocator.allocate(); if (descriptor_index_ == UINT32_MAX) return false;
    handle_ = allocator.get_cpu_handle(descriptor_index_);
    constexpr D3D12_TEXTURE_ADDRESS_MODE addresses[] = {D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_BORDER};
    D3D12_SAMPLER_DESC desc = {};
    desc.AddressU = addresses[info_.address_mode_u]; desc.AddressV = addresses[info_.address_mode_v]; desc.AddressW = addresses[info_.address_mode_w];
    auto reduction = info_.compare_function != COMPARE_FUNCTION_NEVER ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON :
        info_.reduction_mode == SAMPLER_REDUCTION_MODE_MIN ? D3D12_FILTER_REDUCTION_TYPE_MINIMUM :
        info_.reduction_mode == SAMPLER_REDUCTION_MODE_MAX ? D3D12_FILTER_REDUCTION_TYPE_MAXIMUM : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(info_.min_filter == FILTER_TYPE_LINEAR ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        info_.mag_filter == FILTER_TYPE_LINEAR ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        info_.mipmap_mode == MIPMAP_MODE_LINEAR ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, reduction);
    if (info_.max_anisotropy > 1) desc.Filter = D3D12_ENCODE_ANISOTROPIC_FILTER(reduction);
    desc.MaxAnisotropy = std::clamp(UINT(info_.max_anisotropy), 1u, 16u);
    constexpr D3D12_COMPARISON_FUNC comparisons[] = {D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_GREATER,
        D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_EQUAL, D3D12_COMPARISON_FUNC_NOT_EQUAL, D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_ALWAYS};
    desc.ComparisonFunc = comparisons[info_.compare_function];
    desc.MipLODBias = info_.mip_lod_bias; desc.MaxLOD = D3D12_FLOAT32_MAX;
    backend->get_device()->CreateSampler(&desc, handle_);
    return true;
}
void DX12Sampler::destroy() {
    if (auto backend = backend_.lock()) backend->get_cpu_sampler_allocator().free(descriptor_index_);
    descriptor_index_ = UINT32_MAX; handle_ = {};
}
