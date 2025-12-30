#include "engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/core/window/window.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system.h"

std::unique_ptr<EngineContext> EngineContext::instance_;

EngineContext::EngineContext() {}
EngineContext::~EngineContext() {}

void EngineContext::init(std::bitset<8> mode) {
	instance_.reset(new EngineContext());
	instance_->mode_ = mode;
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
}

void EngineContext::exit() {
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

		if (render_system_) {
			render_system_->tick();
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
