#include "engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/core/os/thread_pool.h"
#include "engine/core/window/window.h"
#include "engine/core/utils/profiler.h"
#include "engine/core/utils/cpu_profiler.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"

#include <windows.h>
#include <chrono>
#include <future>

DEFINE_LOG_TAG(LogEngine, "Engine");

/**
 * @brief Set Windows console and system to UTF-8 mode for Unicode support
 */
static void setup_utf8_locale() {
    // Set console output code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    // Set console input code page to UTF-8  
    SetConsoleCP(CP_UTF8);
    // Set system locale to UTF-8 (Windows 10 1903+)
    SetThreadPreferredUILanguages(MUI_CONSOLE_FILTER, L"en-US", nullptr);
}

std::unique_ptr<EngineContext> EngineContext::instance_;
thread_local EngineContext::ThreadRole EngineContext::thread_role_ = EngineContext::ThreadRole::Unknown;
EngineContext::EngineContext() {}
EngineContext::~EngineContext() {}

void EngineContext::init(std::bitset<8> mode) {
	// Setup UTF-8 locale for Unicode support (must be before Log::init)
	setup_utf8_locale();
	
	Log::init();
	instance_.reset(new EngineContext());
	instance_->mode_ = mode;
	instance_->set_thread_role(ThreadRole::MainGame);
	if (mode.test(StartMode::Asset)) { // Asset
		instance_->asset_manager_ = std::make_unique<AssetManager>();
	}
	// World is always created for scene management
	instance_->world_ = std::make_unique<World>();
	instance_->world_->init();
	if (mode.test(StartMode::Window)) { // Window
		instance_->window_ = std::make_unique<class Window>(1280, 720, L"Toy Renderer", !mode.test(StartMode::HiddenWindow));
		if (instance_->window_ && instance_->window_->get_hwnd()) {
			INFO(LogEngine, "Window created successfully");
		} else {
			ERR(LogEngine, "Failed to create window!");
		}
	}
	if (mode.test(StartMode::Render)) {
		instance_->render_system_ = std::make_unique<RenderSystem>();
		void* hwnd = instance_->window_ ? instance_->window_->get_hwnd() : nullptr;
		INFO(LogEngine, "Initializing RenderSystem with hwnd={}", hwnd);
		instance_->render_system_->init(hwnd); 
		instance_->render_resource_manager_ = std::make_unique<RenderResourceManager>();
		instance_->render_resource_manager_->init();
	}
	// Register main game thread with profiler
	CpuProfiler::instance().initialize();
	CpuProfiler::instance().register_thread("Main Game");

	// workers
	instance_->thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
	
    if (mode.test(StartMode::Render) && !mode.test(StartMode::SingleThread)) {
        auto* context = instance_.get();
        context->device_thread_ = std::make_unique<std::jthread>([context] { context->device_loop(); });
        context->render_thread_ = std::make_unique<std::jthread>([context] { context->render_loop(); });
    }
}

