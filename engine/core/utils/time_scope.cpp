#include "time_scope.h"
#include "engine/core/log/Log.h"

#include <algorithm>

DEFINE_LOG_TAG(LogTimeScope, "TimeScope");

void TimeScope::clear() {
    begin_ = std::chrono::steady_clock::now();
    end_ = std::chrono::steady_clock::now();
    duration_ = std::chrono::microseconds::zero();
}

void TimeScope::begin() {
    begin_ = std::chrono::steady_clock::now();
}

void TimeScope::end() {
    end_ = std::chrono::steady_clock::now();
    auto diff = end_ - begin_;
    duration_ = std::chrono::duration_cast<std::chrono::microseconds>(diff);
}

float TimeScope::get_microseconds() const {
    return static_cast<float>(duration_.count());
}

float TimeScope::get_milliseconds() const {
    return static_cast<float>(duration_.count()) / 1000.0f;
}

float TimeScope::get_seconds() const {
    return static_cast<float>(duration_.count()) / 1000000.0f;
}

void TimeScopes::push_scope(const std::string& name) {
    auto new_scope = std::make_shared<TimeScope>();
    new_scope->name_ = name;
    new_scope->depth_ = depth_;
    new_scope->begin();

    scopes_.push_back(new_scope);
    running_scopes_.push(new_scope);

    depth_++;

    if (scopes_.size() == 1) {
        begin_ = new_scope->get_begin_time();
    }
}

void TimeScopes::pop_scope() {
    if (running_scopes_.empty()) {
        ERR(LogTimeScope, "Time scope popping is not valid - no running scope!");
        return;
    }

    auto scope = running_scopes_.top();
    running_scopes_.pop();
    scope->end();

    depth_--;

    if (depth_ == 0) {
        end_ = std::max(end_, scope->get_end_time());
    }
}

void TimeScopes::clear() {
    scopes_.clear();
    while (!running_scopes_.empty()) {
        running_scopes_.pop();
    }
    depth_ = 0;
    begin_ = std::chrono::steady_clock::now();
    end_ = std::chrono::steady_clock::now();
}

bool TimeScopes::valid() const {
    return depth_ == 0;
}

const std::vector<std::shared_ptr<TimeScope>>& TimeScopes::get_scopes() const {
    return scopes_;
}
