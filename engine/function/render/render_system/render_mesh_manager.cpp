#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/world.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_pass/pbr_forward_pass.h"
#include "engine/function/render/render_pass/npr_forward_pass.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/core/log/Log.h"
#include <iostream>

DEFINE_LOG_TAG(LogRenderMeshManager, "RenderMeshManager");

void RenderMeshManager::init() {
    INFO(LogRenderMeshManager, "Initializing RenderMeshManager...");
    
    // Create forward pass
    forward_pass_ = std::make_shared<render::ForwardPass>();
    forward_pass_->init();

    // Create PBR forward pass
    pbr_forward_pass_ = std::make_shared<render::PBRForwardPass>();
    pbr_forward_pass_->init();

    // Create NPR forward pass
    npr_forward_pass_ = std::make_shared<render::NPRForwardPass>();
    npr_forward_pass_->init();
    
    initialized_ = true;
    INFO(LogRenderMeshManager, "RenderMeshManager initialized");
}

void RenderMeshManager::destroy() {
    INFO(LogRenderMeshManager, "Destroying RenderMeshManager...");
    
    mesh_renderers_.clear();
    forward_pass_.reset();
    pbr_forward_pass_.reset();
    npr_forward_pass_.reset();
    
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

void RenderMeshManager::set_wireframe(bool enable) {
    if (forward_pass_) {
        forward_pass_->set_wireframe(enable);
    }
    if (pbr_forward_pass_) {
        pbr_forward_pass_->set_wireframe(enable);
    }
    if (npr_forward_pass_) {
        npr_forward_pass_->set_wireframe(enable);
    }
}

void RenderMeshManager::prepare_mesh_pass() {
    // Collect draw batches for this frame
    current_batches_.clear();
    collect_draw_batches(current_batches_);
    
    if (!current_batches_.empty()) {
        // INFO(LogRenderMeshManager, "Collected {} draw batches", current_batches_.size());
    }
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

void RenderMeshManager::render_batches(RHICommandContextRef context, RHITextureViewRef back_buffer_view, Extent2D extent) {
    if (!context || !back_buffer_view) {
        ERR(LogRenderMeshManager, "Invalid context or back buffer view");
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
    
    // Set viewport and scissor
    context->set_viewport({0, 0}, {extent.width, extent.height});
    context->set_scissor({0, 0}, {extent.width, extent.height});

    //####TODO####: DEBUG only - INFO(LogRenderMeshManager, "render_batches: pbr_enabled={}, npr_enabled={}, batches={}", pbr_enabled_, npr_enabled_, current_batches_.size());
    
    if (npr_enabled_) {
        if (!npr_forward_pass_) {
            ERR(LogRenderMeshManager, "NPR Forward pass is null");
            return;
        }
        if (!npr_forward_pass_->is_ready()) {
            ERR(LogRenderMeshManager, "NPR Forward pass not ready");
            return;
        }

        npr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );

        // Draw all batches using NPR forward pass
        for (const auto& batch : current_batches_) {
            npr_forward_pass_->draw_batch(context, batch);
        }
    } else if (pbr_enabled_) {
        if (!pbr_forward_pass_) {
            ERR(LogRenderMeshManager, "PBR Forward pass is null");
            return;
        }
        if (!pbr_forward_pass_->is_ready()) {
            ERR(LogRenderMeshManager, "PBR Forward pass not ready");
            return;
        }

        pbr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );

        // Collect point lights if any
        if (world && world->get_active_scene()) {
            pbr_forward_pass_->clear_point_lights();
        }

        // Draw all batches using PBR forward pass
        for (const auto& batch : current_batches_) {
            pbr_forward_pass_->draw_batch(context, batch);
        }
    } else {
        if (!forward_pass_ || !forward_pass_->is_ready()) {
            ERR(LogRenderMeshManager, "Forward pass not ready");
            return;
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
        
        // Draw all batches using forward pass
        for (const auto& batch : current_batches_) {
            forward_pass_->draw_batch(context, batch);
        }
    }
}

void RenderMeshManager::build_rdg(RDGBuilder& builder, RDGTextureHandle color_target, 
                                   std::optional<RDGTextureHandle> depth_target) {
    if (!initialized_) return;
    
    // Get camera data
    if (!active_camera_) {
        WARN(LogRenderMeshManager, "No active camera, skipping RDG build");
        return;
    }
    
    // Get light data from world
    Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);
    Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
    float light_intensity = 1.0f;
    
    auto* world = EngineContext::world();
    if (world && world->get_active_scene()) {
        for (auto& entity : world->get_active_scene()->entities_) {
            if (!entity) continue;
            auto* light = entity->get_component<DirectionalLightComponent>();
            if (light && light->enable()) {
                auto* transform = entity->get_component<TransformComponent>();
                if (transform) {
                    light_dir = -transform->transform.front();
                }
                light_color = light->get_color();
                light_intensity = light->get_intensity();
                break;
            }
        }
    }
    
    if (npr_enabled_ && npr_forward_pass_ && npr_forward_pass_->is_ready()) {
        // Set per-frame data
        npr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        
        // Build NPR pass into RDG
        npr_forward_pass_->build(builder, color_target, depth_target.value(), current_batches_);
    } else if (pbr_enabled_ && pbr_forward_pass_ && pbr_forward_pass_->is_ready()) {
        // Set per-frame data
        pbr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        
        // Collect point lights
        if (world && world->get_active_scene()) {
            pbr_forward_pass_->clear_point_lights();
            // TODO: Collect point lights from scene
        }
        
        // Build PBR pass into RDG
        pbr_forward_pass_->build(builder, color_target, depth_target, current_batches_);
    } else if (forward_pass_ && forward_pass_->is_ready()) {
        // For now, fall back to legacy rendering for non-PBR path
        // TODO: Implement ForwardPass::build for RDG
        WARN(LogRenderMeshManager, "Non-PBR path not yet implemented in RDG mode, use render_batches instead");
    }
}
