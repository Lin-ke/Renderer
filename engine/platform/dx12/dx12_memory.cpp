#include "platform_rhi.h"

#include <algorithm>
#include <utility>

DX12Allocation::DX12Allocation(std::weak_ptr<DX12Backend> backend, uint64_t id, ComPtr<ID3D12Resource> resource)
    : backend_(std::move(backend)), id_(id), resource_(std::move(resource)) {}
DX12Allocation::~DX12Allocation() { Reset(); }
DX12Allocation::DX12Allocation(DX12Allocation&& other) noexcept
    : backend_(std::move(other.backend_)), id_(std::exchange(other.id_, 0)), resource_(std::move(other.resource_)) {}
DX12Allocation& DX12Allocation::operator=(DX12Allocation&& other) noexcept {
    if (this != &other) {
        Reset(); backend_ = std::move(other.backend_);
        id_ = std::exchange(other.id_, 0); resource_ = std::move(other.resource_);
    }
    return *this;
}
void DX12Allocation::Reset() {
    if (!resource_) return;
    if (auto backend = backend_.lock()) backend->retire_memory(id_, std::move(resource_));
    else resource_.Reset();
    id_ = 0;
}

DX12Allocation DX12Backend::allocate_memory(const D3D12_HEAP_PROPERTIES& heap, const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES state, const char* category) {
    ComPtr<ID3D12Resource> resource;
    if (!device_ || FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
        nullptr, IID_PPV_ARGS(&resource)))) return {};
    return adopt_memory(std::move(resource), category, false);
}
DX12Allocation DX12Backend::adopt_memory(ComPtr<ID3D12Resource> resource, const char* category, bool external) {
    if (!resource) return {};
    std::scoped_lock lock(submission_mutex_);
    const auto description = resource->GetDesc();
    const auto bytes = device_->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
    const uint64_t id = next_allocation_id_++;
    memory_allocations_.emplace(id, RHIMemoryAllocation{id, bytes, "", category, external});
    allocation_resources_[resource.Get()] = id;
    if (!external) {
        allocated_bytes_ += bytes;
        peak_bytes_ = std::max(peak_bytes_, allocated_bytes_);
    }
    return DX12Allocation(shared_from_this(), id, std::move(resource));
}
void DX12Backend::retire_memory(uint64_t id, ComPtr<ID3D12Resource> resource) {
    // Producers transfer ownership only; object records are consumed on Device.
    std::scoped_lock lock(retired_memory_mutex_);
    retired_memory_.push_back({id, std::move(resource)});
}
void DX12Backend::collect_memory() {
    std::vector<RetiredMemory> completed;
    {
        std::scoped_lock lock(retired_memory_mutex_);
        completed.swap(retired_memory_);
    }
    const auto fence = internal_fence_ ? internal_fence_->GetCompletedValue() : UINT64_MAX;
    std::vector<RetiredMemory> pending;
    for (auto& entry : completed) {
        if (entry.fence == UINT64_MAX)
            entry.fence = internal_fence_value_ + (recording_contexts_.empty() ? 0 : 1);
        if (entry.fence > fence) { pending.push_back(std::move(entry)); continue; }
        auto found = memory_allocations_.find(entry.id);
        if (found == memory_allocations_.end()) continue;
        if (!found->second.external) allocated_bytes_ -= found->second.bytes;
        allocation_resources_.erase(entry.resource.Get());
        memory_allocations_.erase(found);
    }
    if (!pending.empty()) {
        std::scoped_lock lock(retired_memory_mutex_);
        for (auto& entry : pending) retired_memory_.push_back(std::move(entry));
    }
}
void DX12Backend::set_memory_cached(RHIResourceRef resource, bool cached) {
    if (!resource) return;
    std::scoped_lock lock(submission_mutex_);
    auto found = allocation_resources_.find(static_cast<ID3D12Resource*>(resource->raw_handle()));
    if (found != allocation_resources_.end()) memory_allocations_.at(found->second).cached = cached;
}
uint64_t DX12Backend::memory_id(RHIResourceRef resource) const {
    if (!resource) return 0;
    std::scoped_lock lock(submission_mutex_);
    auto found = allocation_resources_.find(static_cast<ID3D12Resource*>(resource->raw_handle()));
    return found == allocation_resources_.end() ? 0 : found->second;
}
RHIMemorySnapshot DX12Backend::get_memory_snapshot() {
    std::scoped_lock lock(submission_mutex_);
    RHIMemorySnapshot snapshot;
    snapshot.allocated_bytes = allocated_bytes_;
    snapshot.peak_bytes = peak_bytes_;
    for (const auto& [id, allocation] : memory_allocations_) {
        snapshot.allocations.push_back(allocation);
        if (allocation.external) snapshot.external_bytes += allocation.bytes;
        else if (allocation.cached) snapshot.cached_bytes += allocation.bytes;
        if (allocation.category == "Upload") snapshot.upload_bytes += allocation.bytes;
        if (allocation.category == "Readback") snapshot.readback_bytes += allocation.bytes;
    }
    {
        std::scoped_lock lock(retired_memory_mutex_);
        for (const auto& entry : retired_memory_) {
            auto found = memory_allocations_.find(entry.id);
            if (found != memory_allocations_.end() && !found->second.external)
                snapshot.pending_release_bytes += found->second.bytes;
        }
    }
    ComPtr<IDXGIAdapter3> adapter;
    if (adapter_ && SUCCEEDED(adapter_.As(&adapter))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
        if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
            SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal))) {
            snapshot.budget_available = true;
            snapshot.local_usage = local.CurrentUsage; snapshot.local_budget = local.Budget;
            snapshot.nonlocal_usage = nonlocal.CurrentUsage; snapshot.nonlocal_budget = nonlocal.Budget;
        }
    }
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return snapshot;
}
