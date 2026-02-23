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
 *   PROFILE_SCOPE("MyFunction");
 *   Profiler::get().begin_scope("MyScope");
 *   // ... code ...
 *   Profiler::get().end_scope();
 */
class Profiler {
public:
    static Profiler& get();

    
    void begin_scope(const std::string& name);
    
    
    void end_scope();
    
    
    void clear();
    
    
    void end_frame();
    
    
    const std::map<std::thread::id, std::shared_ptr<TimeScopes>>& get_all_scopes() const;
    
    
    std::shared_ptr<TimeScopes> get_current_thread_scopes();
    
    
    bool is_enabled() const { return enabled_; }
    
    
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


#define PROFILE_SCOPE(name) ProfileScopeHelper _profile_scope_##__LINE__(name)


#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)


#ifdef _MSC_VER
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCSIG__)
#else
    #define PROFILE_FUNCTION_FULL() PROFILE_SCOPE(__FUNCTION__)
#endif
