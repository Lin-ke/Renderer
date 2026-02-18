#include "engine/function/render/render_system/render_light_manager.h"
#include "engine/core/math/math.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"

DECLARE_LOG_TAG(LogRenderLightManager);
DEFINE_LOG_TAG(LogRenderLightManager, "RenderLightManager");

void RenderLightManager::init() {
    INFO(LogRenderLightManager, "RenderLightManager Initialized");
}

void RenderLightManager::tick(uint32_t frame_index) {
    prepare_lights(frame_index);
}

DirectionalLightComponent* RenderLightManager::get_directional_light(uint32_t frame_index) {
    return perframe_lights_[frame_index].directional_light;
}

const std::vector<PointLightComponent*>& RenderLightManager::get_point_shadow_lights(uint32_t frame_index) {
    return perframe_lights_[frame_index].point_shadow_lights;
}

const std::vector<VolumeLightComponent*>& RenderLightManager::get_volume_lights(uint32_t frame_index) {
    return perframe_lights_[frame_index].volume_lights;
}

void RenderLightManager::prepare_lights(uint32_t frame_index) {
    auto& lights = perframe_lights_[frame_index];

    // Clear previous frame data
    lights.directional_light = nullptr;
    lights.point_shadow_lights.clear();
    lights.volume_lights.clear();

    // Get active scene from world
    auto* world = EngineContext::world();
    if (!world) {
        return;
    }
    
    auto* scene = world->get_active_scene();
    if (!scene) {
        return;
    }

    // Collect directional light
    lights.directional_light = scene->get_directional_light();
    if (lights.directional_light && lights.directional_light->enable()) {
        lights.directional_light->update_light_info();
    }

    // Collect point lights
    auto point_light_components = scene->get_point_lights();
    uint32_t shadow_light_count = 0;
    
    for (auto* point_light : point_light_components) {
        if (!point_light) continue;

        // Reset shadow ID
        point_light->point_shadow_id_ = MAX_POINT_SHADOW_COUNT;

        // Check if this light should cast shadow
        if (point_light->enable() && 
            point_light->cast_shadow() && 
            shadow_light_count < MAX_POINT_SHADOW_COUNT) {
            point_light->set_point_shadow_id(shadow_light_count++);
            lights.point_shadow_lights.push_back(point_light);
        }

        // Update light info regardless of shadow status
        if (point_light->enable()) {
            point_light->update_light_info();
        }
    }

    // Collect volume lights
    auto volume_light_components = scene->get_volume_lights();
    for (auto* volume_light : volume_light_components) {
        if (volume_light && volume_light->enable()) {
            lights.volume_lights.push_back(volume_light);
            volume_light->update_light_info();
        }
    }

    // INFO(LogRenderLightManager, 
    //      "Frame {}: {} directional light, {} point shadow lights, {} volume lights",
    //      frame_index,
    //      lights.directional_light ? 1 : 0,
    //      lights.point_shadow_lights.size(),
    //      lights.volume_lights.size());
}
