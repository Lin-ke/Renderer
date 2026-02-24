#pragma once

#include "engine/core/math/math.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/function/asset/asset.h"
#include "engine/function/asset/asset_macros.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/log/Log.h"
#include <array>
#include <vector>
#include <cstdint>
#include <memory>
#include <cassert>
#include <cereal/access.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>

using RenderPassMasks = uint32_t;
enum RenderPassMaskBits {
    PASS_MASK_NONE = 0x00000000,
    PASS_MASK_FORWARD_PASS = 0x00000001,
    PASS_MASK_DEFERRED_PASS = 0x00000002,
    PASS_MASK_TRANSPARENT_PASS = 0x00000004,
    PASS_MASK_PBR_FORWARD = 0x00000008,
    PASS_MASK_NPR_FORWARD = 0x00000010,
    PASS_MASK_MAX_ENUM = 0x7FFFFFFF,
};

// Material type enum
enum class MaterialType {
    Base = 0,
    PBR = 1,
    NPR = 2,
    Skybox = 3,
};

class Texture;
using TextureRef = std::shared_ptr<Texture>;

/**
 * @brief Base Material class - lightweight, only rendering state
 * 
 * Material base class contains only common rendering pipeline states.
 * All material-specific properties (colors, textures) are managed by subclasses.
 * This allows for flexible material types:
 *   - PBRMaterial: diffuse, normal, arm textures
 *   - NPRMaterial: diffuse, normal, light_map, ramp textures  
 *   - SkyboxMaterial: cube texture only
 *   - UnlitMaterial: color only
 */
class Material : public Asset {
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    
    Material();
    Material(const Material& other) = default;
    virtual ~Material();

