#pragma once

#include "engine/function/framework/component.h"
#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/class_db.h"
#include "engine/function/asset/asset_macros.h"
#include <memory>

/**
 * @brief Component for rendering a skybox environment
 * 
 * SkyboxComponent renders a cube-mapped environment that follows the camera.
 * The skybox is rendered as a cube centered at the camera position, with
 * depth write disabled so it appears at infinity.
 */
class SkyboxComponent : public Component {
    CLASS_DEF(SkyboxComponent, Component)
public:
    SkyboxComponent();
    virtual ~SkyboxComponent();

    virtual void on_init() override;
    virtual void on_update(float delta_time) override;

    /**
     * @brief Set the skybox material
     * @param material The skybox material with cube texture
     */
    void set_material(SkyboxMaterialRef material);
    
    /**
     * @brief Get the current skybox material
     */
    SkyboxMaterialRef get_material() const { return material_; }

    /**
     * @brief Set the cube texture directly (creates/updates material if needed)
     * @param texture The cube texture for the skybox
     */
    void set_cube_texture(TextureRef texture);

    /**
     * @brief Set the intensity/brightness of the skybox
     * @param intensity Intensity multiplier (default 1.0)
     */
    void set_intensity(float intensity);
    float get_intensity() const { return intensity_; }

    /**
     * @brief Get the material ID for rendering
     */
    uint32_t get_material_id() const;

    /**
     * @brief Collect draw batches for rendering
     * @param batches Output vector to append draw batches to
     */
    void collect_draw_batch(std::vector<render::DrawBatch>& batches);

    /**
     * @brief Get the skybox scale (how large the cube is)
     */
    float get_skybox_scale() const { return skybox_scale_; }
    
    /**
     * @brief Set the skybox scale
     * @param scale The scale of the skybox cube (default 1000.0)
     */
    void set_skybox_scale(float scale) { skybox_scale_ = scale; }

    static void register_class();

private:
    void ensure_default_resources();
    void update_transform();

    // Asset dependencies - ASSET_DEPS macro declares these members:
    // SkyboxMaterialRef material_;
    // MeshRef mesh_;
    ASSET_DEPS(
        (SkyboxMaterialRef, material_),
        (MeshRef, mesh_)
    )
    
    float intensity_ = 1.0f;
    float skybox_scale_ = 1000.0f;
    bool initialized_ = false;

    friend class cereal::access;
    template<class Archive>
    void serialize(Archive& ar) {
        serialize_deps(ar);
        ar(cereal::make_nvp("intensity", intensity_));
        ar(cereal::make_nvp("skybox_scale", skybox_scale_));
    }
};

CEREAL_REGISTER_TYPE(SkyboxComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, SkyboxComponent);
