#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/world.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/npr_forward_pass.h"
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_pass/deferred_lighting_pass.h"
#include "engine/function/render/render_pass/draw_material.h"
#include <unordered_set>
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/core/log/Log.h"
#include <iostream>

DEFINE_LOG_TAG(LogRenderMeshManager, "RenderMeshManager");

void RenderMeshManager::init() {
    INFO(LogRenderMeshManager, "Initializing RenderMeshManager...");
    
    forward_pass_ = std::make_shared<render::ForwardPass>();
    forward_pass_->init();

    npr_forward_pass_ = std::make_shared<render::NPRForwardPass>();
    npr_forward_pass_->init();

    g_buffer_pass_ = std::make_shared<render::GBufferPass>();
    g_buffer_pass_->init();

    deferred_lighting_pass_ = std::make_shared<render::DeferredLightingPass>();
    deferred_lighting_pass_->init();
    
    initialized_ = true;
    INFO(LogRenderMeshManager, "RenderMeshManager initialized");
}

void RenderMeshManager::destroy() {
    INFO(LogRenderMeshManager, "Destroying RenderMeshManager...");
    
    mesh_renderers_.clear();
    forward_pass_.reset();
    npr_forward_pass_.reset();

    g_buffer_pass_.reset();
    deferred_lighting_pass_.reset();
    
    initialized_ = false;
    INFO(LogRenderMeshManager, "RenderMeshManager destroyed");
}

void RenderMeshManager::prepare_batches(const std::vector<render::DrawBatch>& instances, const RenderCamera* camera) {
    current_batches_.clear();
    if (!initialized_) return;

    if (!camera) {
        current_batches_ = instances;
        return;
    }
    const auto view_projection = camera->view * camera->projection;
    for (const auto& instance : instances) {
        if (MeshRendererComponent::is_visible(instance, view_projection)) {
            current_batches_.push_back(instance);
        }
    }
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
    if (npr_forward_pass_) {
        npr_forward_pass_->set_wireframe(enable);
    }
}

void RenderMeshManager::collect_draw_batches(std::vector<render::DrawBatch>& batches) {
    collect_draw_batches(batches, nullptr);
}

void RenderMeshManager::collect_draw_batches(std::vector<render::DrawBatch>& batches, const Mat4* view_projection) {
    auto* world = EngineContext::world();
    batches.clear();
    std::unordered_set<MeshRendererComponent*> collected;
    if (world) {
        for (auto* renderer : world->get_mesh_renderers()) {
            if (collected.insert(renderer).second) renderer->collect_draw_batch(batches, view_projection);
        }
    }
    for (auto* renderer : mesh_renderers_) {
        if (renderer && collected.insert(renderer).second) {
            renderer->collect_draw_batch(batches, view_projection);
        }
    }
}

void RenderMeshManager::collect_draw_batches(const Scene* scene, std::vector<render::DrawBatch>& batches,
                                             const Mat4* view_projection) {
    batches.clear();
    if (!scene) return;
    for (auto* renderer : scene->get_components<MeshRendererComponent>()) {
        renderer->collect_draw_batch(batches, view_projection);
    }
}

void RenderMeshManager::cleanup_for_test() {
    // Clear registered mesh renderers
    mesh_renderers_.clear();
    
    // Clear current batches
    current_batches_.clear();
    
    // Reset active camera
    active_camera_ = nullptr;
    

}

void RenderMeshManager::build_rdg(RDGBuilder& builder, RDGTextureHandle color_target,
                                 RDGTextureHandle depth_target, const RenderCamera& camera,
                                 const RenderLights& lights, bool enable_pbr, bool enable_npr) {
    if (!initialized_) return;

    std::vector<render::DrawBatch> npr_batches;
    std::vector<render::DrawBatch> pbr_batches;
    for (const auto& batch : current_batches_) {
        if (!batch.material_snapshot) continue;
        const auto mask = batch.material_snapshot->pass_mask;
        if (mask & PASS_MASK_NPR_FORWARD) {
            if (enable_npr) npr_batches.push_back(batch);
        } else if (enable_pbr && (mask & PASS_MASK_DEFERRED_PASS)) {
            pbr_batches.push_back(batch);
        }
    }

    if (!pbr_batches.empty() && g_buffer_pass_->is_ready() && deferred_lighting_pass_->is_ready()) {
        g_buffer_pass_->set_per_frame_data(camera.view, camera.projection, camera.position);
        const auto gbuffer = g_buffer_pass_->build(builder, depth_target, pbr_batches);
        if (gbuffer) {
            deferred_lighting_pass_->set_per_frame_data(camera.position, (camera.view * camera.projection).inverse());
            deferred_lighting_pass_->set_main_light(lights.direction, lights.color, lights.intensity);
            deferred_lighting_pass_->set_lights(lights.additional);
            deferred_lighting_pass_->build(builder, color_target, *gbuffer);
        }
    }

    if (!npr_batches.empty() && npr_forward_pass_->is_ready()) {
        npr_forward_pass_->set_per_frame_data(camera.view, camera.projection, camera.position,
                                              lights.direction, lights.color, lights.intensity);
        npr_forward_pass_->build(builder, color_target, depth_target, npr_batches);
    }
}
