#include "thread_pool.h"
#include "engine/main/engine_context.h"
std::atomic<int> ThreadPool::next_id{0};
ThreadPool::ThreadPool(size_t thread_count) {
    for (size_t i = 0; i < thread_count; ++i) {
        workers.emplace_back([this]() {
            EngineContext::get().set_thread_role(EngineContext::ThreadRole::Worker);
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(this->queue_mutex);
                    this->condition.wait(lock, [this]() { return this->stop_flag || !this->tasks.empty(); });
                    if (this->stop_flag && this->tasks.empty()) {
                        return;
                    }
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                task();
            }
        });
    }
}

