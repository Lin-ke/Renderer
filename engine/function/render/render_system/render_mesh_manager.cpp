#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/world.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/core/log/Log.h"

DEFINE_LOG_TAG(LogRenderMeshManager, "RenderMeshManager");

void RenderMeshManager::init() {
    INFO(LogRenderMeshManager, "Initializing RenderMeshManager...");
    
    // Create forward pass
    forward_pass_ = std::make_shared<render::ForwardPass>();
    forward_pass_->init();
    
    initialized_ = true;
    INFO(LogRenderMeshManager, "RenderMeshManager initialized");
}

void RenderMeshManager::destroy() {
    INFO(LogRenderMeshManager, "Destroying RenderMeshManager...");
    
    mesh_renderers_.clear();
    forward_pass_.reset();
    
    initialized_ = false;
    INFO(LogRenderMeshManager, "RenderMeshManager destroyed");
}

void RenderMeshManager::tick() {
    if (!initialized_) return;
    
    // Get active camera from world if not set
    if (!active_camera_) {
        auto* world = EngineContext::world();
        if (world) {
            active_camera_ = world->get_active_camera();
        }
    }
    
    // Update camera if available
    if (active_camera_) {
        active_camera_->update_camera_info();
    }
    
    prepare_mesh_pass();
}

void RenderMeshManager::register_mesh_renderer(MeshRendererComponent* component) {
    if (!component) return;
    
    auto it = std::find(mesh_renderers_.begin(), mesh_renderers_.end(), component);
    if (it == mesh_renderers_.end()) {
        mesh_renderers_.push_back(component);
        INFO(LogRenderMeshManager, "Registered mesh renderer, total: {}", mesh_renderers_.size());
    }
}

void RenderMeshManager::unregister_mesh_renderer(MeshRendererComponent* component) {
    if (!component) return;
    
    auto it = std::find(mesh_renderers_.begin(), mesh_renderers_.end(), component);
    if (it != mesh_renderers_.end()) {
        mesh_renderers_.erase(it);
        INFO(LogRenderMeshManager, "Unregistered mesh renderer, total: {}", mesh_renderers_.size());
    }
}

void RenderMeshManager::prepare_mesh_pass() {
    // Collect draw batches
    std::vector<render::DrawBatch> batches;
    collect_draw_batches(batches);
    
    if (batches.empty()) {
        return;
    }
    
    INFO(LogRenderMeshManager, "Rendering {} batches", batches.size());
    
    // Execute render pass
    execute_render_pass(batches);
}

void RenderMeshManager::collect_draw_batches(std::vector<render::DrawBatch>& batches) {
    batches.clear();
    
    // First try to get renderers from World (scene)
    auto* world = EngineContext::world();
    if (world && world->get_active_scene()) {
        auto world_renderers = world->get_mesh_renderers();
        for (auto* renderer : world_renderers) {
            if (renderer) {
                renderer->collect_draw_batch(batches);
            }
        }
    }
    
    // Also check manually registered renderers (for tests without World)
    for (auto* renderer : mesh_renderers_) {
        if (!renderer) continue;
        
        // Check if already added from world
        bool already_added = false;
        if (world && world->get_active_scene()) {
            for (auto* world_renderer : world->get_mesh_renderers()) {
                if (world_renderer == renderer) {
                    already_added = true;
                    break;
                }
            }
        }
        
        if (!already_added) {
            renderer->collect_draw_batch(batches);
        }
    }
}

void RenderMeshManager::execute_render_pass(const std::vector<render::DrawBatch>& batches) {
    auto render_system = EngineContext::render_system();
    if (!render_system) return;
    
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    auto swapchain = render_system->get_swapchain();
    if (!swapchain) return;

    if (!forward_pass_ || !forward_pass_->is_ready()) {
        ERR(LogRenderMeshManager, "Forward pass not ready");
        return;
    }
    
    // Get swapchain image
    RHITextureRef back_buffer = swapchain->get_new_frame(nullptr, nullptr);
    if (!back_buffer) {
        ERR(LogRenderMeshManager, "Failed to acquire swapchain image");
        return;
    }
    
    // Get camera data
    if (!active_camera_) {
        WARN(LogRenderMeshManager, "No active camera, skipping render");
        return;
    }
    
    // Get light data from world
    Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);  // Default light from above
    Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
    float light_intensity = 1.0f;
    
    auto* world = EngineContext::world();
    if (world && world->get_active_scene()) {
        // Get first directional light
        for (auto& entity : world->get_active_scene()->entities_) {
            if (!entity) continue;
            auto* light = entity->get_component<DirectionalLightComponent>();
            if (light && light->enable()) {
                // Get light direction from entity's transform
                auto* transform = entity->get_component<TransformComponent>();
                if (transform) {
                    light_dir = -transform->transform.front(); // Light points along -front
                }
                light_color = light->get_color();
                light_intensity = light->get_intensity();
                break;
            }
        }
    }
    
    // Set per-frame data in forward pass
    forward_pass_->set_per_frame_data(
        active_camera_->get_view_matrix(),
        active_camera_->get_projection_matrix(),
        active_camera_->get_position(),
        light_dir,
        light_color,
        light_intensity
    );
    
    // Create command pool and context
    RHICommandPoolInfo pool_info = {};
    RHICommandPoolRef pool = backend->create_command_pool(pool_info);
    if (!pool) {
        ERR(LogRenderMeshManager, "Failed to create command pool");
        return;
    }
    
    RHICommandContextRef context = backend->create_command_context(pool);
    if (!context) {
        ERR(LogRenderMeshManager, "Failed to create command context");
        return;
    }
    
    // Create render pass
    RHIRenderPassInfo rp_info = {};
    rp_info.extent = swapchain->get_extent();
    
    // Create texture view for back buffer
    RHITextureViewInfo view_info = {};
    view_info.texture = back_buffer;
    RHITextureViewRef back_buffer_view = backend->create_texture_view(view_info);
    if (!back_buffer_view) {
        ERR(LogRenderMeshManager, "Failed to create back buffer view");
        return;
    }
    
    rp_info.color_attachments[0].texture_view = back_buffer_view;
    rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
    rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;
    rp_info.color_attachments[0].clear_color = { 0.2f, 0.2f, 0.2f, 1.0f }; // Dark gray
    
    RHIRenderPassRef render_pass = backend->create_render_pass(rp_info);
    if (!render_pass) {
        ERR(LogRenderMeshManager, "Failed to create render pass");
        back_buffer_view->destroy();
        return;
    }
    
    // Begin recording
    context->begin_command();
    context->begin_render_pass(render_pass);
    
    // Set viewport and scissor
    Extent2D extent = swapchain->get_extent();
    INFO(LogRenderMeshManager, "Viewport: {}x{}, scissor disabled", extent.width, extent.height);
    context->set_viewport({0, 0}, {extent.width, extent.height});
    // Scissor disabled in pipeline, but set anyway
    context->set_scissor({0, 0}, {extent.width, extent.height});
    
    // Draw all batches using forward pass
    for (const auto& batch : batches) {
        forward_pass_->draw_batch(context, batch);
    }
    
    // End recording
    context->end_render_pass();
    context->end_command();
    
    // Execute and present
    RHIFenceRef fence = backend->create_fence(false);
    if (!fence) {
        ERR(LogRenderMeshManager, "Failed to create fence");
        return;
    }
    context->execute(fence, nullptr, nullptr);
    fence->wait();
    
    swapchain->present(nullptr);
    
    // Cleanup
    render_pass->destroy();
    back_buffer_view->destroy();
    context->destroy();
    pool->destroy();
    fence->destroy();
}
