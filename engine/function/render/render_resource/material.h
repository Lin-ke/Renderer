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

typedef std::vector<TextureRef> TextureRefList;
typedef std::vector<ShaderRef> ShaderRefList;

class Material : public Asset {
public:
    Material();
    Material(const Material& other) = default;
    ~Material();

    virtual std::string_view get_asset_type_name() const override { return "Material Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Material; }

    virtual void on_load_asset() override;
    virtual void on_save_asset() override;

    uint32_t get_material_id() { return material_id_; }

    // Parameters
    void set_diffuse(Vec4 diffuse) { this->diffuse_ = diffuse; update(); }
    void set_emission(Vec4 emission) { this->emission_ = emission; update(); }
    void set_roughness(float roughness) { this->roughness_ = roughness; update(); }
    void set_metallic(float metallic) { this->metallic_ = metallic; update(); }
    void set_alpha_clip(float alpha_clip) { this->alpha_clip_ = alpha_clip; update(); }
    void set_int(int32_t data, uint32_t index) { ints_[index] = data; update(); }
    void set_float(float data, uint32_t index) { floats_[index] = data; update(); }
    void set_color(Vec4 data, uint32_t index) { colors_[index] = data; update(); }
    
    // Textures
    void set_diffuse_texture(TextureRef texture) { texture_diffuse = texture; update(); }
    void set_normal_texture(TextureRef texture) { texture_normal = texture; update(); }
    void set_arm_texture(TextureRef texture) { texture_arm = texture; update(); }
    void set_specular_texture(TextureRef texture) { texture_specular = texture; update(); }
    
    void set_texture_2d(TextureRef texture, uint32_t index) { 
        if (index < texture_2d.size()) texture_2d[index] = texture; 
        update(); 
    }
    void set_texture_cube(TextureRef texture, uint32_t index) { 
        if (index < texture_cube.size()) texture_cube[index] = texture; 
        update(); 
    }
    void set_texture_3d(TextureRef texture, uint32_t index) { 
        if (index < texture_3d.size()) texture_3d[index] = texture; 
        update(); 
    }
    
    // Shaders
    void set_vertex_shader(ShaderRef shader) { shaders[0] = shader; }
    void set_geometry_shader(ShaderRef shader) { shaders[1] = shader; }
    void set_fragment_shader(ShaderRef shader) { shaders[2] = shader; }

    // Getters
    inline Vec4 get_diffuse() const { return this->diffuse_; }
    inline Vec4 get_emission() const { return this->emission_; }
    inline float get_roughness() const { return this->roughness_; }
    inline float get_metallic() const { return this->metallic_; }
    inline float get_alpha_clip() const { return this->alpha_clip_; }
    inline int32_t get_int(uint32_t index) const { return ints_[index]; }
    inline float get_float(uint32_t index) const { return floats_[index]; }
    inline Vec4 get_color(uint32_t index) const { return colors_[index]; }
    
    inline ShaderRef get_vertex_shader() const { return shaders[0]; }
    inline ShaderRef get_geometry_shader() const { return shaders[1]; }
    inline ShaderRef get_fragment_shader() const { return shaders[2]; }

    inline TextureRef get_diffuse_texture() const { return texture_diffuse; }
    inline const std::vector<TextureRef>& get_texture_2d_list() const { return texture_2d; }

    // Pipeline States
    uint32_t render_queue() { return render_queue_; }
    RenderPassMasks render_pass_mask() { return render_pass_mask_; }
    RasterizerCullMode cull_mode() { return cull_mode_; }
    RasterizerFillMode fill_mode() { return fill_mode_; }
    bool depth_test() { return depth_test_ ;}
    bool depth_write() { return depth_write_; }
    CompareFunction depth_compare() { return depth_compare_; }
    bool use_for_depth_pass() { return use_for_depth_pass_; }
    bool cast_shadow() { return cast_shadow_; }

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
        (TextureRef, texture_arm),
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
        ar(cereal::make_nvp("roughness", roughness_));
        ar(cereal::make_nvp("metallic", metallic_));
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
    Vec4 diffuse_ = Vec4::Ones();
    Vec4 emission_ = Vec4::Zero();
    float roughness_ = 0.5f;
    float metallic_ = 0.0f;
    float alpha_clip_ = 0.0f;

    std::array<int32_t, 8> ints_ = { 0 };
    std::array<float, 8> floats_ = { 0.0f };
    std::array<Vec4, 8> colors_ = { Vec4::Zero() };

    void update();

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
using MaterialRef = std::shared_ptr<Material>;
