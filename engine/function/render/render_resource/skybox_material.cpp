#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/render/render_resource/panorama_converter.h"
#include "engine/core/log/Log.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"

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

SkyboxMaterial::~SkyboxMaterial() {
    // cube_texture_ will be released automatically
}

void SkyboxMaterial::set_panorama_texture(TextureRef texture) {
    if (texture && texture->get_texture_type() != TextureType::Texture2D) {
        ERR(LogSkyboxMaterial, "SkyboxMaterial panorama requires a 2D equirectangular texture!");
        return;
    }
    
    // Only mark dirty if texture actually changed
    if (panorama_texture_ != texture) {
        panorama_texture_ = texture;
        cube_texture_dirty_ = true;
    }
}

bool SkyboxMaterial::ensure_cube_texture_ready() {
    if (!panorama_texture_) {
        return false;
    }
    
    if (cube_texture_ && !cube_texture_dirty_) {
        return true;  // Already up to date
    }
    
    // Need to generate/update cube texture
    update_cube_texture();
    
    return cube_texture_ != nullptr;
}

void SkyboxMaterial::update_cube_texture() {
    if (!panorama_texture_) {
        return;
    }
    
    auto rhi = EngineContext::rhi();
    if (!rhi) {
        return;
    }
    
    // Initialize converter if needed
    if (!converter_) {
        converter_ = std::make_unique<PanoramaConverter>();
        if (!converter_->init()) {
            ERR(LogSkyboxMaterial, "Failed to initialize PanoramaConverter");
            converter_.reset();
            return;
        }
    }
    
    // Perform conversion
    cube_texture_ = converter_->convert(panorama_texture_, cube_texture_resolution_);
    
    if (cube_texture_) {
        cube_texture_dirty_ = false;
        INFO(LogSkyboxMaterial, "Cube texture updated from panorama (resolution: {})", cube_texture_resolution_);
    } else {
        ERR(LogSkyboxMaterial, "Failed to convert panorama to cubemap");
    }
}
