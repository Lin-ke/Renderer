#pragma once

#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/core/reflect/class_db.h"

#include <array>
#include <cstdint>

class DirectionalLightComponent : public Component {
    CLASS_DEF(DirectionalLightComponent, Component)
public:
    DirectionalLightComponent() = default;
    virtual ~DirectionalLightComponent() = default;

    virtual void on_init(); // Renamed from OnInit
    virtual void on_update(float delta_time); // Renamed from OnUpdate

    // inline Frustum get_frustum(int index) { return light_infos_[index].frustum; }

    // bool should_update(uint32_t cascade) { return update_cnts_[cascade] == 0; }
    float get_constant_bias() { return constant_bias_; }
    float get_slope_bias() { return slope_bias_; }

    void set_color(Vec3 color) { this->color_ = color; }
    void set_intensity(float intensity) { this->intensity_ = intensity; }
    void set_cascade_split(float cascade_split) { this->cascade_split_lambda_ = cascade_split; }
    void set_update_frequency(uint32_t cascade, int32_t frequency) { update_frequencies_[cascade] = frequency; }
    void set_cast_shadow(bool cast_shadow) { this->cast_shadow_ = cast_shadow; }
    void set_fog_scattering(float fog_scattering) { this->fog_scattering_ = fog_scattering; }
    void set_enable(bool enable) { this->enable_ = enable; }

    inline Vec3 get_color() const { return color_; }
    inline float get_intensity() const { return intensity_; }
    inline bool cast_shadow() const { return cast_shadow_; }
    inline bool enable() const { return enable_; }

    void update_light_info(); // Changed to public to be called by manager

    static void register_class() {
        Registry::add<DirectionalLightComponent>("DirectionalLightComponent")
            .member("color", &DirectionalLightComponent::color_)
            .member("intensity", &DirectionalLightComponent::intensity_)
            .member("cast_shadow", &DirectionalLightComponent::cast_shadow_)
            .member("enable", &DirectionalLightComponent::enable_)
            // Add others...
            ;
    }

private:
    Vec3 color_ = Vec3::Ones();
    float intensity_ = 2.0f;
    bool cast_shadow_ = true;
    bool enable_ = true;
    float cascade_split_lambda_ = 0.95f;
    std::array<int32_t, DIRECTIONAL_SHADOW_CASCADE_LEVEL> update_frequencies_ = { 0 };
    std::array<int32_t, DIRECTIONAL_SHADOW_CASCADE_LEVEL> update_cnts_ = { 0 };
    float constant_bias_ = 1.0f;
    float slope_bias_ = 5.0f;
    float fog_scattering_ = 0.005f;

    std::array<DirectionalLightInfo, DIRECTIONAL_SHADOW_CASCADE_LEVEL> light_infos_;

    Vec3 front_ = Vec3::UnitX();
    Vec3 up_ = Vec3::UnitY();

    void update_matrix();
    void update_cascades();
};

CEREAL_REGISTER_TYPE(DirectionalLightComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, DirectionalLightComponent);
