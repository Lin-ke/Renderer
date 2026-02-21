#pragma once

#include "time_scope.h"
#include <map>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @brief Global profiler for multi-threaded performance analysis
 * 
 * Usage:
 *   // In your function
 *   PROFILE_SCOPE("MyFunction");
 *   
 *   // Or manual scope
 *   Profiler::get().begin_scope("MyScope");
 *   // ... code ...
 *   Profiler::get().end_scope();
 */
class Profiler {
public:
    static Profiler& get();

    // Begin a new time scope (call at start of timed section)
    void begin_scope(const std::string& name);
    
    // End current time scope (call at end of timed section)
    void end_scope();
    
    // Clear all recorded scopes for current thread
    void clear();
    
    // Finish current frame and prepare for next (call once per frame)
    void end_frame();
    
    // Get time scopes for all threads
    const std::map<std::thread::id, std::shared_ptr<TimeScopes>>& get_all_scopes() const;
    
    // Get time scopes for current thread
    std::shared_ptr<TimeScopes> get_current_thread_scopes();
    
    // Check if profiling is enabled
    bool is_enabled() const { return enabled_; }
    
    // Enable/disable profiling
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    Profiler() = default;
    ~Profiler() = default;
    
    mutable std::mutex mutex_;
    std::map<std::thread::id, std::shared_ptr<TimeScopes>> thread_scopes_;
    bool enabled_ = true;
};

/**
 * @brief RAII helper for profiling a scope
 */
class ProfileScopeHelper {
public:
    ProfileScopeHelper(const std::string& name) {
        Profiler::get().begin_scope(name);
    }
    
    ~ProfileScopeHelper() {
        Profiler::get().end_scope();
    }
};

// Profile a scope with automatic timing
#define PROFILE_SCOPE(name) ProfileScopeHelper _profile_scope_##__LINE__(name)

// Profile a function with its name (function name only)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

// Profile a function with full signature (includes class name and parameters, MSVC only)
#ifdef _MSC_VER
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCSIG__)
#else
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCTION__)
#endif
