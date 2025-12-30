#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <concepts>
#include <memory>

class ThreadPool {
public:
    // 构造函数：启动指定数量的线程
    explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < thread_count; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(queue_mutex);
                        // 等待直到有任务或停止信号
                        condition.wait(lock, [this] { return stop_flag || !tasks.empty(); });
                        
                        if (stop_flag && tasks.empty()) return;
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task(); // 执行任务
                }
            });
        }
    }

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
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock lock(queue_mutex);
            if (stop_flag) throw std::runtime_error("enqueue on stopped ThreadPool");
            
            // 将任务封装进 lambda 以存入 void() 类型的队列中
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
};