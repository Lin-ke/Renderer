#include "thread_pool.h"
#include "engine/main/engine_context.h"
#include <algorithm>

std::atomic<int> ThreadPool::next_id_{0};
ThreadPool::ThreadPool(size_t thread_count) {
    thread_count = std::max<size_t>(1, thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this]() {
            EngineContext::set_thread_role(EngineContext::ThreadRole::Worker);
            ProfileSession::instance().name_thread("Worker");
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(this->queue_mutex_);
                    this->condition_.wait(lock, [this]() { return this->stop_flag_ || !this->tasks_.empty(); });
                    if (this->stop_flag_ && this->tasks_.empty()) {
                        return;
                    }
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }
                task();
            }
        });
    }
}

