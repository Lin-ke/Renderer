#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// Scope entry: a single timed block recorded during profiling
// ============================================================================
struct CpuProfileScope {
    const char* name;           // scope / function name (static string)
    const char* file;           // source file path      (static string)
    uint32_t    line;           // source line number
    uint32_t    depth;          // nesting depth (0 = root)
    uint64_t    thread_id;      // owning thread
    uint64_t    start_ticks;    // QPC start
    uint64_t    end_ticks;      // QPC end

    float duration_ms(uint64_t freq) const {
        return freq > 0
            ? static_cast<float>(static_cast<double>(end_ticks - start_ticks) * 1000.0 / static_cast<double>(freq))
            : 0.0f;
    }
};

// ============================================================================
// Per-thread data inside a captured frame
// ============================================================================
struct CpuProfileThreadData {
    uint64_t    thread_id = 0;
    std::string name;
    std::vector<CpuProfileScope> scopes;
};

// ============================================================================
// One captured frame (immutable once finalized)
// ============================================================================
struct CpuProfileFrame {
    std::vector<CpuProfileThreadData> threads;
    uint64_t start_ticks  = 0;
    uint64_t end_ticks    = 0;
    uint64_t cpu_frequency = 0;
    float    duration_ms  = 0.0f;
    uint32_t frame_number = 0;

    void clear() {
        threads.clear();
        start_ticks = end_ticks = 0;
        duration_ms = 0.0f;
    }

    float ticks_to_ms(uint64_t ticks) const {
        return cpu_frequency > 0
            ? static_cast<float>(static_cast<double>(ticks) * 1000.0 / static_cast<double>(cpu_frequency))
            : 0.0f;
    }
};

// ============================================================================
// Thread-local write buffer – each thread owns one, no locking on hot path
// ============================================================================
struct CpuProfileThreadBuffer {
    static constexpr uint32_t MAX_SCOPES = 8192;
    static constexpr uint32_t MAX_STACK  = 256;

    CpuProfileScope scopes[MAX_SCOPES];
    uint32_t scope_count   = 0;
    uint32_t current_depth = 0;
    uint64_t thread_id     = 0;

    // Open-scope stack (indices into scopes[])
    uint32_t open_stack[MAX_STACK];
    uint32_t open_count = 0;

    void reset() {
        scope_count   = 0;
        current_depth = 0;
        open_count    = 0;
    }

    uint32_t begin_scope(const char* n, const char* f, uint32_t ln, uint64_t ts) {
        if (scope_count >= MAX_SCOPES) return UINT32_MAX;
        uint32_t idx = scope_count++;
        auto& s       = scopes[idx];
        s.name        = n;
        s.file        = f;
        s.line        = ln;
        s.depth       = current_depth;
        s.thread_id   = thread_id;
        s.start_ticks = ts;
        s.end_ticks   = ts;
        if (open_count < MAX_STACK) {
            open_stack[open_count++] = idx;
        }
        ++current_depth;
        return idx;
    }

    void end_scope(uint64_t ts) {
        if (open_count == 0) return;
        --open_count;
        scopes[open_stack[open_count]].end_ticks = ts;
        if (current_depth > 0) --current_depth;
    }
};

// ============================================================================
// CpuProfiler – singleton, multi-thread safe
// ============================================================================
class CpuProfiler {
public:
    static constexpr size_t FRAME_HISTORY_MAX = 300;

    static CpuProfiler& instance();

    // --- lifecycle ----------------------------------------------------------
    void initialize();
    void shutdown();

    // --- per-frame bookkeeping (call from main-loop thread) -----------------
    void begin_frame();
    void end_frame();

    // --- scope recording (called from ANY thread, lock-free) ----------------
    void begin_scope(const char* name, const char* file, uint32_t line);
    void end_scope();

    // --- thread registration ------------------------------------------------
    void register_thread(const char* name);
    void unregister_thread();

    // --- control ------------------------------------------------------------
    bool  is_paused()  const { return paused_.load(std::memory_order_relaxed); }
    void  set_paused(bool v) { paused_.store(v, std::memory_order_relaxed); }
    bool  is_enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void  set_enabled(bool v){ enabled_.store(v, std::memory_order_relaxed); }

    void  set_threshold(float ms, uint32_t level = 0) { threshold_ms_ = ms; threshold_level_ = level; }
    float get_threshold_ms()    const { return threshold_ms_; }
    uint32_t get_threshold_level() const { return threshold_level_; }

    // --- data access (read from render / UI thread) -------------------------

    /// Currently selected frame for display (thread-safe copy)
    const CpuProfileFrame& get_display_frame() const { return display_frame_; }

