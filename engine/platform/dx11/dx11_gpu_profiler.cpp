#include "dx11_gpu_profiler.h"
#include "engine/core/log/Log.h"

DECLARE_LOG_TAG(LogDX11GPUProfiler);
DEFINE_LOG_TAG(LogDX11GPUProfiler, "DX11GPUProfiler");

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

DX11GPUProfiler::DX11GPUProfiler(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t max_scopes)
    : device_(device), context_(context), max_scopes_(max_scopes)
{
    if (!device_ || !context_) {
        ERR(LogDX11GPUProfiler, "Created with null device or context!");
        return;
    }

    for (auto& fq : frame_queries_) {
        // Disjoint query
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        HRESULT hr = device_->CreateQuery(&desc, fq.disjoint_query.GetAddressOf());
        if (FAILED(hr)) {
            ERR(LogDX11GPUProfiler, "Failed to create TIMESTAMP_DISJOINT query (hr=0x{:X})", (unsigned)hr);
            return;
        }

        // Timestamp queries (2 per scope: begin + end)
        fq.timestamp_queries.resize(max_scopes * 2);
        fq.scopes.resize(max_scopes);
        desc.Query = D3D11_QUERY_TIMESTAMP;
        for (uint32_t i = 0; i < max_scopes * 2; ++i) {
            hr = device_->CreateQuery(&desc, fq.timestamp_queries[i].GetAddressOf());
            if (FAILED(hr)) {
                ERR(LogDX11GPUProfiler, "Failed to create TIMESTAMP query {} (hr=0x{:X})", i, (unsigned)hr);
                return;
            }
        }
    }

    initialized_ = true;
    INFO(LogDX11GPUProfiler, "DX11 GPU Profiler initialized (max_scopes={}, frames_in_flight={})", max_scopes, FRAMES_IN_FLIGHT);
}

DX11GPUProfiler::~DX11GPUProfiler() {
    destroy();
}

void DX11GPUProfiler::destroy() {
    if (!initialized_) return;
    for (auto& fq : frame_queries_) {
        fq.disjoint_query.Reset();
        fq.timestamp_queries.clear();
        fq.scopes.clear();
        fq.scope_count = 0;
        fq.active = false;
    }
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// Dynamic query growth
// ---------------------------------------------------------------------------

void DX11GPUProfiler::ensure_queries(FrameQueries& fq, uint32_t needed) {
    uint32_t current_count = static_cast<uint32_t>(fq.timestamp_queries.size());
    if (needed <= current_count) return;

    fq.timestamp_queries.resize(needed);
    fq.scopes.resize(needed / 2);

    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_TIMESTAMP;
    for (uint32_t i = current_count; i < needed; ++i) {
        device_->CreateQuery(&desc, fq.timestamp_queries[i].GetAddressOf());
    }
}

// ---------------------------------------------------------------------------
// Per-frame recording
// ---------------------------------------------------------------------------

void DX11GPUProfiler::begin_frame() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    fq.scope_count = 0;
    fq.active = true;

    context_->Begin(fq.disjoint_query.Get());
}

void DX11GPUProfiler::end_frame() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;

    context_->End(fq.disjoint_query.Get());
    fq.active = false;

    frames_recorded_++;
    write_index_ = (write_index_ + 1) % FRAMES_IN_FLIGHT;
}

void DX11GPUProfiler::begin_scope(const std::string& name) {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;

    uint32_t scope_idx = fq.scope_count;
    uint32_t ts_idx = scope_idx * 2;

    ensure_queries(fq, (scope_idx + 1) * 2);

    fq.scopes[scope_idx].name = name;
    context_->End(fq.timestamp_queries[ts_idx].Get()); // Record begin timestamp
    fq.scope_count++;
}

void DX11GPUProfiler::end_scope() {
    if (!initialized_ || !enabled_) return;

    auto& fq = frame_queries_[write_index_];
    if (!fq.active) return;
    if (fq.scope_count == 0) return;

    uint32_t scope_idx = fq.scope_count - 1;
    uint32_t ts_idx = scope_idx * 2 + 1;

    context_->End(fq.timestamp_queries[ts_idx].Get());
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

void DX11GPUProfiler::collect_results() {
    if (!initialized_ || !enabled_) return;
    if (frames_recorded_ < FRAMES_IN_FLIGHT) return;

    auto& fq = frame_queries_[read_index_];
    if (fq.scope_count == 0) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    // Try to get disjoint data
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
    HRESULT hr = context_->GetData(fq.disjoint_query.Get(), &disjoint_data, sizeof(disjoint_data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (hr == S_FALSE) return; // Not ready
    if (FAILED(hr) || disjoint_data.Disjoint) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    double frequency = static_cast<double>(disjoint_data.Frequency);
    if (frequency == 0.0) {
        read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
        return;
    }

    // Read all timestamp values
    results_.clear();
    results_.reserve(fq.scope_count);
    total_frame_time_ms_ = 0.0f;

    for (uint32_t i = 0; i < fq.scope_count; ++i) {
        uint64_t ts_begin = 0, ts_end = 0;

        hr = context_->GetData(fq.timestamp_queries[i * 2].Get(), &ts_begin, sizeof(ts_begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr != S_OK) continue;

        hr = context_->GetData(fq.timestamp_queries[i * 2 + 1].Get(), &ts_end, sizeof(ts_end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr != S_OK) continue;

        float elapsed_ms = static_cast<float>((static_cast<double>(ts_end - ts_begin) / frequency) * 1000.0);
        if (elapsed_ms < 0.0f) elapsed_ms = 0.0f;

        results_.push_back({ fq.scopes[i].name, elapsed_ms });
        total_frame_time_ms_ += elapsed_ms;
    }

    update_smoothing();

    read_index_ = (read_index_ + 1) % FRAMES_IN_FLIGHT;
}
