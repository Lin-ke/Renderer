#pragma once

#include "engine/main/engine_context.h"
#include "engine/core/utils/path_utils.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/function/render/render_resource/skybox_material.h"

#include <filesystem>

namespace sun_temple {

inline constexpr const char* model_path = "/Game/sun_temple/model.asset";
inline constexpr const char* scene_path = "/Game/sun_temple/scene.asset";

inline std::shared_ptr<Model> import_model(const std::filesystem::path& source_dir) {
    ModelProcessSetting settings;
    settings.load_materials = true;
    settings.smooth_normal = false; // Preserve the authored normals and hard edges.
    settings.tangent_space = true;
    settings.flip_uv = true;
    ModelImportOptions options;
    options.convert_to_left_handed = true;
    options.specular_map_is_arm = true; // ORCA README: R=AO, G=roughness, B=metallic.
    ModelImporter importer;
    auto model = importer.import_model((source_dir / "SunTemple.fbx").string(), model_path, settings, options);
    if (!model) return nullptr;
    for (uint32_t i = 0; i < model->get_slot_count(); ++i) {
        auto material = std::dynamic_pointer_cast<PBRMaterial>(model->get_material(i));
        if (!material) continue;
        if (material->get_name().find("DoubleSided") != std::string::npos) {
            material->set_cull_mode(CULL_MODE_NONE);
            material->set_alpha_clip(0.5f);
            material->set_use_for_depth_pass(false);
        }
    }
    return model;
}

inline std::shared_ptr<Scene> create_scene(ModelRef model, const std::filesystem::path& source_dir = {}) {
    if (!model) return nullptr;
    auto scene = std::make_shared<Scene>();
    auto* temple = scene->create_entity("Sun Temple");
    temple->add_component<TransformComponent>();
    temple->add_component<MeshRendererComponent>()->set_model(model);

    auto* camera_entity = scene->create_entity("Camera");
    auto* camera_transform = camera_entity->add_component<TransformComponent>();
    // The camera exported in SunTemple.fbx, converted to the engine's LH axes.
    camera_transform->transform.set_position({0.128083f, 3.169803f, -70.873520f});
    camera_transform->transform.set_rotation({3.25f, -3.8f, 0.0f});
    auto* camera = camera_entity->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    camera->set_near(0.1f);
    camera->set_far(500.0f);

    auto* sun_entity = scene->create_entity("Sun");
    sun_entity->add_component<TransformComponent>()->transform.set_rotation({55.0f, -35.0f, 0.0f});
    auto* sun = sun_entity->add_component<DirectionalLightComponent>();
    sun->set_color({1.0f, 0.95f, 0.85f});
    sun->set_intensity(4.0f);
    sun->set_enable(true);
    if (!source_dir.empty()) {
        auto panorama = std::make_shared<Texture>((source_dir / "SunTemple_Skybox.hdr").string());
        if (panorama->texture_) {
            auto material = std::make_shared<SkyboxMaterial>();
            material->set_panorama_texture(panorama);
            auto* sky = scene->create_entity("Sky");
            sky->add_component<TransformComponent>();
            auto* component = sky->add_component<SkyboxComponent>();
            component->set_material(material);
            component->set_skybox_scale(400.0f);
        }
    }
    return scene;
}

} // namespace sun_temple
