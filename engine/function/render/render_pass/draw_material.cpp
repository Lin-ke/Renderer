#include "engine/function/render/render_pass/draw_material.h"
#include "engine/function/render/render_resource/texture.h"

namespace render {

static RHITextureRef rhi_texture(const TextureRef& texture) {
    return texture ? texture->texture_ : nullptr;
}

DrawMaterial snapshot_material(const Material& material) {
    DrawMaterial result;
    result.pass_mask = material.render_pass_mask();
    result.cull_mode = material.cull_mode();
    result.use_for_depth_pass = material.use_for_depth_pass();

    if (const auto* pbr = dynamic_cast<const PBRMaterial*>(&material)) {
        const std::array textures{
            pbr->get_diffuse_texture(), pbr->get_normal_texture(), pbr->get_arm_texture(),
            pbr->get_roughness_texture(), pbr->get_metallic_texture(), pbr->get_ao_texture(),
            pbr->get_emission_texture()
        };
        for (size_t i = 0; i < textures.size(); ++i) result.textures[i] = rhi_texture(textures[i]);
        result.pbr = GBufferMaterialData{
            pbr->get_diffuse(), pbr->get_roughness(), pbr->get_metallic(), pbr->get_emission().x,
            pbr->get_alpha_clip(), pbr->get_specular(),
            float(bool(textures[0])), float(bool(textures[1])), float(bool(textures[2])),
            float(bool(textures[3])), float(bool(textures[4])), float(bool(textures[5])), float(bool(textures[6]))
        };
    } else if (const auto* npr = dynamic_cast<const NPRMaterial*>(&material)) {
        const std::array textures{
            npr->get_diffuse_texture(), npr->get_normal_texture(), npr->get_light_map_texture(), npr->get_ramp_texture()
        };
        for (size_t i = 0; i < textures.size(); ++i) result.textures[i] = rhi_texture(textures[i]);
        auto& data = result.npr.emplace();
        data.albedo = npr->get_diffuse();
        data.emission = npr->get_emission();
        set_npr_params(data, npr->get_lambert_clamp(), npr->get_ramp_offset(),
            npr->get_rim_threshold(), npr->get_rim_strength(), npr->get_rim_width(),
            float(bool(textures[0])), float(bool(textures[1])), float(bool(textures[2])),
            npr->get_rim_color(), float(bool(textures[3])), float(npr->get_face_mode()));
    }
    return result;
}

} // namespace render