    /// Frame history ring (for bar chart). Returns (array, head, count).
    const std::array<float, FRAME_HISTORY_MAX>& get_frame_times()  const { return frame_time_history_; }
    size_t get_frame_history_head()  const { return frame_history_head_;  }
    size_t get_frame_history_count() const { return frame_history_count_; }

    /// Get a specific historical frame (0 = oldest stored). Returns nullptr if out of range.
    const CpuProfileFrame* get_history_frame(size_t index) const;
    size_t get_history_size() const;

    /// Select a history frame by index for display
    void select_history_frame(size_t index);
    /// Return to auto-selecting latest frame
    void select_latest_frame();
    bool is_selecting_latest() const { return select_latest_; }

    // --- stats convenience --------------------------------------------------
    float    get_average_fps()        const { return avg_fps_; }
    float    get_current_fps()        const { return current_fps_; }
    float    get_current_frame_time() const { return current_frame_time_ms_; }

    // --- timestamp helpers --------------------------------------------------
    static uint64_t get_timestamp();
    static uint64_t get_cpu_frequency();

private:
    CpuProfiler() = default;
    ~CpuProfiler() = default;

    CpuProfileThreadBuffer* get_thread_buffer();
    static uint64_t get_current_thread_id();

    // -- thread buffers (one per thread) ------------------------------------
    std::mutex thread_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<CpuProfileThreadBuffer>> thread_buffers_;
    std::unordered_map<uint64_t, std::string> thread_names_;

    // -- frame data ---------------------------------------------------------
    mutable std::mutex frame_mutex_;         // protects display_frame_ + history
    CpuProfileFrame    display_frame_;
    std::deque<CpuProfileFrame> frame_history_;

    // -- frame time ring for bar chart --------------------------------------
    std::array<float, FRAME_HISTORY_MAX> frame_time_history_{};
    size_t frame_history_head_  = 0;
    size_t frame_history_count_ = 0;

    // -- stats --------------------------------------------------------------
    float avg_fps_              = 0.0f;
    float current_fps_          = 0.0f;
    float current_frame_time_ms_= 0.0f;

    // -- timing -------------------------------------------------------------
    uint64_t frame_start_ticks_ = 0;
    uint64_t cpu_frequency_     = 0;
    uint32_t frame_number_      = 0;

    // -- threshold ----------------------------------------------------------
    float    threshold_ms_    = 0.0f;
    uint32_t threshold_level_ = 0;

    // -- state --------------------------------------------------------------
    std::atomic<bool> initialized_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> enabled_{true};
    bool              select_latest_ = true;

    // -- TLS ----------------------------------------------------------------
    static thread_local CpuProfileThreadBuffer* tls_buffer_;
};

// ============================================================================
// RAII scope helper
// ============================================================================
class CpuProfileScopeHelper {
public:
    CpuProfileScopeHelper(const char* file, int line, const char* name) {
        CpuProfiler::instance().begin_scope(name, file, static_cast<uint32_t>(line));
    }
    ~CpuProfileScopeHelper() {
        CpuProfiler::instance().end_scope();
    }
    CpuProfileScopeHelper(const CpuProfileScopeHelper&) = delete;
    CpuProfileScopeHelper& operator=(const CpuProfileScopeHelper&) = delete;
};

// ============================================================================
// Macros
// ============================================================================
#ifndef CPU_PROFILER_DISABLE

#define CPU_CONCAT_IMPL(x, y) x##y
#define CPU_CONCAT(x, y) CPU_CONCAT_IMPL(x, y)

#define CPU_PROFILE_SCOPE(name) \
    CpuProfileScopeHelper CPU_CONCAT(_cpu_prof_, __LINE__)(__FILE__, __LINE__, name)

#define CPU_PROFILE_FUNCTION() \
    CpuProfileScopeHelper CPU_CONCAT(_cpu_prof_, __LINE__)(__FILE__, __LINE__, __FUNCTION__)

#ifdef _MSC_VER
#define CPU_PROFILE_FUNCTION_FULL() \
    CpuProfileScopeHelper CPU_CONCAT(_cpu_prof_, __LINE__)(__FILE__, __LINE__, __FUNCSIG__)
#else
#define CPU_PROFILE_FUNCTION_FULL() CPU_PROFILE_FUNCTION()
#endif

#define CPU_PROFILE_REGISTER_THREAD(name) \
    CpuProfiler::instance().register_thread(name)

#define CPU_PROFILE_BEGIN_FRAME() \
    CpuProfiler::instance().begin_frame()

#else
#define CPU_PROFILE_SCOPE(name)             ((void)0)
#define CPU_PROFILE_FUNCTION()              ((void)0)
#define CPU_PROFILE_FUNCTION_FULL()         ((void)0)
#define CPU_PROFILE_REGISTER_THREAD(name)   ((void)0)
#define CPU_PROFILE_BEGIN_FRAME()           ((void)0)
#endif
