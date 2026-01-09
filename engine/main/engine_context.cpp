#include "engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/core/os/thread_pool.h"
#include "engine/core/window/window.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system.h"

DEFINE_LOG_TAG(LogEngine, "Engine");

std::unique_ptr<EngineContext> EngineContext::instance_;
EngineContext::ThreadRole EngineContext::thread_role_ = EngineContext::ThreadRole::Unknown;
EngineContext::EngineContext() {}
EngineContext::~EngineContext() {}

void EngineContext::init(std::bitset<8> mode) {
	instance_.reset(new EngineContext());
	instance_->mode_ = mode;
	instance_->set_thread_role(ThreadRole::MainGame);
	if (mode.test(StartMode::Log_)) {
		Log::init();
	}
	if (mode.test(StartMode::Asset_)) { // Asset
		instance_->asset_manager_ = std::make_unique<AssetManager>();
	}
	if (mode.test(StartMode::Window_)) { // Window
		instance_->window_ = std::make_unique<Window>(800, 600, L"Renderer Window");
	}
	if (mode.test(StartMode::Render_) && mode.test(StartMode::Window_)) {
		instance_->render_system_ = std::make_unique<RenderSystem>();
		instance_->render_system_->init(instance_->window_->get_hwnd()); // todo: add headless
	}
	// workers
	instance_->thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
	
	// main thread
	if (!mode.test(StartMode::Single_Thread_)) {
		instance_->render_thread_ = std::make_unique<std::jthread>([](std::stop_token stoken) {
			instance_->set_thread_role(ThreadRole::Render);
			INFO(LogEngine, "Render thread started.");
			while (!stoken.stop_requested()) {
				RenderPacket packet;
				{
					std::unique_lock lock(instance_->render_mutex_);
					instance_->render_cv_.wait(lock, [stoken]() {
						return stoken.stop_requested() || !instance_->render_queue_.empty();
					});

					if (stoken.stop_requested() && instance_->render_queue_.empty()) {
						break;
					}

					packet = instance_->render_queue_.front();
					instance_->render_queue_.pop();
				}
				instance_->render_cv_.notify_one();
				instance_->render_system_->tick(packet);
			}
		});
	}
}

void EngineContext::exit() {
	if (!instance_) {
		return;
	}

	if (instance_->render_thread_) {
		instance_->render_thread_->request_stop();
		{
			std::unique_lock lock(instance_->render_mutex_);
			instance_->render_cv_.notify_all();
		}
		instance_->render_thread_.reset();
	}

	if (instance_->mode_.test(StartMode::Log_)) {
		Log::shutdown();
	}
	instance_.reset();
}

void EngineContext::main_loop() {
	if (instance_) {
		instance_->main_loop_internal();
	}
}

void EngineContext::main_loop_internal() {
	while (true) {
		if (window_ && !window_->process_messages()) {
			break;
		}

		Input::get_instance().tick();

		// Logic Tick (Prepare Render Packet)
		RenderPacket packet;
		// Fill packet with data here...

		if (mode_.test(StartMode::Single_Thread_) && render_system_) {
			render_system_->tick(packet);
		} else if (render_system_) {
			// Multi-threaded submission
			std::unique_lock lock(render_mutex_);
			render_cv_.wait(lock, [this]() {
				return render_queue_.size() < MAX_FRAMES_IN_FLIGHT;
			});

			render_queue_.push(packet);
			lock.unlock();
			render_cv_.notify_one();
		}
	}
}

RHI *EngineContext::get_rhi() {
	if (instance_ && instance_->render_system_) {
		return instance_->render_system_->get_rhi();
	}
	return nullptr;
}

AssetManager *EngineContext::asset() {
	if (instance_) {
		return instance_->asset_manager_.get();
	}
	return nullptr;
}

ThreadPool *EngineContext::thread_pool() {
	if (instance_) {
		return instance_->thread_pool_.get();
	}
	return nullptr;
}