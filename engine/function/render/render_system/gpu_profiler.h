#pragma once

#include "engine/configs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Result for a single GPU-timed scope (one RDG pass).
 */
struct GPUTimingResult {
    std::string name;
    float time_ms = 0.0f;  // Elapsed time in milliseconds
};

/**
 * @brief Abstract GPU time profiler interface.
 *
 * Platform backends (DX11, Vulkan, ...) provide a concrete subclass via
 * RHIBackend::create_gpu_profiler().  The base class owns the smoothing /
 * book-keeping logic that is identical across all backends.
 *
 * Usage:
 *  1. begin_frame() at the start of GPU command recording.
 *  2. begin_scope(name) / end_scope() for each scope to measure.
 *  3. end_frame() after the last scope.
 *  4. collect_results() each frame (internally delayed by FRAMES_IN_FLIGHT).
 */
class GPUProfiler {
public:
    GPUProfiler() = default;
    virtual ~GPUProfiler() = default;

    // --- Lifecycle (platform-specific) ---
    virtual void destroy() = 0;

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }

    // --- Per-frame recording (called on the render thread) ---
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void begin_scope(const std::string& name) = 0;
    virtual void end_scope() = 0;

    // --- Readback ---
    virtual void collect_results() = 0;

    // --- Result accessors (platform-independent) ---
    const std::vector<GPUTimingResult>& get_results() const { return results_; }
    float get_total_frame_time_ms() const { return total_frame_time_ms_; }

    const std::vector<GPUTimingResult>& get_smoothed_results() const { return smoothed_results_; }
    float get_smoothed_total_ms() const { return smoothed_total_ms_; }

protected:
    /**
     * @brief Called by subclass after raw results_ / total_frame_time_ms_ are
     *        populated.  Updates the exponential moving average.
     */
    void update_smoothing();

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
