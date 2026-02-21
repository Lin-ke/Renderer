#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stack>
#include <string>
#include <vector>

using TimePoint = std::chrono::steady_clock::time_point;

/**
 * @brief Single time scope for profiling
 */
class TimeScope {
public:
    TimeScope() { clear(); }
    ~TimeScope() = default;

    std::string name;

    void clear();
    void begin();
    void end();
    float get_microseconds() const;
    float get_milliseconds() const;
    float get_seconds() const;

    TimePoint get_begin_time() const { return begin_; }
    TimePoint get_end_time() const { return end_; }
    uint32_t get_depth() const { return depth_; }

private:
    TimePoint begin_;
    TimePoint end_;
    std::chrono::microseconds duration_;
    uint32_t depth_ = 0;
    friend class TimeScopes;
};

/**
 * @brief Collection of time scopes for a profiling session
 */
class TimeScopes {
public:
    TimeScopes() = default;
    ~TimeScopes() = default;

    void push_scope(const std::string& name);
    void pop_scope();
    void clear();
    bool valid() const;
    const std::vector<std::shared_ptr<TimeScope>>& get_scopes() const;
    const TimePoint& get_begin_time() const { return begin_; }
    const TimePoint& get_end_time() const { return end_; }
    uint32_t size() const { return static_cast<uint32_t>(scopes_.size()); }

private:
    std::vector<std::shared_ptr<TimeScope>> scopes_;
    std::stack<std::shared_ptr<TimeScope>> running_scopes_;
    uint32_t depth_ = 0;
    TimePoint begin_ = std::chrono::steady_clock::now();
    TimePoint end_ = std::chrono::steady_clock::now();
};

/**
 * @brief RAII helper for automatic scope timing
 */
class TimeScopeHelper {
public:
    TimeScopeHelper(const std::string& name, TimeScopes* scopes)
        : scopes_(scopes) {
        if (scopes_) {
            scopes_->push_scope(name);
        }
    }

    ~TimeScopeHelper() {
        if (scopes_) {
            scopes_->pop_scope();
        }
    }

private:
    TimeScopes* scopes_;
};