    virtual std::string_view get_asset_type_name() const override { return "Material Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Material; }
    virtual MaterialType get_material_type() const { return MaterialType::Base; }

    // Base class doesn't override on_load_asset/on_save_asset
    // Subclasses use ASSET_DEPS macro which generates load_asset_deps()/save_asset_deps()
    // Asset::on_load_asset() will call the generated load_asset_deps() automatically

    uint32_t get_material_id() const { return material_id_; }

    // Pipeline States - common to all materials
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

    // Base class has no asset dependencies - subclasses manage their own
    void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const override {}

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
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
    virtual void update() {}

    // Pipeline state only - no textures, no colors
    uint32_t render_queue_ = 1000;
    RenderPassMasks render_pass_mask_ = PASS_MASK_FORWARD_PASS;
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

using MaterialRef = std::shared_ptr<Material>;

// ============================================================================
// PBR Material - manages its own textures and parameters
// ============================================================================

class PBRMaterial : public Material {
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    
    PBRMaterial();
    virtual ~PBRMaterial() override = default;

    virtual std::string_view get_asset_type_name() const override { return "PBR Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::PBR; }

    // PBR parameters
    void set_diffuse(Vec4 diffuse) { diffuse_ = diffuse; update(); }
    void set_emission(Vec4 emission) { emission_ = emission; update(); }
    void set_alpha_clip(float alpha_clip) { alpha_clip_ = alpha_clip; update(); }
    void set_roughness(float roughness) { roughness_ = roughness; update(); }
    void set_metallic(float metallic) { metallic_ = metallic; update(); }
    
    // Generic parameters
    void set_int(int32_t data, uint32_t index) { 
        assert(index < 8 && "PBRMaterial: int index out of range");
        ints_[index] = data;
        update();
    }
    void set_float(float data, uint32_t index) { 
        assert(index < 8 && "PBRMaterial: float index out of range");
        floats_[index] = data;
        update();
    }
    void set_color(Vec4 data, uint32_t index) { 
        assert(index < 8 && "PBRMaterial: color index out of range");
        colors_[index] = data;
        update();
    }
    
    // PBR textures
    void set_diffuse_texture(TextureRef texture) { texture_diffuse_ = texture; update(); }
    void set_normal_texture(TextureRef texture) { texture_normal_ = texture; update(); }
    void set_arm_texture(TextureRef texture) { texture_arm_ = texture; update(); }
    
    // Getters
    inline Vec4 get_diffuse() const { return diffuse_; }
    inline Vec4 get_emission() const { return emission_; }
    inline float get_alpha_clip() const { return alpha_clip_; }
    inline float get_roughness() const { return roughness_; }
    inline float get_metallic() const { return metallic_; }
    inline int32_t get_int(uint32_t index) const { return ints_[index]; }
    inline float get_float(uint32_t index) const { return floats_[index]; }
    inline Vec4 get_color(uint32_t index) const { return colors_[index]; }
    inline TextureRef get_diffuse_texture() const { return texture_diffuse_; }
    inline TextureRef get_normal_texture() const { return texture_normal_; }
    inline TextureRef get_arm_texture() const { return texture_arm_; }

    // PBR manages its own texture dependencies
    ASSET_DEPS(
        (TextureRef, texture_diffuse_),
        (TextureRef, texture_normal_),
        (TextureRef, texture_arm_)
    )

    // Sync deps before saving to ensure UIDs are correct
    virtual void on_save_asset() override { save_asset_deps(); }

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("diffuse", diffuse_));
        ar(cereal::make_nvp("emission", emission_));
        ar(cereal::make_nvp("alpha_clip", alpha_clip_));
        ar(cereal::make_nvp("roughness", roughness_));
        ar(cereal::make_nvp("metallic", metallic_));
        ar(cereal::make_nvp("ints", ints_));
        ar(cereal::make_nvp("floats", floats_));
        ar(cereal::make_nvp("colors", colors_));
    }

protected:
    virtual void update() override;

    // PBR-specific properties
    Vec4 diffuse_ = Vec4::Ones();
    Vec4 emission_ = Vec4::Zero();
    float alpha_clip_ = 0.0f;
    float roughness_ = 0.5f;
    float metallic_ = 0.0f;

    std::array<int32_t, 8> ints_ = { 0 };
    std::array<float, 8> floats_ = { 0.0f };
    std::array<Vec4, 8> colors_ = { Vec4::Zero() };
};

using PBRMaterialRef = std::shared_ptr<PBRMaterial>;

// ============================================================================
// NPR Material - manages its own textures and parameters
// ============================================================================

class NPRMaterial : public Material {
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    
    NPRMaterial();
    virtual ~NPRMaterial() override = default;

    virtual std::string_view get_asset_type_name() const override { return "NPR Material Asset"; }
    virtual MaterialType get_material_type() const override { return MaterialType::NPR; }

    // NPR parameters
    void set_diffuse(Vec4 diffuse) { diffuse_ = diffuse; update(); }
    void set_emission(Vec4 emission) { emission_ = emission; update(); }
    void set_alpha_clip(float alpha_clip) { alpha_clip_ = alpha_clip; update(); }
    void set_lambert_clamp(float value) { lambert_clamp_ = value; update(); }
    void set_ramp_offset(float value) { ramp_offset_ = value; update(); }
    void set_rim_threshold(float value) { rim_threshold_ = value; update(); }
    void set_rim_strength(float value) { rim_strength_ = value; update(); }
    void set_rim_width(float value) { rim_width_ = value; update(); }
    void set_rim_color(Vec3 value) { rim_color_ = value; update(); }
    
    // Generic parameters (for shader access)
    void set_int(int32_t data, uint32_t index) { 
        assert(index < 8 && "NPRMaterial: int index out of range");
        ints_[index] = data;
        update();
    }
    void set_float(float data, uint32_t index) { 
        assert(index < 8 && "NPRMaterial: float index out of range");
        floats_[index] = data;
        update();
    }
    void set_color(Vec4 data, uint32_t index) { 
        assert(index < 8 && "NPRMaterial: color index out of range");
        colors_[index] = data;
        update();
    }
    
