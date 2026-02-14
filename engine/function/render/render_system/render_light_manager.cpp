#include "engine/function/render/render_system/render_light_manager.h"
#include "engine/core/math/math.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"

//####TODO####: Include world/scene headers
// #include "engine/function/framework/world/world.h"
// #include "engine/function/framework/scene/scene.h"

DECLARE_LOG_TAG(LogRenderLightManager);
DEFINE_LOG_TAG(LogRenderLightManager, "RenderLightManager");

void RenderLightManager::init() {
    INFO(LogRenderLightManager, "RenderLightManager Initialized");
}

void RenderLightManager::tick() {
    prepare_lights();
}

void RenderLightManager::prepare_lights() {
    // ENGINE_TIME_SCOPE(RenderLightManager::PrepareLights); //####TODO####: Time scope

    // LightSetting setting = {};
    // setting.directionalLightCnt = 0;
    // ...

    directional_light_ = nullptr;
    point_shadow_lights_.clear();
    volume_lights_.clear();

    //####TODO####: Missing Scene/World API to get lights
    /*
    directional_light_ = EngineContext::world()->get_active_scene()->get_directional_light();
    if (directional_light_ && directional_light_->enable()) {
        directional_light_->update_light_info();
        setting.directionalLightCnt = 1;
    }

    auto point_light_components = EngineContext::world()->get_active_scene()->get_point_lights();
    for (auto& point_light : point_light_components) {
        if (point_light) {
            // ... logic from RD ...
        }
    }
    
    // ... volume lights ...

    EngineContext::render_resource()->set_light_setting(setting);
    */
}