static void dispatch_sent_window_messages() {
    // DXGI can send synchronous messages to the thread which owns the HWND.
    MSG message;
    PeekMessageW(&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
}

bool EngineContext::render_frame() {
    auto* context = instance_.get();
    if (!context || !context->render_system_) return false;
    auto* scene = context->world_ ? context->world_->get_active_scene() : nullptr;
    const uint64_t frame_id = context->next_frame_id_;
    auto profile_context = ProfileSession::context(); profile_context.frame_id = frame_id;
    ProfileContextScope profile_scope(profile_context);
    PROFILE_SCOPE("ExtractAndEnqueue");
    auto packet = context->render_system_->make_render_packet(scene, nullptr, context->next_frame_id_++);
    context->current_frame_index_ = static_cast<uint32_t>(context->next_frame_id_ % FRAMES_IN_FLIGHT);
    if (!context->render_thread_) {
        return context->render_system_->tick(packet);
    }

    std::unique_lock lock(context->pipeline_mutex_);
    const auto wait_start = CpuProfiler::get_timestamp();
    while (context->pending_frames_ >= FRAMES_IN_FLIGHT && !context->render_failed_) {
        context->pipeline_cv_.wait_for(lock, std::chrono::milliseconds(1));
        lock.unlock();
        dispatch_sent_window_messages();
        lock.lock();
    }
    if (context->render_failed_) return false;
    const auto enqueued = CpuProfiler::get_timestamp();
    ProfileSession::instance().record("FrameBackpressure", wait_start, enqueued, profile_context, packet.profile_generation);
    packet.profile_queued_at = enqueued;
    context->render_queue_.push(std::move(packet));
    ++context->pending_frames_;
    lock.unlock();
    context->pipeline_cv_.notify_all();
    return true;
}

bool EngineContext::wait_for_render() {
    auto* context = instance_.get();
    if (!context || get_thread_role() == ThreadRole::Device) return true;
    std::unique_lock lock(context->pipeline_mutex_);
    while (context->pending_frames_) {
        context->pipeline_cv_.wait_for(lock, std::chrono::milliseconds(1));
        lock.unlock();
        if (get_thread_role() == ThreadRole::MainGame) dispatch_sent_window_messages();
        lock.lock();
    }
    const bool success = !context->render_failed_;
    lock.unlock();
    run_device_task([context] { context->render_system_->wait_for_gpu(); });
    return success;
}

void EngineContext::run_device_task(std::function<void()> task) {
    auto* context = instance_.get();
    if (!context || !context->device_thread_ || get_thread_role() == ThreadRole::Device) {
        task();
        return;
    }

    const auto profile_context = ProfileSession::context();
    auto pending = std::make_shared<std::packaged_task<void()>>([profile_context, task = std::move(task)] {
        ProfileContextScope scope(profile_context);
        PROFILE_SCOPE("DeviceTask");
        task();
    });
    auto completed = pending->get_future();
    {
        std::scoped_lock lock(context->pipeline_mutex_);
        context->device_queue_.emplace([pending] { (*pending)(); });
    }
    context->pipeline_cv_.notify_all();

    PROFILE_SCOPE("WaitDeviceTask");
    if (get_thread_role() == ThreadRole::MainGame) {
        while (completed.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            dispatch_sent_window_messages();
        }
    } else {
        completed.wait();
    }
}

void EngineContext::complete_render_frame(bool success) {
    {
        std::scoped_lock lock(pipeline_mutex_);
        render_failed_ |= !success;
        --pending_frames_;
    }
    pipeline_cv_.notify_all();
}

void EngineContext::render_loop() {
    set_thread_role(ThreadRole::Renderer);
    CpuProfiler::instance().register_thread("Render");
    INFO(LogEngine, "Render thread started.");
    for (;;) {
        RenderPacket packet;
        {
            std::unique_lock lock(pipeline_mutex_);
            pipeline_cv_.wait(lock, [this] { return pipeline_stopping_ || !render_queue_.empty(); });
            if (render_queue_.empty()) return;
            packet = std::move(render_queue_.front());
            render_queue_.pop();
        }
        ProfileContextScope scope({packet.frame_id, packet.profile_operation_id});
        if (packet.profile_queued_at) ProfileSession::instance().record("RenderQueue", packet.profile_queued_at,
            CpuProfiler::get_timestamp(), ProfileSession::context(), packet.profile_generation);
        auto frame = render_system_->build_frame(packet);
        if (!frame) {
            complete_render_frame(false);
            continue;
        }
        submit_frame(std::move(*frame));
    }
}

void EngineContext::submit_frame(RenderSystem::CompiledFrame frame) {
    frame.profile_queued_at = CpuProfiler::get_timestamp();
    auto compiled = std::make_shared<RenderSystem::CompiledFrame>(std::move(frame));
    {
        std::scoped_lock lock(pipeline_mutex_);
        device_queue_.emplace([this, compiled] {
            complete_render_frame(render_system_->execute_frame(std::move(*compiled)));
        });
    }
    pipeline_cv_.notify_all();
}

void EngineContext::device_loop() {
    set_thread_role(ThreadRole::Device);
    CpuProfiler::instance().register_thread("Device");
    INFO(LogEngine, "Device thread started.");
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(pipeline_mutex_);
            pipeline_cv_.wait(lock, [this] { return pipeline_stopping_ || !device_queue_.empty(); });
            if (device_queue_.empty()) return;
            task = std::move(device_queue_.front());
            device_queue_.pop();
        }
        task();
    }
}