    // NPR textures
    void set_diffuse_texture(TextureRef texture) { texture_diffuse_ = texture; update(); }
    void set_normal_texture(TextureRef texture) { texture_normal_ = texture; update(); }
    void set_light_map_texture(TextureRef texture) { texture_light_map_ = texture; update(); }
    void set_ramp_texture(TextureRef texture) { texture_ramp_ = texture; update(); }
    
    // Getters
    inline Vec4 get_diffuse() const { return diffuse_; }
    inline Vec4 get_emission() const { return emission_; }
    inline float get_alpha_clip() const { return alpha_clip_; }
    inline float get_lambert_clamp() const { return lambert_clamp_; }
    inline float get_ramp_offset() const { return ramp_offset_; }
    inline float get_rim_threshold() const { return rim_threshold_; }
    inline float get_rim_strength() const { return rim_strength_; }
    inline float get_rim_width() const { return rim_width_; }
    inline Vec3 get_rim_color() const { return rim_color_; }
    inline int32_t get_int(uint32_t index) const { return ints_[index]; }
    inline float get_float(uint32_t index) const { return floats_[index]; }
    inline Vec4 get_color(uint32_t index) const { return colors_[index]; }
    inline TextureRef get_diffuse_texture() const { return texture_diffuse_; }
    inline TextureRef get_normal_texture() const { return texture_normal_; }
    inline TextureRef get_light_map_texture() const { return texture_light_map_; }
    inline TextureRef get_ramp_texture() const { return texture_ramp_; }

    // NPR manages its own texture dependencies (only what it uses)
    ASSET_DEPS(
        (TextureRef, texture_diffuse_),
        (TextureRef, texture_normal_),
        (TextureRef, texture_light_map_),
        (TextureRef, texture_ramp_)
    )

    // Sync deps before saving to ensure UIDs are correct
    virtual void on_save_asset() override { save_asset_deps(); }

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Material>(this));
        serialize_deps(ar);
        ar(cereal::make_nvp("diffuse", diffuse_));
        ar(cereal::make_nvp("emission", emission_));
        ar(cereal::make_nvp("alpha_clip", alpha_clip_));
        ar(cereal::make_nvp("lambert_clamp", lambert_clamp_));
        ar(cereal::make_nvp("ramp_offset", ramp_offset_));
        ar(cereal::make_nvp("rim_threshold", rim_threshold_));
        ar(cereal::make_nvp("rim_strength", rim_strength_));
        ar(cereal::make_nvp("rim_width", rim_width_));
        ar(cereal::make_nvp("rim_color", rim_color_));
        ar(cereal::make_nvp("ints", ints_));
        ar(cereal::make_nvp("floats", floats_));
        ar(cereal::make_nvp("colors", colors_));
    }

protected:
    virtual void update() override;

    // NPR-specific properties
    Vec4 diffuse_ = Vec4::Ones();
    Vec4 emission_ = Vec4::Zero();
    float alpha_clip_ = 0.0f;

    // NPR parameters
    float lambert_clamp_ = 0.5f;
    float ramp_offset_ = 0.0f;
    float rim_threshold_ = 0.1f;
    float rim_strength_ = 1.0f;
    float rim_width_ = 0.5f;
    Vec3 rim_color_ = Vec3(1.0f, 1.0f, 1.0f);

    // Generic parameter slots (for shader communication)
    std::array<int32_t, 8> ints_ = { 0 };
    std::array<float, 8> floats_ = { 0.0f };
    std::array<Vec4, 8> colors_ = { Vec4::Zero() };
};

using NPRMaterialRef = std::shared_ptr<NPRMaterial>;

CEREAL_REGISTER_TYPE(PBRMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, PBRMaterial)
CEREAL_REGISTER_TYPE(NPRMaterial)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Material, NPRMaterial)
