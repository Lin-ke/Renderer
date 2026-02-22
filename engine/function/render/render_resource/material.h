#pragma once

#include "engine/core/math/math.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/asset/asset.h"
#include "engine/function/asset/asset_macros.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/log/Log.h"
#include <array>
#include <vector>
#include <cstdint>
#include <memory>
#include <cereal/access.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>

using RenderPassMasks = uint32_t;
enum RenderPassMaskBits {
    PASS_MASK_NONE = 0x00000000,
    PASS_MASK_FORWARD_PASS = 0x00000001,
    PASS_MASK_DEFERRED_PASS = 0x00000002,
    PASS_MASK_TRANSPARENT_PASS = 0x00000004,
    PASS_MASK_MAX_ENUM = 0x7FFFFFFF,
};

// Material type enum
enum class MaterialType {
    Base = 0,
    PBR = 1,
    NPR = 2,
};

typedef std::vector<TextureRef> TextureRefList;
typedef std::vector<ShaderRef> ShaderRefList;

/**
 * @brief Base Material class
 * Contains common properties for all material types
 */
class Material : public Asset {
public:
    Material();
    Material(const Material& other) = default;
    virtual ~Material();

    virtual std::string_view get_asset_type_name() const override { return "Material Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Material; }
    virtual MaterialType get_material_type() const { return MaterialType::Base; }

    virtual void on_load_asset() override;
    virtual void on_save_asset() override;

    uint32_t get_material_id() const { return material_id_; }

    // Common parameters
    void set_diffuse(Vec4 diffuse) { this->diffuse_ = diffuse; update(); }
    void set_emission(Vec4 emission) { this->emission_ = emission; update(); }
    void set_alpha_clip(float alpha_clip) { this->alpha_clip_ = alpha_clip; update(); }
    
    // Generic parameters
    void set_int(int32_t data, uint32_t index) { if (index < 8) ints_[index] = data; update(); }
    void set_float(float data, uint32_t index) { if (index < 8) floats_[index] = data; update(); }
    void set_color(Vec4 data, uint32_t index) { if (index < 8) colors_[index] = data; update(); }
    
    // Common textures
    void set_diffuse_texture(TextureRef texture) { texture_diffuse = texture; update(); }
    void set_normal_texture(TextureRef texture) { texture_normal = texture; update(); }
    void set_specular_texture(TextureRef texture) { texture_specular = texture; update(); }
    
    void set_texture_2d(TextureRef texture, uint32_t index) { 
        if (index < texture_2d.size()) texture_2d[index] = texture; 
        update(); 
    }
    void set_texture_cube(TextureRef texture, uint32_t index) { 
        if (index < texture_cube.size()) texture_cube[index] = texture; 
        update(); 
    }
    
    // Shaders
    void set_vertex_shader(ShaderRef shader) { shaders[0] = shader; update(); }
    void set_geometry_shader(ShaderRef shader) { shaders[1] = shader; update(); }
    void set_fragment_shader(ShaderRef shader) { shaders[2] = shader; update(); }

    // Getters
    inline Vec4 get_diffuse() const { return this->diffuse_; }
    inline Vec4 get_emission() const { return this->emission_; }
    inline float get_alpha_clip() const { return this->alpha_clip_; }
    inline int32_t get_int(uint32_t index) const { return ints_[index]; }
    inline float get_float(uint32_t index) const { return floats_[index]; }
    inline Vec4 get_color(uint32_t index) const { return colors_[index]; }
    
    inline ShaderRef get_vertex_shader() const { return shaders[0]; }
    inline ShaderRef get_geometry_shader() const { return shaders[1]; }
    inline ShaderRef get_fragment_shader() const { return shaders[2]; }

    inline TextureRef get_diffuse_texture() const { return texture_diffuse; }
    inline TextureRef get_normal_texture() const { return texture_normal; }
    inline TextureRef get_specular_texture() const { return texture_specular; }
    inline const std::vector<TextureRef>& get_texture_2d_list() const { return texture_2d; }

    // Pipeline States
    uint32_t render_queue() const { return render_queue_; }
    RenderPassMasks render_pass_mask() const { return render_pass_mask_; }
    RasterizerCullMode cull_mode() const { return cull_mode_; }
    RasterizerFillMode fill_mode() const { return fill_mode_; }
    bool depth_test() const { return depth_test_ ;}
    bool depth_write() const { return depth_write_; }
    CompareFunction depth_compare() const { return depth_compare_; }
    bool use_for_depth_pass() const { return use_for_depth_pass_; }
    bool cast_shadow() const { return cast_shadow_; }

    void set_render_queue(uint32_t queue) { render_queue_ = queue; }
    void set_render_pass_mask(RenderPassMasks mask) { render_pass_mask_ = mask; }
    void set_cull_mode(RasterizerCullMode cull) { cull_mode_ = cull; }
    void set_fill_mode(RasterizerFillMode fill) { fill_mode_ = fill; }
    void set_depth_test(bool test) { depth_test_ = test;}
    void set_depth_write(bool write) { depth_write_ = write; }
    void set_depth_compare(CompareFunction compare) { depth_compare_ = compare; }
    void set_use_for_depth_pass(bool use) { use_for_depth_pass_ = use; }
    void set_cast_shadow(bool shadow) { cast_shadow_ = shadow; }

    ASSET_DEPS(
        (TextureRef, texture_diffuse),
        (TextureRef, texture_normal),
        (TextureRef, texture_specular),
        (TextureRefList, texture_2d),
        (TextureRefList, texture_cube),
        (TextureRefList, texture_3d),
        (ShaderRefList, shaders)
    )

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("diffuse", diffuse_));
        ar(cereal::make_nvp("emission", emission_));
        ar(cereal::make_nvp("alpha_clip", alpha_clip_));
        ar(cereal::make_nvp("ints", ints_));
        ar(cereal::make_nvp("floats", floats_));
        ar(cereal::make_nvp("colors", colors_));
        ar(cereal::make_nvp("render_queue", render_queue_));
        ar(cereal::make_nvp("render_pass_mask", render_pass_mask_));
        ar(cereal::make_nvp("cull_mode", cull_mode_));
        ar(cereal::make_nvp("fill_mode", fill_mode_));
        ar(cereal::make_nvp("depth_test", depth_test_));
        ar(cereal::make_nvp("depth_write", depth_write_));
        ar(cereal::make_nvp("depth_compare", depth_compare_));
        ar(cereal::make_nvp("use_for_depth_pass", use_for_depth_pass_));
        ar(cereal::make_nvp("cast_shadow", cast_shadow_));
    }

