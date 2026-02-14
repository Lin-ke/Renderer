#pragma once

#include <memory>
#include <vector>

//####TODO####: Include actual component headers when available
// #include "engine/function/framework/component/directional_light_component.h"
// #include "engine/function/framework/component/point_light_component.h"
// #include "engine/function/framework/component/volume_light_component.h"

//####TODO####: Remove these forward declarations when components are available
class DirectionalLightComponent;
class PointLightComponent;
class VolumeLightComponent;

class RenderLightManager {
public:
    void init();
    void tick();
    void destroy() {}

    std::shared_ptr<DirectionalLightComponent> get_directional_light() { return directional_light_; }
    const std::vector<std::shared_ptr<PointLightComponent>>& get_point_shadow_lights() { return point_shadow_lights_; }
    const std::vector<std::shared_ptr<VolumeLightComponent>>& get_volume_lights() { return volume_lights_; }

private:
    void prepare_lights();

    std::vector<std::shared_ptr<PointLightComponent>> point_shadow_lights_;
    std::shared_ptr<DirectionalLightComponent> directional_light_;
    std::vector<std::shared_ptr<VolumeLightComponent>> volume_lights_;
};