void EngineContext::exit() {
	if (!instance_) {
		return;
	}

    wait_for_render();
    instance_->thread_pool_.reset();
    {
        std::scoped_lock lock(instance_->pipeline_mutex_);
        instance_->pipeline_stopping_ = true;
    }
    instance_->pipeline_cv_.notify_all();
    instance_->render_thread_.reset();
    instance_->device_thread_.reset();

	// Important: Destroy asset_manager BEFORE render_system to ensure all assets
	// (which may hold RHI resources) are destroyed while RHI is still valid
	if (instance_->render_resource_manager_) {
		instance_->render_resource_manager_->destroy();
		instance_->render_resource_manager_.reset();
	}

	if (instance_->world_) {
		instance_->world_->destroy();
		instance_->world_.reset();
	}

	if (instance_->asset_manager_) {
		instance_->asset_manager_.reset();
	}

	// Cleanup render system (includes RHI resources)
	if (instance_->render_system_) {
		instance_->render_system_->destroy();
		instance_->render_system_.reset();
	}
	
	instance_.reset();
	thread_role_ = ThreadRole::Unknown;
	Log::shutdown();
}

void EngineContext::main_loop() {
	if (instance_) {
		instance_->main_loop_internal();
	}
}

void EngineContext::main_loop_internal() {
	// Reset timer at start of main loop
	instance_->timer_.reset();
	
	while (true) {
		PROFILE_SCOPE("MainLoop_Frame");
		
		// Calculate delta time (time since last frame)
		instance_->delta_time_ = instance_->timer_.get_elapsed_sec();
		
		// Process window messages
		if (instance_->window_ && !instance_->window_->process_messages()) {
			break;
		}

		// System Ticks
		{
			PROFILE_SCOPE("MainLoop_Input");
			Input::get_instance().tick();
		}
		
		if (instance_->world_) {
			PROFILE_SCOPE("MainLoop_World");
			instance_->world_->tick(instance_->delta_time_);
		}
		
		if (instance_->asset_manager_) {
			PROFILE_SCOPE("MainLoop_AssetManager");
			instance_->asset_manager_->tick();
		}

        if (instance_->render_system_ && !render_frame()) break;

		// End frame for profiler (prepares for next frame)
		Profiler::get().end_frame();
		
		// Increment tick counter
		instance_->current_tick_++;
	}
}

RHIBackendRef EngineContext::rhi() {
	if (instance_ && instance_->render_system_) {
		return instance_->render_system_->get_rhi();
	}
	return nullptr;
}

uint32_t EngineContext::current_frame_index() {
    return instance_ ? instance_->current_frame_index_.load(std::memory_order_relaxed) : 0;
}

float EngineContext::get_delta_time() {
    return instance_ ? instance_->delta_time_ : 0.0f;
}

uint32_t EngineContext::get_current_tick() {
    return instance_ ? instance_->current_tick_ : 0;
}

AssetManager *EngineContext::asset() {
	if (instance_) {
		return instance_->asset_manager_.get();
	}
	return nullptr;
}

World* EngineContext::world() {
	if (instance_) {
		return instance_->world_.get();
	}
	return nullptr;
}

RenderResourceManager* EngineContext::render_resource() {
	if (instance_) {
		return instance_->render_resource_manager_.get();
	}
	return nullptr;
}

ThreadPool *EngineContext::thread_pool() {
	if (instance_) {
		return instance_->thread_pool_.get();
	}
	return nullptr;
}
