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

void RenderMeshManager::cleanup_for_test() {
    // Clear registered mesh renderers
    mesh_renderers_.clear();
    
    // Clear current batches
    current_batches_.clear();
    
    // Reset active camera
    active_camera_ = nullptr;
    

}

void RenderMeshManager::build_rdg(RDGBuilder& builder, RDGTextureHandle color_target, 
                                   std::optional<RDGTextureHandle> depth_target,
                                   bool enable_pbr, bool enable_npr) {
    if (!initialized_) return;
    
    if (!active_camera_) {
        WARN(LogRenderMeshManager, "No active camera, skipping RDG build");
        return;
    }
    
    Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);
    Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
    float light_intensity = 1.0f;
    std::vector<render::ShaderLightData> additional_lights;
    
    auto* world = EngineContext::world();
    if (world && world->get_active_scene()) {
        bool main_light_found = false;
        for (auto& entity : world->get_active_scene()->entities_) {
            if (!entity) continue;
            
            // Directional lights
            auto* dir_light = entity->get_component<DirectionalLightComponent>();
            if (dir_light && dir_light->enable()) {
                auto* transform = entity->get_component<TransformComponent>();
                Vec3 dir = Vec3(0.0f, -1.0f, 0.0f);
                if (transform) {
                    dir = transform->transform.front();
                }
                
                if (!main_light_found) {
                    // First directional light → main light in cbuffer
                    light_dir = dir;
                    light_color = dir_light->get_color();
                    light_intensity = dir_light->get_intensity();
                    main_light_found = true;
                } else {
                    // Extra directional lights → light buffer
                    render::ShaderLightData ld = {};
                    ld.direction = dir;
                    ld.color = dir_light->get_color();
                    ld.intensity = dir_light->get_intensity();
                    ld.type = static_cast<uint32_t>(render::LightType::Directional);
                    additional_lights.push_back(ld);
                }
                continue;
            }
            
            // Point lights
            auto* pt_light = entity->get_component<PointLightComponent>();
            if (pt_light && pt_light->enable()) {
                auto* transform = entity->get_component<TransformComponent>();
                render::ShaderLightData ld = {};
                ld.position = transform ? transform->get_world_position() : Vec3::Zero();
                ld.color = pt_light->get_color();
                ld.intensity = pt_light->get_intensity();
                ld.type = static_cast<uint32_t>(render::LightType::Point);
                ld.range = 25.0f; // default range
                additional_lights.push_back(ld);
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
            } 
            else if (mask & PASS_MASK_DEFERRED_PASS) {
                pbr_batches.push_back(batch);
            } 
        }
    }
    
    // Determine if we need depth prepass (any opaque objects to render)
    bool has_opaque_objects = !pbr_batches.empty() || !npr_batches.empty();
    
    // Get or create depth target for prepass
    std::optional<RDGTextureHandle> depth_handle = depth_target;
    if (has_opaque_objects && !depth_handle.has_value()) {
        // Create depth texture if not provided
        auto* rsys = EngineContext::render_system();
        auto swapchain = rsys ? rsys->get_swapchain() : nullptr;
        Extent2D extent = swapchain ? swapchain->get_extent() : Extent2D{1280, 720};
        depth_handle = builder.create_texture("DepthPrePass_Depth")
            .extent({extent.width, extent.height, 1})
            .format(FORMAT_D32_SFLOAT)
            .allow_depth_stencil()
            .finish();
    }
    
    // Deferred rendering path: GBufferPass -> DeferredLightingPass
    if (enable_pbr && !pbr_batches.empty() && g_buffer_pass_ && g_buffer_pass_->is_ready() && 
        deferred_lighting_pass_ && deferred_lighting_pass_->is_ready()) {
        
        // G-Buffer Pass (reads depth from prepass, writes gbuffer)
        g_buffer_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position()
        );
        g_buffer_pass_->build(builder, depth_handle.value(), pbr_batches);
        
        // Deferred Lighting Pass (reads gbuffer, writes to color_target)
        deferred_lighting_pass_->set_per_frame_data(
            active_camera_->get_position(),
            (active_camera_->get_view_matrix() * active_camera_->get_projection_matrix()).inverse()
        );
        deferred_lighting_pass_->set_main_light(light_dir, light_color, light_intensity);
        deferred_lighting_pass_->set_lights(additional_lights);
        deferred_lighting_pass_->build(builder, color_target);  // Auto-gets gbuffer from blackboard
    }
    
    // NPR Forward rendering path
    if (enable_npr && !npr_batches.empty() && npr_forward_pass_ && npr_forward_pass_->is_ready()) {
        npr_forward_pass_->set_per_frame_data(
            active_camera_->get_view_matrix(),
            active_camera_->get_projection_matrix(),
            active_camera_->get_position(),
            light_dir,
            light_color,
            light_intensity
        );
        npr_forward_pass_->build(builder, color_target, depth_handle.value(), npr_batches);
    }
    
    // if (!forward_batches.empty() && forward_pass_ && forward_pass_->is_ready()) {
    //     forward_pass_->set_per_frame_data(
    //         active_camera_->get_view_matrix(),
    //         active_camera_->get_projection_matrix(),
    //         active_camera_->get_position(),
    //         light_dir,
    //         light_color,
    //         light_intensity
    //     );
    //     forward_pass_->build(builder, color_target, depth_target, forward_batches);
    // }
}
