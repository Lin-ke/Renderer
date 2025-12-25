#include "engine_context.h"
#include "engine/core/window/window.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_system.h"
#include "engine/function/input/input.h"

std::unique_ptr<EngineContext> EngineContext::instance_;

EngineContext::EngineContext() {}
EngineContext::~EngineContext() {}

void EngineContext::init(){
    Log::init();
    
    instance_.reset(new EngineContext());
    
    instance_->window_ = std::make_unique<Window>(800, 600, L"Renderer Window");
    instance_->render_system_ = std::make_unique<RenderSystem>();
    
    instance_->render_system_->init(instance_->window_->get_hwnd());
}

void EngineContext::exit() {
    instance_.reset();
}

void EngineContext::main_loop() {
    if (instance_) {
        instance_->main_loop_internal();
    }
}

void EngineContext::main_loop_internal(){
    while(true){
        if (window_ && !window_->process_messages()) {
            break;
        }

        Input::get_instance().tick();

        if (render_system_) {
            render_system_->tick();
        }
    }
}

RHI* EngineContext::get_rhi() {
    if (instance_ && instance_->render_system_) {
        return instance_->render_system_->get_rhi();
    }
    return nullptr;
}
