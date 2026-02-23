#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <concepts>
#include <memory>
#include <optional>

class ThreadPool {
public:
    static int get_thread_id() {
        static thread_local int id = next_id_++;
        return id;
    }

    explicit ThreadPool(size_t thread_count);
    
    ~ThreadPool() {
        {
            std::unique_lock lock(queue_mutex_);
            stop_flag_ = true;
        }
        condition_.notify_all();
    }
    
    template<class F, class... Args>
    requires std::invocable<F, Args...>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                return func(std::forward<decltype(args)>(args)...);
            });

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock lock(queue_mutex_);
            if (stop_flag_) {
                lock.unlock();
                (*task)(); 
                return res;
            }
            
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }
    
private:
    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_flag_ = false;
    static std::atomic<int> next_id_;
};
