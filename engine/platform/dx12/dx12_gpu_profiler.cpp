#include "engine/platform/dx12/dx12_gpu_profiler.h"
#include "engine/core/log/Log.h"
#include "platform_rhi.h"
#include "engine/core/utils/profile_session.h"
#include <cereal/archives/json.hpp>
#include <sstream>

DECLARE_LOG_TAG(LogDX12GPUProfiler);
DEFINE_LOG_TAG(LogDX12GPUProfiler, "DX12GPUProfiler");

DX12GPUProfiler::DX12GPUProfiler(DX12Backend& backend, uint32_t max_scopes)
    : backend_(backend.weak_from_this()), device_(backend.get_device()), queue_(backend.get_command_queue()), max_scopes_(max_scopes) {
    if (!device_ || !queue_) {
        ERR(LogDX12GPUProfiler, "Created with null device or queue!");
        return;
    }

    uint32_t max_queries = max_scopes_ * 2 + 2;
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

        fq.readback_buffer = backend.allocate_memory(heap_props, buf_desc, D3D12_RESOURCE_STATE_COPY_DEST, "Readback");
        if (!fq.readback_buffer) return;

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
        fq.statistics_heap.Reset();
        fq.statistics_readback.Reset();
        fq.scopes.clear();
        fq.scope_count = 0;
        fq.active = false;
        fq.resolved = false;
    }
    initialized_ = false;
}

void DX12GPUProfiler::prepare_statistics(FrameQueries& frame) {
    frame.statistics_active = false;
    frame.statistics_resolved = false;
    frame.statistics_status = "not_requested";
    if (!ProfileSession::instance().generation()) return;
    frame.statistics_status = "query_heap_failed";
    if (!frame.statistics_heap) {
        D3D12_QUERY_HEAP_DESC heap{};
        heap.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
        heap.Count = max_scopes_;
        if (FAILED(device_->CreateQueryHeap(&heap, IID_PPV_ARGS(&frame.statistics_heap)))) return;
    }
    if (!frame.statistics_readback) {
        frame.statistics_status = "backend_unavailable";
        auto backend = backend_.lock();
        if (!backend) return;
        frame.statistics_status = "readback_allocation_failed";
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) * max_scopes_;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        frame.statistics_readback = backend->allocate_memory(heap, buffer, D3D12_RESOURCE_STATE_COPY_DEST, "Readback");
    }
    frame.statistics_active = bool(frame.statistics_readback);
    if (frame.statistics_active) frame.statistics_status = "no_scopes";
}

void DX12GPUProfiler::begin_frame() {
    if (!initialized_ || !enabled_ || !cmd_list_) return;

    auto& fq = frame_queries_[write_index_];
    fq.scope_count = 0;
    fq.active = true;
    fq.resolved = false;
    fq.frame_id = ProfileSession::context().frame_id;
    prepare_statistics(fq);
    skipped_scopes_ = 0;
    cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void DX12GPUProfiler::end_frame() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;

    if (cmd_list_) {
        cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd_list_->ResolveQueryData(
            fq.query_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            fq.scope_count * 2 + 2,
            fq.readback_buffer.Get(),
            0);
        fq.resolved = true;
        if (fq.statistics_active && fq.scope_count) {
            cmd_list_->ResolveQueryData(fq.statistics_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
                0, fq.scope_count, fq.statistics_readback.Get(), 0);
            fq.statistics_resolved = true;
        }
    }

    fq.active = false;
    frames_recorded_++;
    write_index_ = (write_index_ + 1) % FRAMES_IN_FLIGHT;
}

void DX12GPUProfiler::begin_scope(const std::string& name) {
    if (!initialized_ || !enabled_ || !cmd_list_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;
    if (fq.scope_count >= max_scopes_) { ++skipped_scopes_; return; }

    uint32_t scope_idx = fq.scope_count;
    uint32_t ts_idx = scope_idx * 2 + 2;

    fq.scopes[scope_idx].name = name;
    fq.scopes[scope_idx].pass_id = ProfileSession::context().pass_id;
    cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ts_idx);
    if (fq.statistics_active)
        cmd_list_->BeginQuery(fq.statistics_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, scope_idx);
    fq.scope_count++;
}

