#pragma once

#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/asset/asset_macros.h"
#include <cereal/access.hpp>

/**
 * @brief Skybox material for rendering environment cubemap
 * 
 * SkyboxMaterial manages a cube texture for environment rendering.
 * It uses special pipeline states: no depth write, no culling, and high render queue.
 */
class SkyboxMaterial : public Material {
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    
    SkyboxMaterial();
    virtual ~SkyboxMaterial() override = default;

    virtual std::string_view get_asset_type_name() const override { return "Skybox Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::Skybox; }

    // Skybox parameters
    void set_intensity(float intensity) { intensity_ = intensity; update(); }
    float get_intensity() const { return intensity_; }
    
    // Cube texture for skybox
    void set_cube_texture(TextureRef texture);
    TextureRef get_cube_texture() const { return cube_texture_; }

    // Shader accessors
    void set_vertex_shader(ShaderRef shader) { vertex_shader_ = shader; update(); }
    void set_fragment_shader(ShaderRef shader) { fragment_shader_ = shader; update(); }
    ShaderRef get_vertex_shader() const { return vertex_shader_; }
    ShaderRef get_fragment_shader() const { return fragment_shader_; }

    // Asset dependencies - cube texture and shaders
    // Note: ASSET_DEPS macro declares the member variables
    ASSET_DEPS(
        (TextureRef, cube_texture_),
        (ShaderRef, vertex_shader_),
        (ShaderRef, fragment_shader_)
    )

    // Sync deps before saving to ensure UIDs are correct
    virtual void on_save_asset() override { save_asset_deps(); }

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("intensity", intensity_));
    }

protected:
    virtual void update() override;
    
    float intensity_ = 1.0f;
};

using SkyboxMaterialRef = std::shared_ptr<SkyboxMaterial>;

CEREAL_REGISTER_TYPE(SkyboxMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, SkyboxMaterial)
