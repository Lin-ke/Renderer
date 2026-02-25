#pragma once

#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/asset/asset_macros.h"
#include <cereal/access.hpp>
#include <memory>

// Forward declaration
class PanoramaConverter;

/**
 * @brief Skybox material for rendering environment cubemap
 * 
 * SkyboxMaterial manages a panorama texture (equirectangular) that is converted
 * to a cube texture for rendering. The conversion happens on-demand when the
 * cube texture is needed for rendering.
 * 
 * Rendering flow: Panorama (2D equirectangular) -> Cube Texture -> Skybox Rendering
 */
class SkyboxMaterial : public Material {
public:
    SkyboxMaterial();
    virtual ~SkyboxMaterial() override;

    virtual std::string_view get_asset_type_name() const override { return "Skybox Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::Skybox; }

    // Skybox parameters
    void set_intensity(float intensity) { intensity_ = intensity; }
    float get_intensity() const { return intensity_; }
    
    // Panorama texture (input - equirectangular 2D texture)
    void set_panorama_texture(TextureRef texture);
    TextureRef get_panorama_texture() const { return panorama_texture_; }
    
    // Cube texture (generated from panorama, cached)
    // This returns the generated cube texture, or nullptr if not yet generated
    TextureRef get_cube_texture() const { return cube_texture_; }
    
    // Ensure cube texture is up-to-date (called before rendering)
    // Returns true if cube texture is ready for rendering
    bool ensure_cube_texture_ready();
    
    // Check if cube texture needs to be regenerated
    bool is_cube_texture_dirty() const { return cube_texture_dirty_; }
    
    // Mark cube texture as dirty (force regeneration)
    void mark_cube_texture_dirty() { cube_texture_dirty_ = true; }

    // Asset dependencies - only panorama texture needs to be serialized
    // Note: cube_texture_ is generated at runtime, not serialized
    // Note: shaders are managed by SkyboxPass at runtime, not serialized
    ASSET_DEPS(
        (TextureRef, panorama_texture_)
    )

    // Sync deps before saving to ensure UIDs are correct
    virtual void on_save_asset() override { save_asset_deps(); }

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("intensity", intensity_));
        ar(cereal::make_nvp("cube_texture_resolution", cube_texture_resolution_));
    }

protected:
    // Generate cube texture from panorama using GPU
    void update_cube_texture();
    
    float intensity_ = 1.0f;
    uint32_t cube_texture_resolution_ = 512;  // Resolution of generated cubemap
    
    // Runtime state (not serialized)
    bool cube_texture_dirty_ = true;
    TextureRef cube_texture_;  // Generated at runtime
    
    // Panorama converter (lazy initialized)
    std::unique_ptr<PanoramaConverter> converter_;
};

using SkyboxMaterialRef = std::shared_ptr<SkyboxMaterial>;

CEREAL_REGISTER_TYPE(SkyboxMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, SkyboxMaterial)
