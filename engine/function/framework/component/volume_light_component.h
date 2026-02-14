#pragma once

#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/core/reflect/class_db.h" // Includes VolumeLightInfo/Textures
#include <cstdint>

//####TODO####: Define VolumeLightTextures if not in RenderStructs.h
// RenderStructs.h usually only has UBO/SSBO structs. 
// VolumeLightTextures (shared_ptr to textures) might be in a different file or need def.
// I will define a placeholder struct here if needed.
struct VolumeLightTextures {
    // std::shared_ptr<Texture> ...
    // Using void* or placeholders for now
    void* diffuse_tex;
    void* normal_tex;
    void* emission_tex;
    void* position_tex;
    void* radiance_tex;
    void* irradiance_tex;
    void* depth_tex;
};

class VolumeLightComponent : public Component {
    CLASS_DEF(VolumeLightComponent, Component)
public:
    VolumeLightComponent() = default;
    virtual ~VolumeLightComponent();

    virtual void on_init();
    virtual void on_update(float delta_time);

    // Setters...
    void set_enable(bool enable) { this->enable_ = enable; }
    // ...

    inline bool enable() const { return enable_; }
    
    void update_light_info(); // Public for manager

    uint32_t volume_light_id_ = 0;

    static void register_class() {
        Registry::add<VolumeLightComponent>("VolumeLightComponent")
            // ...
            ;
    }

private:
    bool enable_ = true;
    IVec3 probe_counts_ = IVec3(10, 10, 10);
    Vec3 grid_step_ = Vec3(3.0f, 3.0f, 3.0f);
    int rays_per_probe_ = 256;
    float normal_bias_ = 0.25f;
    float energy_preservation_ = 0.95f;
    float depth_sharpness_ = 50.0f;
    float blend_weight_ = 0.95f;
    bool visibility_test_ = true;
    bool infinite_bounce_ = true;
    bool random_orientation_ = true;

    bool visualize_ = true;
    int visualize_mode_ = 0;
    float visualize_probe_scale_ = 0.3f;

    int update_frequencies_[2] = { 1, 1 };
    int update_cnts_[2] = { 0, 0 };
    bool should_update_[2] = { false , false };

    BoundingBox box_;
    VolumeLightInfo info_;
    VolumeLightTextures textures_; // Placeholder

    void update_texture();
};

CEREAL_REGISTER_TYPE(VolumeLightComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, VolumeLightComponent);
