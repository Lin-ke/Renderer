#pragma once

#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/volume_light_component.h"
#include "engine/configs.h"

#include <array>
#include <memory>
#include <vector>

class RenderLightManager {
public:
    void init();
    void tick(uint32_t frame_index);
    void destroy() {}

    DirectionalLightComponent* get_directional_light(uint32_t frame_index);
    const std::vector<PointLightComponent*>& get_point_shadow_lights(uint32_t frame_index);
    const std::vector<VolumeLightComponent*>& get_volume_lights(uint32_t frame_index);

private:
    void prepare_lights(uint32_t frame_index);

    struct PerFrameLights {
        std::vector<PointLightComponent*> point_shadow_lights;
        DirectionalLightComponent* directional_light = nullptr;
        std::vector<VolumeLightComponent*> volume_lights;
    };
    
    std::array<PerFrameLights, FRAMES_IN_FLIGHT> perframe_lights_;
};
