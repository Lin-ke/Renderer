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
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/core/log/Log.h"
#include <iostream>

DEFINE_LOG_TAG(LogRenderMeshManager, "RenderMeshManager");

void RenderMeshManager::init() {
    INFO(LogRenderMeshManager, "Initializing RenderMeshManager...");
    
    forward_pass_ = std::make_shared<render::ForwardPass>();
    forward_pass_->init();

    pbr_forward_pass_ = std::make_shared<render::PBRForwardPass>();
    pbr_forward_pass_->init();

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
    
    if (!active_camera_) {
        auto* world = EngineContext::world();
        if (world) {
            active_camera_ = world->get_active_camera();
        }
    }
    
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
    current_batches_.clear();
    collect_draw_batches(current_batches_);
}

void RenderMeshManager::collect_draw_batches(std::vector<render::DrawBatch>& batches) {
    batches.clear();
    
    auto* world = EngineContext::world();
    if (world && world->get_active_scene()) {
        auto world_renderers = world->get_mesh_renderers();
        for (auto* renderer : world_renderers) {
            if (renderer) {
                renderer->collect_draw_batch(batches);
            }
        }
    }
    
    for (auto* renderer : mesh_renderers_) {
        if (!renderer) continue;
        
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
    
    if (!active_camera_) {
        WARN(LogRenderMeshManager, "No active camera, skipping render");
        return;
    }
    
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
    
    context->set_viewport({0, 0}, {extent.width, extent.height});
    context->set_scissor({0, 0}, {extent.width, extent.height});

    std::vector<render::DrawBatch> npr_batches;
    std::vector<render::DrawBatch> pbr_batches;
    std::vector<render::DrawBatch> forward_batches;
    
    for (const auto& batch : current_batches_) {
        if (batch.material) {
            auto mask = batch.material->render_pass_mask();
            if (mask & PASS_MASK_NPR_FORWARD) {
                npr_batches.push_back(batch);
            } else if (mask & PASS_MASK_PBR_FORWARD) {
                pbr_batches.push_back(batch);
            } else {
                forward_batches.push_back(batch);
            }
        } else {
            forward_batches.push_back(batch);
        }
    }
    
    if (!npr_batches.empty() && npr_forward_pass_ && npr_forward_pass_->is_ready()) {
        npr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        for (const auto& batch : npr_batches) {
            npr_forward_pass_->draw_batch(context, batch);
        }
    }
    
    if (!pbr_batches.empty() && pbr_forward_pass_ && pbr_forward_pass_->is_ready()) {
        pbr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        if (world && world->get_active_scene()) {
            pbr_forward_pass_->clear_point_lights();
        }
        for (const auto& batch : pbr_batches) {
            pbr_forward_pass_->draw_batch(context, batch);
        }
    }
    
    if (!forward_batches.empty() && forward_pass_ && forward_pass_->is_ready()) {
        forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        for (const auto& batch : forward_batches) {
            forward_pass_->draw_batch(context, batch);
        }
    }
}

void RenderMeshManager::build_rdg(RDGBuilder& builder, RDGTextureHandle color_target, 
                                   std::optional<RDGTextureHandle> depth_target) {
    if (!initialized_) return;
    
    if (!active_camera_) {
        WARN(LogRenderMeshManager, "No active camera, skipping RDG build");
        return;
    }
    
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
    
    std::vector<render::DrawBatch> npr_batches;
    std::vector<render::DrawBatch> pbr_batches;
    std::vector<render::DrawBatch> forward_batches;
    
    for (const auto& batch : current_batches_) {
        if (batch.material) {
            auto mask = batch.material->render_pass_mask();
            if (mask & PASS_MASK_NPR_FORWARD) {
                npr_batches.push_back(batch);
            } else if (mask & PASS_MASK_PBR_FORWARD) {
                pbr_batches.push_back(batch);
            } else {
                forward_batches.push_back(batch);
            }
        } else {
            forward_batches.push_back(batch);
        }
    }
    
    if (!npr_batches.empty() && npr_forward_pass_ && npr_forward_pass_->is_ready()) {
        npr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        npr_forward_pass_->build(builder, color_target, depth_target.value(), npr_batches);
    }
    
    if (!pbr_batches.empty() && pbr_forward_pass_ && pbr_forward_pass_->is_ready()) {
        pbr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        if (world && world->get_active_scene()) {
            pbr_forward_pass_->clear_point_lights();
        }
        pbr_forward_pass_->build(builder, color_target, depth_target, pbr_batches);
    }
    
    if (!forward_batches.empty() && forward_pass_ && forward_pass_->is_ready()) {
        WARN(LogRenderMeshManager, "Non-PBR/NPR path not yet implemented in RDG mode, use render_batches instead");
    }
}
