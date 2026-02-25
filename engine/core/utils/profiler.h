#pragma once

#include "cpu_profiler.h"

/**
 * @brief Thin wrapper that forwards the legacy Profiler API to CpuProfiler
 *
 * Usage (unchanged from before):
 *   PROFILE_SCOPE("MyFunction");
 *   Profiler::get().end_frame();
 */
class Profiler {
public:
    static Profiler& get() {
        static Profiler inst;
        return inst;
    }

    void begin_scope(const char* name, const char* file = nullptr, uint32_t line = 0) {
        CpuProfiler::instance().begin_scope(name, file, line);
    }

    void end_scope() {
        CpuProfiler::instance().end_scope();
    }

    void end_frame() {
        CpuProfiler::instance().end_frame();
        CpuProfiler::instance().begin_frame();
    }

    void clear() { /* no-op: history is managed internally */ }

    bool is_enabled() const { return CpuProfiler::instance().is_enabled(); }
    void set_enabled(bool e) { CpuProfiler::instance().set_enabled(e); }

private:
    Profiler() { CpuProfiler::instance().initialize(); }
    ~Profiler() = default;
};

/**
 * @brief RAII helper – records file + line through __FILE__ / __LINE__
 */
class ProfileScopeHelper {
public:
    ProfileScopeHelper(const char* name, const char* file, int line) {
        CpuProfiler::instance().begin_scope(name, file, static_cast<uint32_t>(line));
    }
    ~ProfileScopeHelper() {
        CpuProfiler::instance().end_scope();
    }
};

#define PROFILE_SCOPE(name) \
    ProfileScopeHelper _profile_scope_##__LINE__(name, __FILE__, __LINE__)

#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

#ifdef _MSC_VER
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCSIG__)
#else
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCTION__)
#endif
