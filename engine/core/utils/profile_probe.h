#pragma once

#include "cpu_profiler.h"

// Detailed probes are restricted to one selected frame so per-draw diagnostics
// do not fill the session buffer or perturb the steady-state sample window.
// UINT64_MAX selects loading work; per-draw probes require explicit opt-in.
class ProfileProbe {
public:
    static void set_enabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    static bool is_enabled() { return enabled_.load(std::memory_order_relaxed); }
    explicit ProfileProbe(const char* name, uint64_t frame_id = 30)
        : name_(name), context_(ProfileSession::context()) {
        if (context_.frame_id == frame_id && (frame_id == UINT64_MAX || enabled_.load(std::memory_order_relaxed))) generation_ = ProfileSession::instance().generation();
        if (generation_) start_ = CpuProfiler::get_timestamp();
    }
    ~ProfileProbe() {
        if (generation_) ProfileSession::instance().record(name_, start_, CpuProfiler::get_timestamp(), context_, generation_);
    }
    ProfileProbe(const ProfileProbe&) = delete;
    ProfileProbe& operator=(const ProfileProbe&) = delete;
private:
    inline static std::atomic<bool> enabled_{false};
    const char* name_;
    ProfileContext context_;
    uint64_t start_ = 0, generation_ = 0;
};
