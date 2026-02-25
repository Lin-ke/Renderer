#include "gpu_profiler.h"

// ---------------------------------------------------------------------------
// Platform-independent smoothing (exponential moving average)
// ---------------------------------------------------------------------------

void GPUProfiler::update_smoothing() {
    if (smoothed_results_.size() != results_.size()) {
        // Structure changed, reset
        smoothed_results_ = results_;
        smoothed_total_ms_ = total_frame_time_ms_;
    } else {
        for (size_t i = 0; i < results_.size(); ++i) {
            smoothed_results_[i].name = results_[i].name;
            smoothed_results_[i].time_ms += kSmoothFactor * (results_[i].time_ms - smoothed_results_[i].time_ms);
        }
        smoothed_total_ms_ += kSmoothFactor * (total_frame_time_ms_ - smoothed_total_ms_);
    }
}
