#pragma once

#include "engine/function/render/render_system/gpu_profiler.h"

#include "dx12_memory.h"
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class DX12GPUProfiler : public GPUProfiler {
public:
    DX12GPUProfiler(DX12Backend& backend, uint32_t max_scopes = 64);
    ~DX12GPUProfiler() override;

    void destroy() override;

    void begin_frame() override;
    void end_frame() override;
    void begin_scope(const std::string& name) override;
    void end_scope() override;

    void collect_results() override;
    void set_command_list(ID3D12GraphicsCommandList* cmd_list) { cmd_list_ = cmd_list; }

private:
    struct ScopeInfo {
        std::string name;
        uint32_t pass_id = UINT32_MAX;
    };

    struct FrameQueries {
        ComPtr<ID3D12QueryHeap> query_heap;
        DX12Allocation readback_buffer;
        ComPtr<ID3D12QueryHeap> statistics_heap;
        DX12Allocation statistics_readback;
        uint64_t frame_id = UINT64_MAX;
        std::vector<ScopeInfo> scopes;
        uint32_t scope_count = 0;
        bool active = false;
        bool resolved = false;
        bool statistics_active = false;
        bool statistics_resolved = false;
    };

    void prepare_statistics(FrameQueries& frame);
    std::weak_ptr<DX12Backend> backend_;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12GraphicsCommandList* cmd_list_ = nullptr;
    uint32_t max_scopes_ = 64;
    bool initialized_ = false;

    std::array<FrameQueries, FRAMES_IN_FLIGHT> frame_queries_{};
    uint32_t write_index_ = 0;
    uint32_t read_index_ = 0;
    uint32_t frames_recorded_ = 0;
    uint32_t skipped_scopes_ = 0;
};
