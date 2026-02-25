#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/world.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

DEFINE_LOG_TAG(LogSkyboxComponent, "SkyboxComponent");

SkyboxComponent::SkyboxComponent() {
}

SkyboxComponent::~SkyboxComponent() {
}

void SkyboxComponent::on_init() {
    Component::on_init();
    
    if (!material_) {
        // Create default material if none set
        material_ = std::make_shared<SkyboxMaterial>();
        material_->set_intensity(intensity_);
    }
    
    initialized_ = true;
}

void SkyboxComponent::on_update(float delta_time) {
    if (!initialized_) {
        on_init();
    }

    if (!material_ || !mesh_) {
        return;
    }

    // Update material intensity
    material_->set_intensity(intensity_);
    
    // Ensure cube texture is ready for rendering
    // This will convert panorama to cubemap if needed
    material_->ensure_cube_texture_ready();

    // Skybox follows camera position
    update_transform();
}

void SkyboxComponent::set_material(SkyboxMaterialRef material) {
    material_ = material;
}

void SkyboxComponent::set_panorama_texture(TextureRef texture) {
    if (!texture) {
        ERR(LogSkyboxComponent, "set_panorama_texture: texture is null");
        return;
    }

    if (texture->get_texture_type() != TextureType::Texture2D) {
        ERR(LogSkyboxComponent, "SkyboxComponent requires a 2D equirectangular panorama texture!");
        return;
    }

    if (!material_) {
        material_ = std::make_shared<SkyboxMaterial>();
        material_->set_intensity(intensity_);
    }
    
    material_->set_panorama_texture(texture);
}

void SkyboxComponent::set_intensity(float intensity) {
    intensity_ = intensity;
    if (material_) {
        material_->set_intensity(intensity);
    }
}

void SkyboxComponent::set_cube_texture_resolution(uint32_t resolution) {
    cube_texture_resolution_ = resolution;
    if (material_) {
        // Mark cube texture as dirty so it regenerates with new resolution
        material_->mark_cube_texture_dirty();
    }
}

uint32_t SkyboxComponent::get_material_id() const {
    return material_ ? material_->get_material_id() : 0;
}

void SkyboxComponent::collect_draw_batch(std::vector<render::DrawBatch>& batches) {
    if (!material_ || !mesh_) {
        return;
    }

    // Ensure cube texture is ready before rendering
    if (!material_->ensure_cube_texture_ready()) {
        return;  // Can't render without valid cube texture
    }

    render::DrawBatch batch;
    batch.object_id = 0;  // Skybox doesn't need object ID for standard rendering
    batch.vertex_buffer = mesh_->get_vertex_buffer()->position_buffer_;
    batch.normal_buffer = nullptr;  // Skybox doesn't need normals
    batch.tangent_buffer = nullptr;
    batch.texcoord_buffer = nullptr;
    batch.index_buffer = mesh_->get_index_buffer()->buffer_;
    batch.index_count = mesh_->get_index_count();
    batch.index_offset = 0;
    batch.model_matrix = Mat4::Identity();
    batch.inv_model_matrix = Mat4::Identity();
    batch.material = material_;

    batches.push_back(batch);
}


void SkyboxComponent::update_transform() {
    // Get active camera position
    auto* world = EngineContext::world();
    if (!world) return;
    
    auto* scene = world->get_active_scene();
    if (!scene) return;
    
    CameraComponent* camera = scene->get_camera();
    if (!camera) return;

    // Position the skybox at camera position
    Vec3 camera_pos = camera->get_position();
    
    // Update the owner's transform if available
    if (owner_) {
        auto* transform_comp = owner_->get_component<TransformComponent>();
        if (transform_comp) {
            transform_comp->transform.set_position(camera_pos);
            transform_comp->transform.set_scale(Vec3(skybox_scale_, skybox_scale_, skybox_scale_));
        }
    }
}

void SkyboxComponent::register_class() {
    // Register component properties for reflection/editor
    Registry::add<SkyboxComponent>("SkyboxComponent")
        .member("intensity", &SkyboxComponent::intensity_)
        .member("skybox_scale", &SkyboxComponent::skybox_scale_)
        .member("cube_texture_resolution", &SkyboxComponent::cube_texture_resolution_);
}
