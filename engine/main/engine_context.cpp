#include "engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/core/os/thread_pool.h"
#include "engine/core/window/window.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"

DEFINE_LOG_TAG(LogEngine, "Engine");

std::unique_ptr<EngineContext> EngineContext::instance_;
EngineContext::ThreadRole EngineContext::thread_role_ = EngineContext::ThreadRole::Unknown;
EngineContext::EngineContext() {}
EngineContext::~EngineContext() {}

void EngineContext::init(std::bitset<8> mode) {
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
		instance_->window_ = std::make_unique<class Window>(800, 600, L"Renderer Window");
	}
	if (mode.test(StartMode::Render)) {
		instance_->render_system_ = std::make_unique<RenderSystem>();
		instance_->render_system_->init(instance_->window_ ? instance_->window_->get_hwnd() : nullptr); 
		instance_->render_resource_manager_ = std::make_unique<RenderResourceManager>();
		instance_->render_resource_manager_->init();
	}
	// workers
	instance_->thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
	
	// main thread
	if (!mode.test(StartMode::SingleThread)) {
		instance_->render_thread_ = std::make_unique<std::jthread>([](std::stop_token stoken) {
			instance_->set_thread_role(ThreadRole::Renderer);
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

	// Cleanup render system (includes RHI resources and GLFW)
	if (instance_->render_system_) {
		instance_->render_system_->destroy();
		instance_->render_system_->destroy_glfw();
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
	while (true) {
		if (instance_->window_ && !instance_->window_->process_messages()) {
			break;
		}

		Input::get_instance().tick();

		// Logic Tick (Prepare Render Packet)
		RenderPacket packet;
		
		// Fill packet with data from World/Scene
		if (instance_->world_) {
			Scene* active_scene = instance_->world_->get_active_scene();
			packet.active_scene = active_scene;
			
			// Find active camera in scene
			if (active_scene) {
				for (const auto& entity : active_scene->entities_) {
					if (auto* camera = entity->get_component<CameraComponent>()) {
						packet.active_camera = camera;
						break;
					}
				}
			}
		}
		
		if (instance_->mode_.test(StartMode::SingleThread) && instance_->render_system_) {
			// Single-threaded: set frame index before tick
			packet.frame_index = instance_->current_frame_index_;
			instance_->render_system_->tick(packet);
            instance_->current_frame_index_ = (instance_->current_frame_index_ + 1) % instance_->MAX_FRAMES_IN_FLIGHT;
		} else if (instance_->render_system_) {
			// Multi-threaded submission
			std::unique_lock lock(instance_->render_mutex_);
			instance_->render_cv_.wait(lock, [this]() {
				return instance_->render_queue_.size() < instance_->MAX_FRAMES_IN_FLIGHT;
			});

			// Set frame index for this packet before queuing
			packet.frame_index = instance_->current_frame_index_;
			instance_->render_queue_.push(packet);
            instance_->current_frame_index_ = (instance_->current_frame_index_ + 1) % instance_->MAX_FRAMES_IN_FLIGHT;
			lock.unlock();
			instance_->render_cv_.notify_one();
		}
	}
}

RHIBackendRef EngineContext::rhi() {
	if (instance_ && instance_->render_system_) {
		return instance_->render_system_->get_rhi();
	}
	return nullptr;
}

uint32_t EngineContext::current_frame_index() {
    return instance_ ? instance_->current_frame_index_ : 0;
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