#include "engine/function/render/render_system/render_system.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi.h"
#include <GLFW/glfw3.h>
#include "engine/core/log/Log.h"

//####TODO####: Render Passes
// #include "engine/function/render/render_pass/gpu_culling_pass.h"
// ...

DECLARE_LOG_TAG(LogRenderSystem);
DEFINE_LOG_TAG(LogRenderSystem, "RenderSystem");

void RenderSystem::init_glfw() {
    if (!glfwInit()) {
        ERR(LogRenderSystem, "Failed to initialize GLFW");
        return;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_ = glfwCreateWindow(WINDOW_EXTENT.width, WINDOW_EXTENT.height, "Toy Renderer", nullptr, nullptr);
    if (!window_) {
        ERR(LogRenderSystem, "Failed to create GLFW window");
    }
}

void RenderSystem::destroy_glfw() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    // Note: glfwTerminate() is not called here to allow multiple test runs
    // GLFW will be cleaned up when the process exits
}

void RenderSystem::init(void* window_handle) {
    INFO(LogRenderSystem, "RenderSystem Initialized");

    if (!window_) {
        if (window_handle) {
             // In a real system we'd wrap existing handle
             // but here our RHI backend takes GLFWwindow*
             // so we fallback to init_glfw if window_ is null
             init_glfw();
        } else {
             init_glfw(); 
        }
    }

    // Initialize RHI backend first so it's available for managers
    init_base_resource();

    light_manager_ = std::make_shared<RenderLightManager>();
    mesh_manager_ = std::make_shared<RenderMeshManager>();
    
    light_manager_->init();
    mesh_manager_->init();

    init_passes();
}

void RenderSystem::init_base_resource() {
    RHIBackendInfo info = {};
    info.type = BACKEND_DX11;
    info.enable_debug = true;
    backend_ = RHIBackend::init(info); 
    
    if (!backend_) {
        WARN(LogRenderSystem, "RHI Backend not initialized!");
        return;
    }

    surface_ = backend_->create_surface(window_);
    queue_ = backend_->get_queue({ QUEUE_TYPE_GRAPHICS, 0 });
    
    RHISwapchainInfo sw_info = {};
    sw_info.surface = surface_;
    sw_info.present_queue = queue_;
    sw_info.image_count = FRAMES_IN_FLIGHT;
    sw_info.extent = WINDOW_EXTENT;
    sw_info.format = COLOR_FORMAT;
    
    swapchain_ = backend_->create_swapchain(sw_info);
    pool_ = backend_->create_command_pool({ queue_ });

    for(uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        per_frame_common_resources_[i].command = backend_->create_command_context(pool_);
        per_frame_common_resources_[i].start_semaphore = backend_->create_semaphore();
        per_frame_common_resources_[i].finish_semaphore = backend_->create_semaphore();
        per_frame_common_resources_[i].fence = backend_->create_fence(true);
    }
}

void RenderSystem::init_passes() {
    // passes_[MESH_DEPTH_PASS] = std::make_shared<DepthPass>();
    // ...
    // for(auto& pass : passes_) { if(pass) pass->init(); }
}

bool RenderSystem::tick(const RenderPacket& packet) {
    // ENGINE_TIME_SCOPE(RenderSystem::Tick);
    
    // Check if window should close
    if(glfwWindowShouldClose(window_)) {
        return false;
    }

    mesh_manager_->tick();
    light_manager_->tick();
    update_global_setting();

    // auto& resource = per_frame_common_resources_[EngineContext::current_frame_index()];
    // resource.fence->wait();
    
    // RHITextureRef swapchain_texture = swapchain_->get_new_frame(nullptr, resource.start_semaphore);
    // RHICommandContextRef command = resource.command;
    
    // command->begin_command();
    // {
        // ENGINE_TIME_SCOPE(RenderSystem::RDGBuild);
        // RDGBuilder rdg_builder(command);
        // for(auto& pass : passes_) { if(pass) pass->build(rdg_builder); }
        // rdg_builder.execute();
        // rdg_dependency_graph_ = rdg_builder.get_graph();
    // }
    // {
        // ENGINE_TIME_SCOPE(RenderSystem::RecordCommands);
        // command->end_command();
        // command->execute(resource.fence, resource.start_semaphore, resource.finish_semaphore);
    // }
    // swapchain_->present(resource.finish_semaphore);
    
    return true;
}

void RenderSystem::destroy() {
    // Cleanup managers first (they may hold RHI resources)
    if (mesh_manager_) {
        mesh_manager_->destroy();
        mesh_manager_.reset();
    }
    if (light_manager_) {
        light_manager_->destroy();
        light_manager_.reset();
    }
    
    // Clear per-frame resources
    for (auto& resource : per_frame_common_resources_) {
        resource.command.reset();
        resource.start_semaphore.reset();
        resource.finish_semaphore.reset();
        resource.fence.reset();
    }
    
    // Cleanup RHI resources in reverse order of creation
    // Important: swapchain must be destroyed before pool/surface
    pool_.reset();
    swapchain_.reset();
    queue_.reset();
    surface_.reset();
    
    // Destroy backend last (after all RHI resources are released)
    if (backend_) {
        backend_->destroy();
        backend_.reset();
        // Important: Reset the static backend instance so next init() creates a fresh one
        RHIBackend::reset_backend();
    }
}

void RenderSystem::update_global_setting() {
    /* //####TODO####
    global_setting_.totalTicks = EngineContext::get_current_tick();
    global_setting_.totalTickTime += EngineContext::get_delta_time();
    // ...
    EngineContext::render_resource()->set_render_global_setting(global_setting_);
    */
}
