#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <memory>
#include <bitset>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "engine/function/render/render_system.h"

class Window;
class RHI;
class AssetManager;
class ThreadPool;

class EngineContext {
public:

    enum StartMode{
        Asset_ = 0,
        Render_ = 1,
        Window_ = 2,
        Log_ = 3,
        Single_Thread_ = 4
    };

    enum ThreadRole {
        Unknown = 0,
        MainGame,
        Render,
        Worker
    };


    ~EngineContext();
	static void init(std::bitset<8> mode );
    static void exit();
	static void main_loop();
    
    static RHI* get_rhi();
    static AssetManager* asset();
    static ThreadPool* thread_pool();
    static EngineContext& get() {
        return *instance_;
    }

    static ThreadRole get_thread_role() {
        return thread_role_;
    }
    void set_thread_role(ThreadRole role) {
        thread_role_ = role;
    }

private:
	EngineContext();
    std::bitset<8> mode_;
	static std::unique_ptr<EngineContext> instance_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<AssetManager> asset_manager_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<std::jthread> render_thread_;
	void main_loop_internal();

    static ThreadRole thread_role_;

    // Render Thread Synchronization
    std::mutex render_mutex_;
    std::condition_variable render_cv_;
    std::queue<RenderPacket> render_queue_;
    const size_t MAX_FRAMES_IN_FLIGHT = 2;
};

#endif