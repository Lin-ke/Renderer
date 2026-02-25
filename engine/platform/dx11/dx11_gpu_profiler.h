#pragma once

#include "engine/function/render/render_system/gpu_profiler.h"

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

/**
 * @brief DX11 implementation of GPUProfiler using D3D11 timestamp queries.
 *
 * Uses D3D11_QUERY_TIMESTAMP_DISJOINT to validate results,
 * and D3D11_QUERY_TIMESTAMP pairs (begin+end) for each scope.
 * Triple-buffered (FRAMES_IN_FLIGHT) to avoid CPU stalls.
 */
class DX11GPUProfiler : public GPUProfiler {
public:
    DX11GPUProfiler(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t max_scopes = 64);
    ~DX11GPUProfiler() override;

    void destroy() override;

    void begin_frame() override;
    void end_frame() override;
    void begin_scope(const std::string& name) override;
    void end_scope() override;

    void collect_results() override;

private:
    struct ScopeInfo {
        std::string name;
    };

    struct FrameQueries {
        ComPtr<ID3D11Query> disjoint_query;
        std::vector<ComPtr<ID3D11Query>> timestamp_queries; // 2 per scope
        std::vector<ScopeInfo> scopes;
        uint32_t scope_count = 0;
        bool active = false;
    };

    void ensure_queries(FrameQueries& fq, uint32_t needed);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    uint32_t max_scopes_ = 64;
    bool initialized_ = false;

    std::array<FrameQueries, FRAMES_IN_FLIGHT> frame_queries_{};
    uint32_t write_index_ = 0;
    uint32_t read_index_ = 0;
    uint32_t frames_recorded_ = 0;
};
