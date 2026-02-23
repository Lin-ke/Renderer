#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <memory>
#include <bitset>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/render_resource/render_resource_manager.h"
#include "engine/core/utils/timer.h"
#include "engine/core/log/Log.h" // Include for DECLARE_LOG_TAG

DECLARE_LOG_TAG(LogEngine);

class Window;
class AssetManager;
class ThreadPool;
class World;

class EngineContext {
public:

    enum StartMode{
        Asset = 0,
        Render = 1,
        Window = 2,
        SingleThread = 4
    };

    enum ThreadRole {
        Unknown = 0,
        MainGame,
        Renderer,
        Worker
    };


    ~EngineContext();
	static void init(std::bitset<8> mode );
    static void exit();
	static void main_loop();
    
    static RHIBackendRef rhi();
    static AssetManager* asset();
    static ThreadPool* thread_pool();
    static RenderResourceManager* render_resource();
    static World* world();
    static class Window* window() { return instance_ ? instance_->window_.get() : nullptr; }
    static RenderSystem* render_system() { return instance_ ? instance_->render_system_.get() : nullptr; }
    static EngineContext& get() {
        return *instance_;
    }

    static ThreadRole get_thread_role() {
        return thread_role_;
    }
    void set_thread_role(ThreadRole role) {
        thread_role_ = role;
    }

    static uint32_t current_frame_index();

    /**
     * @brief Get the delta time (time since last frame in seconds)
     * @return Delta time in seconds
     */
    static float get_delta_time();

    /**
     * @brief Get the current tick count (total number of frames processed)
     * @return Current tick count
     */
    static uint32_t get_current_tick();

private:
	EngineContext();
    std::bitset<8> mode_;
	static std::unique_ptr<EngineContext> instance_;
    std::unique_ptr<class Window> window_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<AssetManager> asset_manager_;
    std::unique_ptr<RenderResourceManager> render_resource_manager_;
    std::unique_ptr<World> world_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<std::jthread> render_thread_;
	void main_loop_internal();

    static ThreadRole thread_role_;
    uint32_t current_frame_index_ = 0;
    uint32_t current_tick_ = 0;          // Total frame/tick count
    float delta_time_ = 0.0f;            // Time since last frame in seconds
    Timer timer_;                        // Timer for measuring delta time

    // Render Thread Synchronization
    std::mutex render_mutex_;
    std::condition_variable render_cv_;
    std::queue<RenderPacket> render_queue_;
    const size_t MAX_FRAMES_IN_FLIGHT = 3;
};

#endif