#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/core/log/Log.h"

DEFINE_LOG_TAG(LogSkyboxMaterial, "SkyboxMaterial");

SkyboxMaterial::SkyboxMaterial() {
    // Skybox uses special pipeline states
    set_render_queue(10000);              // Render last
    set_render_pass_mask(PASS_MASK_FORWARD_PASS);
    set_cull_mode(CULL_MODE_NONE);        // No culling - we see inside the cube
    set_depth_test(true);                 // Enable depth test
    set_depth_write(false);               // No depth write - skybox is at infinity
    set_depth_compare(COMPARE_FUNCTION_LESS_EQUAL);
    set_use_for_depth_pass(false);        // Don't render in depth pass
    set_cast_shadow(false);               // Skybox doesn't cast shadows
}

void SkyboxMaterial::set_cube_texture(TextureRef texture) {
    if (texture && texture->get_texture_type() != TextureType::TextureCube) {
        ERR(LogSkyboxMaterial, "SkyboxMaterial requires a cube texture!");
        return;
    }
    cube_texture_ = texture;
    update();
}

void SkyboxMaterial::update() {
    // Update material info for GPU
    // Note: MaterialInfo is typically updated by the render system
    // based on the material's properties
}
