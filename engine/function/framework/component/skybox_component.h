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
 * 
 * Rendering flow starts from a panorama (equirectangular 2D texture), which
 * is converted to a cube texture for rendering.
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
     * @param material The skybox material with panorama texture
     */
    void set_material(SkyboxMaterialRef material);
    
    /**
     * @brief Get the current skybox material
     */
    SkyboxMaterialRef get_material() const { return material_; }

    /**
     * @brief Set the panorama texture (equirectangular 2D texture)
     * This will be converted to a cube texture for rendering.
     * @param texture The panorama texture (2D equirectangular)
     */
    void set_panorama_texture(TextureRef texture);
    
    /**
     * @brief Get the panorama texture
     */
    TextureRef get_panorama_texture() const { 
        return material_ ? material_->get_panorama_texture() : nullptr; 
    }

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

    /**
     * @brief Set the resolution of the generated cube texture
     * @param resolution Resolution in pixels (default 512)
     */
    void set_cube_texture_resolution(uint32_t resolution);
    uint32_t get_cube_texture_resolution() const { return cube_texture_resolution_; }

    static void register_class();

private:
    void update_transform();

    // Asset dependencies - only material needs to be serialized
    // mesh_ is loaded/generated at runtime (standard cube mesh)
    ASSET_DEPS(
        (SkyboxMaterialRef, material_)
    )
    
    // Runtime mesh - not serialized
    MeshRef mesh_;
    
    float intensity_ = 1.0f;
    float skybox_scale_ = 1000.0f;
    uint32_t cube_texture_resolution_ = 512;
    bool initialized_ = false;

    friend class cereal::access;
    template<class Archive>
    void serialize(Archive& ar) {
        serialize_deps(ar);
        ar(cereal::make_nvp("intensity", intensity_));
        ar(cereal::make_nvp("skybox_scale", skybox_scale_));
        ar(cereal::make_nvp("cube_texture_resolution", cube_texture_resolution_));
    }
};

CEREAL_REGISTER_TYPE(SkyboxComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, SkyboxComponent);
