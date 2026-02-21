#include "profiler.h"

Profiler& Profiler::get() {
    static Profiler instance;
    return instance;
}

void Profiler::begin_scope(const std::string& name) {
    if (!enabled_) return;
    
    auto thread_id = std::this_thread::get_id();
    std::shared_ptr<TimeScopes> scopes;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = thread_scopes_.find(thread_id);
        if (it == thread_scopes_.end()) {
            scopes = std::make_shared<TimeScopes>();
            thread_scopes_[thread_id] = scopes;
        } else {
            scopes = it->second;
        }
    }
    
    scopes->push_scope(name);
}

void Profiler::end_scope() {
    if (!enabled_) return;
    
    auto thread_id = std::this_thread::get_id();
    std::shared_ptr<TimeScopes> scopes;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = thread_scopes_.find(thread_id);
        if (it == thread_scopes_.end()) {
            return;
        }
        scopes = it->second;
    }
    
    scopes->pop_scope();
}

void Profiler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto thread_id = std::this_thread::get_id();
    auto it = thread_scopes_.find(thread_id);
    if (it != thread_scopes_.end()) {
        it->second->clear();
    }
}

void Profiler::end_frame() {
    if (!enabled_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Move current scopes to previous frame and clear for new frame
    for (auto& [thread_id, scopes] : thread_scopes_) {
        if (scopes->valid()) {
            scopes->clear();
        }
    }
}

const std::map<std::thread::id, std::shared_ptr<TimeScopes>>& Profiler::get_all_scopes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_scopes_;
}

std::shared_ptr<TimeScopes> Profiler::get_current_thread_scopes() {
    if (!enabled_) return nullptr;
    
    auto thread_id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = thread_scopes_.find(thread_id);
    if (it != thread_scopes_.end()) {
        return it->second;
    }
    return nullptr;
}
