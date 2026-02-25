#include "cpu_profiler.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <sstream>

// ============================================================================
// TLS definition
// ============================================================================
thread_local CpuProfileThreadBuffer* CpuProfiler::tls_buffer_ = nullptr;

// ============================================================================
// Timestamp helpers (platform-specific)
// ============================================================================
uint64_t CpuProfiler::get_timestamp() {
#ifdef _WIN32
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return static_cast<uint64_t>(li.QuadPart);
#else
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(now.time_since_epoch().count());
#endif
}

uint64_t CpuProfiler::get_cpu_frequency() {
#ifdef _WIN32
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return static_cast<uint64_t>(li.QuadPart);
#else
    return 1000000000ULL; // nanoseconds
#endif
}

uint64_t CpuProfiler::get_current_thread_id() {
#ifdef _WIN32
    return static_cast<uint64_t>(::GetCurrentThreadId());
#else
    std::hash<std::thread::id> hasher;
    return static_cast<uint64_t>(hasher(std::this_thread::get_id()));
#endif
}

// ============================================================================
// Singleton
// ============================================================================
CpuProfiler& CpuProfiler::instance() {
    static CpuProfiler inst;
    return inst;
}

// ============================================================================
// Lifecycle
// ============================================================================
void CpuProfiler::initialize() {
    if (initialized_.exchange(true)) return;
    cpu_frequency_ = get_cpu_frequency();
    frame_start_ticks_ = get_timestamp();
    frame_number_ = 0;
}

void CpuProfiler::shutdown() {
    if (!initialized_.exchange(false)) return;
    std::lock_guard<std::mutex> lock(thread_mutex_);
    thread_buffers_.clear();
    thread_names_.clear();
    tls_buffer_ = nullptr;
}

// ============================================================================
// Thread-local buffer access
// ============================================================================
CpuProfileThreadBuffer* CpuProfiler::get_thread_buffer() {
    if (tls_buffer_) return tls_buffer_;

    uint64_t tid = get_current_thread_id();
    std::lock_guard<std::mutex> lock(thread_mutex_);

    auto it = thread_buffers_.find(tid);
    if (it == thread_buffers_.end()) {
        auto buf = std::make_unique<CpuProfileThreadBuffer>();
        buf->thread_id = tid;
        tls_buffer_ = buf.get();
        thread_buffers_[tid] = std::move(buf);
    } else {
        tls_buffer_ = it->second.get();
    }
    return tls_buffer_;
}

// ============================================================================
// Thread registration
// ============================================================================
void CpuProfiler::register_thread(const char* name) {
    uint64_t tid = get_current_thread_id();
    std::lock_guard<std::mutex> lock(thread_mutex_);
    thread_names_[tid] = name ? name : "";
    // Ensure buffer exists
    if (thread_buffers_.find(tid) == thread_buffers_.end()) {
        auto buf = std::make_unique<CpuProfileThreadBuffer>();
        buf->thread_id = tid;
        tls_buffer_ = buf.get();
        thread_buffers_[tid] = std::move(buf);
    }
}

void CpuProfiler::unregister_thread() {
    uint64_t tid = get_current_thread_id();
    std::lock_guard<std::mutex> lock(thread_mutex_);
    thread_names_.erase(tid);
    // Don't erase the buffer yet – end_frame will collect remaining data
}

// ============================================================================
// Frame control
// ============================================================================
void CpuProfiler::begin_frame() {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (!initialized_.load(std::memory_order_relaxed)) initialize();
    frame_start_ticks_ = get_timestamp();
}

