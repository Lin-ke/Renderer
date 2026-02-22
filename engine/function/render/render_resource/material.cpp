#include "engine/function/render/render_resource/material.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include <cstdint>

DEFINE_LOG_TAG(LogMaterial, "Material");

Material::Material() {
    // Initialize texture arrays with fixed sizes for bindless indexing
    texture_2d.resize(8);
    texture_cube.resize(4);
    texture_3d.resize(4);
    shaders.resize(3); // vertex, geometry, fragment
    
    // Allocate material ID from render resource manager
    if (EngineContext::render_resource()) {
        material_id_ = EngineContext::render_resource()->allocate_material_id();
    }
}

Material::~Material() {
    // Release material ID when render resource manager is available
    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->release_material_id(material_id_);
    }
}

void Material::on_load_asset() {
    load_asset_deps();
    
    // Ensure texture arrays have correct sizes after loading
    if (texture_2d.size() != 8) texture_2d.resize(8);
    if (texture_cube.size() != 4) texture_cube.resize(4);
    if (texture_3d.size() != 4) texture_3d.resize(4);
    if (shaders.size() != 3) shaders.resize(3);
    
    update();
}

void Material::on_save_asset() {
    save_asset_deps();
}

void Material::update() {
    material_info_ = {};
    material_info_.alpha_clip = alpha_clip_;
    material_info_.diffuse = diffuse_;
    material_info_.emission = emission_;

    // Set common texture IDs
    if (texture_diffuse) material_info_.texture_diffuse = texture_diffuse->texture_id_;
    if (texture_normal) material_info_.texture_normal = texture_normal->texture_id_;
    if (texture_specular) material_info_.texture_specular = texture_specular->texture_id_;

    // Set generic parameters
    material_info_.ints = ints_;
    material_info_.floats = floats_;
    material_info_.colors = colors_;

    // Set array texture IDs with bounds checking
    for (uint32_t i = 0; i < 8 && i < texture_2d.size(); ++i) {
        if (texture_2d[i]) material_info_.texture_2d[i] = texture_2d[i]->texture_id_;
    }
    for (uint32_t i = 0; i < 4 && i < texture_cube.size(); ++i) {
        if (texture_cube[i]) material_info_.texture_cube[i] = texture_cube[i]->texture_id_;
    }
    for (uint32_t i = 0; i < 4 && i < texture_3d.size(); ++i) {
        if (texture_3d[i]) material_info_.texture_3d[i] = texture_3d[i]->texture_id_;
    }

    // Submit material info to GPU when render resource manager is available
    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->set_material_info(material_info_, material_id_);
    }
}

// ============================================================================
// PBR Material
// ============================================================================

PBRMaterial::PBRMaterial() : Material() {
    update();
}

void PBRMaterial::update() {
    // Fill base parameters
    Material::update();

    // Fill PBR specific parameters
    material_info_.roughness = roughness_;
    material_info_.metallic = metallic_;

    if (texture_arm) material_info_.texture_arm = texture_arm->texture_id_;

    // Re-submit with PBR data
    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->set_material_info(material_info_, material_id_);
    }

    INFO(LogMaterial, "PBR Material updated: id={}, roughness={}, metallic={}", material_id_, roughness_, metallic_);
}

// ============================================================================
// NPR Material
// ============================================================================

NPRMaterial::NPRMaterial() : Material() {
    update();
}

void NPRMaterial::update() {
    // Fill base parameters
    Material::update();

    // Map NPR parameters to generic slots in MaterialInfo
    // Shader will need to access them via floats[0-4] and colors[0]
    material_info_.floats[0] = lambert_clamp_;
    material_info_.floats[1] = ramp_offset_;
    material_info_.floats[2] = rim_threshold_;
    material_info_.floats[3] = rim_strength_;
    material_info_.floats[4] = rim_width_;
    material_info_.colors[0] = Vec4(rim_color_.x(), rim_color_.y(), rim_color_.z(), 1.0f);

    // Map NPR textures
    if (texture_ramp) material_info_.texture_2d[0] = texture_ramp->texture_id_;
    if (texture_light_map) material_info_.texture_2d[1] = texture_light_map->texture_id_;

    // Re-submit with NPR data
    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->set_material_info(material_info_, material_id_);
    }

    INFO(LogMaterial, "NPR Material updated: id={}, rim_strength={}", material_id_, rim_strength_);
}

// Cereal registration for polymorphic serialization
CEREAL_REGISTER_TYPE(Material);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Material);
