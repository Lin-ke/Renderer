#pragma once

#include "engine/configs.h"
#include <cereal/cereal.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GPUPipelineStatistics {
    bool available = false;
    uint64_t input_vertices = 0;
    uint64_t input_primitives = 0;
    uint64_t vertex_invocations = 0;
    uint64_t raster_primitives = 0;
    uint64_t pixel_invocations = 0;
    uint64_t compute_invocations = 0;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(available), CEREAL_NVP(input_vertices), CEREAL_NVP(input_primitives),
            CEREAL_NVP(vertex_invocations), CEREAL_NVP(raster_primitives),
            CEREAL_NVP(pixel_invocations), CEREAL_NVP(compute_invocations));
    }
};

/**
 * @brief Result for a single GPU-timed scope (one RDG pass).
 */
struct GPUTimingResult {
    std::string name;
    float time_ms = 0.0f;  // Elapsed time in milliseconds
    uint32_t pass_id = UINT32_MAX;
    float offset_ms = 0.0f;
    GPUPipelineStatistics pipeline_statistics;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(name), CEREAL_NVP(time_ms), CEREAL_NVP(pass_id), CEREAL_NVP(offset_ms),
            CEREAL_NVP(pipeline_statistics));
    }
};

/**
 * @brief Abstract GPU time profiler interface.
 *
 * The platform backend provides a concrete subclass via
 * RHIBackend::create_gpu_profiler().  The base class owns the smoothing /
 * book-keeping logic that is identical across all backends.
 *
 * Usage:
 *  1. begin_frame() at the start of GPU command recording.
 *  2. begin_scope(name) / end_scope() for each scope to measure.
 *  3. end_frame() after the last scope.
 *  4. collect_results() once the recorded submission has completed.
 */
class GPUProfiler {
public:
    GPUProfiler() = default;
    virtual ~GPUProfiler() = default;

    // --- Lifecycle (platform-specific) ---
    virtual void destroy() = 0;

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }

    // --- Per-frame recording (called on the device thread) ---
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void begin_scope(const std::string& name) = 0;
    virtual void end_scope() = 0;

    // --- Readback ---
    virtual void collect_results() = 0;

    // --- Result accessors (platform-independent) ---
    const std::vector<GPUTimingResult>& get_results() const { return results_; }
    uint64_t get_result_frame_id() const { return result_frame_id_; }
    float get_total_frame_time_ms() const { return total_frame_time_ms_; }

    const std::vector<GPUTimingResult>& get_smoothed_results() const { return smoothed_results_; }
    float get_smoothed_total_ms() const { return smoothed_total_ms_; }

protected:
    /**
     * @brief Called by subclass after raw results_ / total_frame_time_ms_ are
     *        populated.  Updates the exponential moving average.
     */
    void update_smoothing();

    uint64_t result_frame_id_ = UINT64_MAX;
    bool enabled_ = true;

    // Raw results from the last successful readback.
    std::vector<GPUTimingResult> results_;
    float total_frame_time_ms_ = 0.0f;

    // Smoothed results (exponential moving average).
    std::vector<GPUTimingResult> smoothed_results_;
    float smoothed_total_ms_ = 0.0f;
    static constexpr float kSmoothFactor = 0.1f;
};

using GPUProfilerRef = std::unique_ptr<GPUProfiler>;
