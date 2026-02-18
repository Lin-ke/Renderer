#pragma once

#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/core/reflect/class_db.h"
#include <cstdint>

class PointLightComponent : public Component {
    CLASS_DEF(PointLightComponent, Component)
public:
    PointLightComponent() = default;
    virtual ~PointLightComponent();

    virtual void on_init();
    virtual void on_update(float delta_time);

    void set_scale(float scale) { this->far_ = scale; }
    void set_color(Vec3 color) { this->color_ = color; }
    void set_intensity(float intensity) { this->intensity_ = intensity; }
    void set_cast_shadow(bool cast_shadow) { this->cast_shadow_ = cast_shadow; }
    void set_fog_scattering(float fog_scattering) { this->fog_scattering_ = fog_scattering; }
    void set_enable(bool enable) { this->enable_ = enable; }

    inline Vec3 get_color() const { return color_; }
    inline float get_intensity() const { return intensity_; }

    inline BoundingSphere get_bounding_sphere() const { return sphere_; }
    float get_constant_bias() { return constant_bias_; }
    float get_slope_bias() { return slope_bias_; }

    inline bool cast_shadow() const { return cast_shadow_; }
    inline bool enable() const { return enable_; }

    inline uint32_t get_point_light_id() { return point_light_id_; }

    void update_light_info(); // public for manager

    // Public members for manager access (based on RD code accessing them directly or via friend)
    // Assuming manager uses getters/setters or public access. 
    // RD code had friend class RenderLightManager.
    // I made update_light_info public.
    
    // Helper to set shadow ID (RD code manager sets it)
    void set_point_shadow_id(uint32_t id) { point_shadow_id_ = id; }
    uint32_t point_light_id_ = 0;
    uint32_t point_shadow_id_ = MAX_POINT_SHADOW_COUNT;

    static void register_class() {
        Registry::add<PointLightComponent>("PointLightComponent")
            .member("color", &PointLightComponent::color_)
            .member("intensity", &PointLightComponent::intensity_)
            .member("cast_shadow", &PointLightComponent::cast_shadow_)
            .member("enable", &PointLightComponent::enable_)
            ;
    }

private:
    float near_ = 0.1f;
    float far_ = 25.0f;
    Vec3 color_ = Vec3::Ones();
    float intensity_ = 2.0f;
    float evsm_[2] = {10, 15};
    float fog_scattering_ = 0.02f;
    bool cast_shadow_ = true;
    bool enable_ = true;
    float constant_bias_ = 0.005f;
    float slope_bias_ = 0.0;

    BoundingSphere sphere_;
    PointLightInfo info_;
};

CEREAL_REGISTER_TYPE(PointLightComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, PointLightComponent);
