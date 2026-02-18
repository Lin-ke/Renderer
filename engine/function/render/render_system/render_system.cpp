#include "engine/function/render/render_system/render_system.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi.h"
#include <GLFW/glfw3.h>
#include "engine/core/log/Log.h"

#include <imgui.h>

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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hide window for tests/headless
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
    
    // Initialize ImGui
    if (backend_ && window_) {
        backend_->init_imgui(window_);
    }

    light_manager_ = std::make_shared<RenderLightManager>();
    mesh_manager_ = std::make_shared<RenderMeshManager>();
    gizmo_manager_ = std::make_shared<GizmoManager>();
    
    light_manager_->init();
    mesh_manager_->init();
    gizmo_manager_->init();

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

    // Use frame_index from packet for multi-threaded safety
    uint32_t frame_index = packet.frame_index;
    
    // Update active camera in mesh manager if provided
    if (packet.active_camera) {
        mesh_manager_->set_active_camera(packet.active_camera);
    }
    
    // Tick managers (collect render data)
    mesh_manager_->tick();
    light_manager_->tick(frame_index);
    update_global_setting();

    // Execute rendering using frame_index from packet for thread safety
    auto& resource = per_frame_common_resources_[frame_index];
    resource.fence->wait();
    
    RHITextureRef swapchain_texture = swapchain_->get_new_frame(nullptr, resource.start_semaphore);
    RHICommandContextRef command = resource.command;
    
    Extent2D extent = swapchain_->get_extent();
    
    command->begin_command();
    {
        // Create render pass that loads clear and stores
        RHIRenderPassInfo rp_info = {};
        rp_info.extent = extent;
        
        // Create texture view for back buffer
        RHITextureViewInfo view_info = {};
        view_info.texture = swapchain_texture;
        RHITextureViewRef back_buffer_view = backend_->create_texture_view(view_info);
        
        rp_info.color_attachments[0].texture_view = back_buffer_view;
        rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
        rp_info.color_attachments[0].clear_color = {0.1f, 0.2f, 0.4f, 1.0f};  // Dark blue background
        rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;
        
        RHIRenderPassRef render_pass = backend_->create_render_pass(rp_info);
        command->begin_render_pass(render_pass);
        
        // Render all mesh batches (bunny, etc.)
        mesh_manager_->render_batches(command, back_buffer_view, extent);
        
        //####TODO####: Other render passes
        // for(auto& pass : passes_) { if(pass) pass->build(...); }
        
        command->end_render_pass();
        
        // Cleanup render pass resources before UI
        render_pass->destroy();
        back_buffer_view->destroy();
    }
    command->end_command();
    
    // Render ImGui and Gizmo
    // Note: This happens outside the main render pass
    if (backend_) {
        backend_->imgui_new_frame();
        
        // Build UI
        if (show_ui_) {
            ImGui::Begin("Renderer Debug", &show_ui_);
            
            // Wireframe toggle
            if (ImGui::Checkbox("Wireframe", &wireframe_mode_)) {
                if (mesh_manager_) {
                    mesh_manager_->set_wireframe(wireframe_mode_);
                }
            }
            
            // Gizmo controls
            if (gizmo_manager_) {
                gizmo_manager_->draw_controls();
            }
            
            // Frame info
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                        1000.0f / ImGui::GetIO().Framerate, 
                        ImGui::GetIO().Framerate);
            
            ImGui::End();
        }
        
        // Draw gizmo for selected entity
        if (gizmo_manager_ && selected_entity_ && packet.active_camera) {
            gizmo_manager_->draw_gizmo(packet.active_camera, selected_entity_, 
                                       ImVec2(0, 0), ImVec2(extent.width, extent.height));
        }
        
        // Render ImGui
        backend_->imgui_render();
    }
    
    command->execute(resource.fence, resource.start_semaphore, resource.finish_semaphore);
    swapchain_->present(resource.finish_semaphore);
    
    return true;
}

void RenderSystem::destroy() {
    // Shutdown ImGui
    if (backend_) {
        backend_->imgui_shutdown();
    }
    
    // Cleanup managers first (they may hold RHI resources)
    if (gizmo_manager_) {
        gizmo_manager_->shutdown();
        gizmo_manager_.reset();
    }
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

// render_ui_begin is now integrated into tick()
void RenderSystem::render_ui_begin() {
    // UI building is done directly in tick() after imgui_new_frame()
}

// render_ui is now integrated into tick()
void RenderSystem::render_ui(RHICommandContextRef command) {
    (void)command;
}