void DX12GPUProfiler::end_scope() {
    if (!initialized_ || !enabled_ || !cmd_list_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;
    if (skipped_scopes_) { --skipped_scopes_; return; }
    if (fq.scope_count == 0) return;

    uint32_t scope_idx = fq.scope_count - 1;
    uint32_t ts_idx = scope_idx * 2 + 3;

    if (fq.statistics_active)
        cmd_list_->EndQuery(fq.statistics_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, scope_idx);
    cmd_list_->EndQuery(fq.query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ts_idx);
}

void DX12GPUProfiler::collect_results() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[read_index_];
    if (!fq.resolved) return;

    uint64_t freq = 0;
    if (FAILED(queue_->GetTimestampFrequency(&freq)) || freq == 0) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    uint64_t* timestamps = nullptr;
    D3D12_RANGE read_range = { 0, sizeof(uint64_t) * (fq.scope_count * 2 + 2) };
    if (FAILED(fq.readback_buffer->Map(0, &read_range, reinterpret_cast<void**>(&timestamps)))) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    double frequency = static_cast<double>(freq);
    results_.clear();
    results_.reserve(fq.scope_count);
    total_frame_time_ms_ = timestamps[1] >= timestamps[0] ?
        float(double(timestamps[1] - timestamps[0]) * 1000.0 / frequency) : 0.0f;
    result_frame_id_ = fq.frame_id;
    D3D12_QUERY_DATA_PIPELINE_STATISTICS* statistics = nullptr;
    auto statistics_status = fq.statistics_status;
    if (fq.statistics_resolved) {
        D3D12_RANGE range{0, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) * fq.scope_count};
        if (FAILED(fq.statistics_readback->Map(0, &range, reinterpret_cast<void**>(&statistics)))) {
            statistics = nullptr;
            statistics_status = "readback_map_failed";
        } else statistics_status = "available";
    }

    for (uint32_t i = 0; i < fq.scope_count; ++i) {
        uint64_t ts_begin = timestamps[i * 2 + 2];
        uint64_t ts_end = timestamps[i * 2 + 3];
        float elapsed_ms = 0.0f;
        if (ts_begin != 0 && ts_end >= ts_begin) {
            elapsed_ms = static_cast<float>((static_cast<double>(ts_end - ts_begin) / frequency) * 1000.0);
        }
        const float offset_ms = ts_begin >= timestamps[0] ? float(double(ts_begin - timestamps[0]) * 1000.0 / frequency) : 0;
        results_.push_back({fq.scopes[i].name, elapsed_ms, fq.scopes[i].pass_id, offset_ms});
        if (statistics) {
            const auto& value = statistics[i];
            results_.back().pipeline_statistics = {true, value.IAVertices, value.IAPrimitives,
                value.VSInvocations, value.CPrimitives, value.PSInvocations, value.CSInvocations};
        }
    }

    D3D12_RANGE write_range = { 0, 0 };
    fq.readback_buffer->Unmap(0, &write_range);
    if (statistics) fq.statistics_readback->Unmap(0, &write_range);
    fq.resolved = false;

    if (ProfileSession::instance().generation()) {
        std::ostringstream output;
        { cereal::JSONOutputArchive archive(output);
          archive(cereal::make_nvp("frame_id", result_frame_id_), cereal::make_nvp("gpu_ms", total_frame_time_ms_),
              cereal::make_nvp("pipeline_statistics_status", statistics_status),
              cereal::make_nvp("passes", results_)); }
        ProfileSession::instance().add_gpu_frame(output.str());
    }
    update_smoothing();
    read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
}
