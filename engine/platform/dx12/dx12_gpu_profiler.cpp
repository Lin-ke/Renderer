#include "engine/platform/dx12/dx12_gpu_profiler.h"
#include "engine/core/log/Log.h"

DECLARE_LOG_TAG(LogDX12GPUProfiler);
DEFINE_LOG_TAG(LogDX12GPUProfiler, "DX12GPUProfiler");

DX12GPUProfiler::DX12GPUProfiler(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* cmd_list, uint32_t max_scopes)
    : device_(device), queue_(queue), cmd_list_(cmd_list), max_scopes_(max_scopes) {
    if (!device_ || !queue_ || !cmd_list_) {
        ERR(LogDX12GPUProfiler, "Created with null device, queue, or command list!");
        return;
    }

    uint32_t max_queries = max_scopes_ * 2;
    for (auto& fq : frame_queries_) {
        D3D12_QUERY_HEAP_DESC heap_desc = {};
        heap_desc.Count = max_queries;
        heap_desc.NodeMask = 0;
        heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

        if (FAILED(device_->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&fq.query_heap)))) {
            ERR(LogDX12GPUProfiler, "Failed to create TIMESTAMP query heap");
            return;
        }

        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC buf_desc = {};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = sizeof(uint64_t) * max_queries;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.Format = DXGI_FORMAT_UNKNOWN;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buf_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (FAILED(device_->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&fq.readback_buffer)))) {
            ERR(LogDX12GPUProfiler, "Failed to create TIMESTAMP readback buffer");
            return;
        }

        fq.scopes.resize(max_scopes_);
    }

    initialized_ = true;
}

DX12GPUProfiler::~DX12GPUProfiler() {
    destroy();
}

void DX12GPUProfiler::destroy() {
    if (!initialized_) return;
    for (auto& fq : frame_queries_) {
        fq.query_heap.Reset();
        fq.readback_buffer.Reset();
        fq.scopes.clear();
        fq.scope_count = 0;
        fq.active = false;
        fq.resolved = false;
    }
    initialized_ = false;
}

void DX12GPUProfiler::begin_frame() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    fq.scope_count = 0;
    fq.active = true;
    fq.resolved = false;
}

void DX12GPUProfiler::end_frame() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;

    if (fq.scope_count > 0 && cmd_list_) {
        cmd_list_->ResolveQueryData(
            fq.query_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            fq.scope_count * 2,
            fq.readback_buffer.Get(),
            0);
        fq.resolved = true;
    }

    fq.active = false;
    frames_recorded_++;
    write_index_ = (write_index_ + 1) % FRAMES_IN_FLIGHT;
}

void DX12GPUProfiler::begin_scope(const std::string& name) {
    if (!initialized_ || !enabled_ || !cmd_list_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active || fq.scope_count >= max_scopes_) return;

    uint32_t scope_idx = fq.scope_count;
    uint32_t ts_idx = scope_idx * 2;

    fq.scopes[scope_idx].name = name;
    cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ts_idx);
    fq.scope_count++;
}

void DX12GPUProfiler::end_scope() {
    if (!initialized_ || !enabled_ || !cmd_list_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active || fq.scope_count == 0) return;

    uint32_t scope_idx = fq.scope_count - 1;
    uint32_t ts_idx = scope_idx * 2 + 1;

    cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ts_idx);
}

void DX12GPUProfiler::collect_results() {
    if (!initialized_ || !enabled_) return;
    if (frames_recorded_ < FRAMES_IN_FLIGHT) return;

    auto& fq = frame_queries_[read_index_];
    if (fq.scope_count == 0 || !fq.resolved) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    uint64_t freq = 0;
    if (FAILED(queue_->GetTimestampFrequency(&freq)) || freq == 0) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    uint64_t* timestamps = nullptr;
    D3D12_RANGE read_range = { 0, sizeof(uint64_t) * fq.scope_count * 2 };
    if (FAILED(fq.readback_buffer->Map(0, &read_range, reinterpret_cast<void**>(&timestamps)))) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    double frequency = static_cast<double>(freq);
    results_.clear();
    results_.reserve(fq.scope_count);
    total_frame_time_ms_ = 0.0f;

    for (uint32_t i = 0; i < fq.scope_count; ++i) {
        uint64_t ts_begin = timestamps[i * 2];
        uint64_t ts_end = timestamps[i * 2 + 1];
        float elapsed_ms = 0.0f;
        if (ts_end >= ts_begin) {
            elapsed_ms = static_cast<float>((static_cast<double>(ts_end - ts_begin) / frequency) * 1000.0);
        }
        results_.push_back({ fq.scopes[i].name, elapsed_ms });
        total_frame_time_ms_ += elapsed_ms;
    }

    D3D12_RANGE write_range = { 0, 0 };
    fq.readback_buffer->Unmap(0, &write_range);
    fq.resolved = false;

    update_smoothing();
    read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
}
