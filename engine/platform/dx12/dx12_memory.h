#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>

class DX12Backend;

// Move-only ownership. Destruction posts native memory to the device retirement queue.
class DX12Allocation {
public:
    DX12Allocation() = default;
    DX12Allocation(std::weak_ptr<DX12Backend> backend, uint64_t id, Microsoft::WRL::ComPtr<ID3D12Resource> resource);
    ~DX12Allocation();
    DX12Allocation(DX12Allocation&& other) noexcept;
    DX12Allocation& operator=(DX12Allocation&& other) noexcept;
    DX12Allocation(const DX12Allocation&) = delete;
    DX12Allocation& operator=(const DX12Allocation&) = delete;
    ID3D12Resource* Get() const { return resource_.Get(); }
    ID3D12Resource* operator->() const { return Get(); }
    explicit operator bool() const { return Get() != nullptr; }
    uint64_t id() const { return id_; }
    void Reset();
private:
    std::weak_ptr<DX12Backend> backend_;
    uint64_t id_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
};
