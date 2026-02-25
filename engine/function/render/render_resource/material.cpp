#include "engine/function/render/render_resource/material.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include <cstdint>

DEFINE_LOG_TAG(LogMaterial, "Material");

// ============================================================================
// Base Material
// ============================================================================

Material::Material() {
    if (EngineContext::render_resource()) {
        material_id_ = EngineContext::render_resource()->allocate_material_id();
    }
}

Material::~Material() {
    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->release_material_id(material_id_);
    }
}

PBRMaterial::PBRMaterial() : Material() {
    render_pass_mask_ = PASS_MASK_PBR_FORWARD;
    update();
}

void PBRMaterial::update() {
    material_info_ = {};
    material_info_.alpha_clip = alpha_clip_;
    material_info_.diffuse = diffuse_;
    material_info_.emission = emission_;
    material_info_.roughness = roughness_;
    material_info_.metallic = metallic_;

    if (texture_diffuse_ && texture_diffuse_->texture_id_ != 0) {
        material_info_.texture_diffuse = texture_diffuse_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_diffuse = EngineContext::render_resource()->get_default_black_texture()->texture_id_;
    }
    
    if (texture_normal_ && texture_normal_->texture_id_ != 0) {
        material_info_.texture_normal = texture_normal_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_normal = EngineContext::render_resource()->get_default_normal_texture()->texture_id_;
    }
    
    if (texture_arm_ && texture_arm_->texture_id_ != 0) {
        material_info_.texture_arm = texture_arm_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_arm = EngineContext::render_resource()->get_default_white_texture()->texture_id_;
    }

    material_info_.ints = ints_;
    material_info_.floats = floats_;
    material_info_.colors = colors_;

    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->set_material_info(material_info_, material_id_);
    }

    INFO(LogMaterial, "PBR Material updated: id={}, roughness={}, metallic={}", material_id_, roughness_, metallic_);
}

NPRMaterial::NPRMaterial() : Material() {
    render_pass_mask_ = PASS_MASK_NPR_FORWARD;
    update();
}

void NPRMaterial::update() {
    material_info_ = {};
    material_info_.alpha_clip = alpha_clip_;
    material_info_.diffuse = diffuse_;
    material_info_.emission = emission_;

    if (texture_diffuse_ && texture_diffuse_->texture_id_ != 0) {
        material_info_.texture_diffuse = texture_diffuse_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_diffuse = EngineContext::render_resource()->get_default_black_texture()->texture_id_;
    }
    
    if (texture_normal_ && texture_normal_->texture_id_ != 0) {
        material_info_.texture_normal = texture_normal_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_normal = EngineContext::render_resource()->get_default_normal_texture()->texture_id_;
    }
    
    if (texture_ramp_ && texture_ramp_->texture_id_ != 0) {
        material_info_.texture_2d[0] = texture_ramp_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_2d[0] = EngineContext::render_resource()->get_default_white_texture()->texture_id_;
    }
    
    if (texture_light_map_ && texture_light_map_->texture_id_ != 0) {
        material_info_.texture_2d[1] = texture_light_map_->texture_id_;
    } else if (EngineContext::render_resource()) {
        material_info_.texture_2d[1] = EngineContext::render_resource()->get_default_white_texture()->texture_id_;
    }

    material_info_.floats[0] = lambert_clamp_;
    material_info_.floats[1] = ramp_offset_;
    material_info_.floats[2] = rim_threshold_;
    material_info_.floats[3] = rim_strength_;
    material_info_.floats[4] = rim_width_;
    material_info_.colors[0] = Vec4(rim_color_.x, rim_color_.y, rim_color_.z, 1.0f);

    material_info_.ints = ints_;
    for (uint32_t i = 5; i < 8; ++i) {
        material_info_.floats[i] = floats_[i];
    }
    for (uint32_t i = 1; i < 8; ++i) {
        material_info_.colors[i] = colors_[i];
    }

    if (material_id_ != 0 && EngineContext::render_resource()) {
        EngineContext::render_resource()->set_material_info(material_info_, material_id_);
    }

    INFO(LogMaterial, "NPR Material updated: id={}, rim_strength={}", material_id_, rim_strength_);
}

CEREAL_REGISTER_TYPE(Material);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Material);