protected:
    virtual void update();

    Vec4 diffuse_ = Vec4::Ones();
    Vec4 emission_ = Vec4::Zero();
    float alpha_clip_ = 0.0f;

    std::array<int32_t, 8> ints_ = { 0 };
    std::array<float, 8> floats_ = { 0.0f };
    std::array<Vec4, 8> colors_ = { Vec4::Zero() };

    uint32_t render_queue_ = 1000;
    RenderPassMasks render_pass_mask_ = PASS_MASK_DEFERRED_PASS;
    RasterizerCullMode cull_mode_ = CULL_MODE_BACK;
    RasterizerFillMode fill_mode_ = FILL_MODE_SOLID;
    bool depth_test_ = true;
    bool depth_write_ = true;
    CompareFunction depth_compare_ = COMPARE_FUNCTION_LESS_EQUAL;
    bool use_for_depth_pass_ = true;
    bool cast_shadow_ = true;

    MaterialInfo material_info_;
    uint32_t material_id_ = 0;
};

/**
 * @brief PBR Material class
 * Physically Based Rendering material with metallic/roughness workflow
 */
class PBRMaterial : public Material {
public:
    PBRMaterial();
    virtual ~PBRMaterial() override = default;

    virtual std::string_view get_asset_type_name() const override { return "PBR Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::PBR; }

    // PBR parameters
    void set_roughness(float roughness) { this->roughness_ = roughness; update(); }
    void set_metallic(float metallic) { this->metallic_ = metallic; update(); }
    
    // PBR textures
    void set_arm_texture(TextureRef texture) { texture_arm = texture; update(); }
    
    // Getters
    inline float get_roughness() const { return this->roughness_; }
    inline float get_metallic() const { return this->metallic_; }
    inline TextureRef get_arm_texture() const { return texture_arm; }

    ASSET_DEPS(
        (TextureRef, texture_arm)
    )

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("roughness", roughness_));
        ar(cereal::make_nvp("metallic", metallic_));
    }