void CpuProfiler::end_frame() {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (!initialized_.load(std::memory_order_relaxed)) return;

    uint64_t frame_end = get_timestamp();
    float frame_ms = static_cast<float>(
        static_cast<double>(frame_end - frame_start_ticks_) * 1000.0 / static_cast<double>(cpu_frequency_));

    // --- update stats -------------------------------------------------------
    current_frame_time_ms_ = frame_ms;
    current_fps_ = frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f;

    // Exponential moving average for FPS (alpha ~0.05)
    constexpr float alpha = 0.05f;
    avg_fps_ = avg_fps_ * (1.0f - alpha) + current_fps_ * alpha;

    // --- record into frame-time ring ----------------------------------------
    frame_time_history_[frame_history_head_] = frame_ms;
    frame_history_head_ = (frame_history_head_ + 1) % FRAME_HISTORY_MAX;
    if (frame_history_count_ < FRAME_HISTORY_MAX) ++frame_history_count_;

    // --- threshold check ----------------------------------------------------
    bool passes_threshold = true;
    if (threshold_ms_ > 0.0f) {
        passes_threshold = (frame_ms >= threshold_ms_);
    }

    // --- collect scopes from all threads into a frame -----------------------
    if (!paused_.load(std::memory_order_relaxed) && passes_threshold) {
        CpuProfileFrame frame;
        frame.start_ticks  = frame_start_ticks_;
        frame.end_ticks    = frame_end;
        frame.cpu_frequency = cpu_frequency_;
        frame.duration_ms  = frame_ms;
        frame.frame_number = frame_number_;

        {
            std::lock_guard<std::mutex> lock(thread_mutex_);
            for (auto& [tid, buf] : thread_buffers_) {
                if (buf->scope_count == 0) continue;

                CpuProfileThreadData td;
                td.thread_id = tid;
                auto name_it = thread_names_.find(tid);
                if (name_it != thread_names_.end()) {
                    td.name = name_it->second;
                } else {
                    std::ostringstream oss;
                    oss << "Thread " << tid;
                    td.name = oss.str();
                }

                td.scopes.reserve(buf->scope_count);
                for (uint32_t i = 0; i < buf->scope_count; ++i) {
                    td.scopes.push_back(buf->scopes[i]);
                }

                // Close any still-open scopes with frame_end time
                for (uint32_t i = 0; i < buf->open_count; ++i) {
                    uint32_t idx = buf->open_stack[i];
                    if (idx < td.scopes.size()) {
                        td.scopes[idx].end_ticks = frame_end;
                    }
                }

                frame.threads.push_back(std::move(td));
            }
            // Reset all buffers for next frame
            for (auto& [tid, buf] : thread_buffers_) {
                buf->reset();
            }
        }

        // --- store into history + display -----------------------------------
        {
            std::lock_guard<std::mutex> flock(frame_mutex_);
            frame_history_.push_back(frame);
            if (frame_history_.size() > FRAME_HISTORY_MAX) {
                frame_history_.pop_front();
            }
            if (select_latest_) {
                display_frame_ = frame;
            }
        }
    } else {
        // Even if paused / below threshold, still reset buffers
        std::lock_guard<std::mutex> lock(thread_mutex_);
        for (auto& [tid, buf] : thread_buffers_) {
            buf->reset();
        }
    }

    ++frame_number_;
}

// ============================================================================
// Scope recording – hot path, no locks
// ============================================================================
void CpuProfiler::begin_scope(const char* name, const char* file, uint32_t line) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    auto* buf = get_thread_buffer();
    if (!buf) return;
    buf->begin_scope(name, file, line, get_timestamp());
}

void CpuProfiler::end_scope() {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (!tls_buffer_) return;
    tls_buffer_->end_scope(get_timestamp());
}

// ============================================================================
// History access
// ============================================================================
const CpuProfileFrame* CpuProfiler::get_history_frame(size_t index) const {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (index >= frame_history_.size()) return nullptr;
    return &frame_history_[index];
}

size_t CpuProfiler::get_history_size() const {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return frame_history_.size();
}

void CpuProfiler::select_history_frame(size_t index) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (index < frame_history_.size()) {
        display_frame_ = frame_history_[index];
        select_latest_ = false;
    }
}

void CpuProfiler::select_latest_frame() {
    select_latest_ = true;
}
