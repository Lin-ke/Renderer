#ifndef THREAD_POOL_H
#define THREAD_POOL_H
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
        static thread_local int id = next_id++; // 注意这里懒汉式可能导致的行为（直到调用才确定id）
        return id;
    }
    // 构造函数：启动指定数量的线程
    explicit ThreadPool(size_t thread_count);

    
    // 析构函数：停止所有线程
    ~ThreadPool() {
        {
            std::unique_lock lock(queue_mutex);
            stop_flag = true;
        }
        condition.notify_all(); // 唤醒所有等待的线程
        // std::jthread 的析构函数会自动 join，这里不需要手动 join
    }
    
    // 提交任务 (使用 Concept 约束)
    template<class F, class... Args>
    requires std::invocable<F, Args...>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        
        // 将任务包装为 packaged_task 以获取 future
       auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
            return func(std::forward<decltype(args)>(args)...);
        });

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock lock(queue_mutex);
            if (stop_flag) {
                lock.unlock();
                (*task)(); 
                return res;
            }
            
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }
    
    private:
    std::vector<std::jthread> workers;          // 使用 jthread 自动管理
    std::queue<std::function<void()>> tasks;    // 任务队列
    std::mutex queue_mutex;                     // 互斥锁
    std::condition_variable condition;          // 条件变量
    bool stop_flag = false;                     // 停止标志
    static std::atomic<int> next_id; // 线程 ID 生成器
};

#endif // THREAD_POOL_H