protected:
    virtual void update() override;

    float roughness_ = 0.5f;
    float metallic_ = 0.0f;
};

/**
 * @brief NPR Material class
 * Non-Photorealistic Rendering material for toon/cel shading
 */
class NPRMaterial : public Material {
public:
    NPRMaterial();
    virtual ~NPRMaterial() override = default;

    virtual std::string_view get_asset_type_name() const override { return "NPR Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::NPR; }

    // NPR parameters
    void set_lambert_clamp(float value) { lambert_clamp_ = value; update(); }
    void set_ramp_offset(float value) { ramp_offset_ = value; update(); }
    void set_rim_threshold(float value) { rim_threshold_ = value; update(); }
    void set_rim_strength(float value) { rim_strength_ = value; update(); }
    void set_rim_width(float value) { rim_width_ = value; update(); }
    void set_rim_color(Vec3 value) { rim_color_ = value; update(); }
    
    // NPR textures
    void set_light_map_texture(TextureRef texture) { texture_light_map = texture; update(); }
    void set_ramp_texture(TextureRef texture) { texture_ramp = texture; update(); }
    
    // Getters
    inline float get_lambert_clamp() const { return lambert_clamp_; }
    inline float get_ramp_offset() const { return ramp_offset_; }
    inline float get_rim_threshold() const { return rim_threshold_; }
    inline float get_rim_strength() const { return rim_strength_; }
    inline float get_rim_width() const { return rim_width_; }
    inline Vec3 get_rim_color() const { return rim_color_; }
    inline TextureRef get_light_map_texture() const { return texture_light_map; }
    inline TextureRef get_ramp_texture() const { return texture_ramp; }

    ASSET_DEPS(
        (TextureRef, texture_light_map),
        (TextureRef, texture_ramp)
    )

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("lambert_clamp", lambert_clamp_));
        ar(cereal::make_nvp("ramp_offset", ramp_offset_));
        ar(cereal::make_nvp("rim_threshold", rim_threshold_));
        ar(cereal::make_nvp("rim_strength", rim_strength_));
        ar(cereal::make_nvp("rim_width", rim_width_));
        ar(cereal::make_nvp("rim_color", rim_color_));
    }

protected:
    virtual void update() override;

    // NPR parameters
    float lambert_clamp_ = 0.5f;     // Half lambert clamp threshold
    float ramp_offset_ = 0.0f;        // Ramp texture vertical offset
    float rim_threshold_ = 0.1f;      // Rim light depth threshold
    float rim_strength_ = 1.0f;       // Rim light intensity
    float rim_width_ = 0.5f;          // Rim light screen space width
    Vec3 rim_color_ = Vec3(1.0f, 1.0f, 1.0f);  // Rim light color
};

using MaterialRef = std::shared_ptr<Material>;
using PBRMaterialRef = std::shared_ptr<PBRMaterial>;
using NPRMaterialRef = std::shared_ptr<NPRMaterial>;

CEREAL_REGISTER_TYPE(PBRMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, PBRMaterial)
CEREAL_REGISTER_TYPE(NPRMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, NPRMaterial)
