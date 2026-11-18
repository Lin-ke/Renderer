#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/asset/asset_manager.h"

#include <filesystem>

/**
 * @file test_scene_core.cpp
 * @brief Core scene tests including serialization and dependency management.
 */

TEST_CASE("Scene dependencies round-trip through native assets", "[scene]") {
    test_utils::TestContext::reset();
    auto* assets = EngineContext::asset();
    REQUIRE(assets);
    const auto directory = assets->get_physical_path("/Game/scene_dependency_roundtrip");
    REQUIRE(directory);
    std::filesystem::create_directories(*directory);

    auto scene = std::make_shared<Scene>();
    scene->set_uid(UID::generate());
    const auto scene_path = "/Game/scene_dependency_roundtrip/" + scene->get_uid().to_string() + ".asset";
    auto* camera_entity = scene->create_entity("Camera");
    camera_entity->add_component<TransformComponent>()->transform.set_position({0, 1, 3});
    camera_entity->add_component<CameraComponent>()->set_fov(60.0f);
    auto* light_entity = scene->create_entity("Sun");
    light_entity->add_component<TransformComponent>();
    light_entity->add_component<DirectionalLightComponent>()->set_intensity(1.5f);

    auto mesh = Mesh::Create({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {}, {}, {}, {2, 1, 0});
    auto material = std::make_shared<PBRMaterial>();
    auto model = Model::Create(mesh, material);
    auto override_material = std::make_shared<PBRMaterial>();
    override_material->set_roughness(0.25f);
    auto* model_entity = scene->create_entity("Triangle");
    model_entity->add_component<TransformComponent>()->transform.set_scale({0.5f, 0.5f, 0.5f});
    auto* renderer = model_entity->add_component<MeshRendererComponent>();
    renderer->set_model(model);
    renderer->set_material(override_material, 0);
    assets->save_asset(scene, scene_path);

    for (const AssetRef& asset : {AssetRef(scene), AssetRef(model), AssetRef(mesh), AssetRef(material),
            AssetRef(override_material)}) {
        REQUIRE_FALSE(asset->get_uid().is_empty());
        assets->unload_asset(asset->get_uid());
    }

    const auto loaded = assets->load_asset<Scene>(scene_path);
    REQUIRE(loaded);
    CHECK(loaded != scene);
    REQUIRE(loaded->entities_.size() == 3);
    REQUIRE(loaded->get_camera());
    CHECK(loaded->get_camera()->get_fov() == 60.0f);
    REQUIRE(loaded->get_directional_light());
    CHECK(loaded->get_directional_light()->get_intensity() == 1.5f);

    const auto renderers = loaded->get_components<MeshRendererComponent>();
    REQUIRE(renderers.size() == 1);
    const auto loaded_model = renderers.front()->get_model();
    REQUIRE(loaded_model);
    CHECK(loaded_model != model);
    CHECK(loaded_model->get_uid() == model->get_uid());
    REQUIRE(loaded_model->get_slot_count() == 1);
    REQUIRE(loaded_model->get_mesh(0));
    CHECK(loaded_model->get_mesh(0)->get_uid() == mesh->get_uid());
    CHECK(loaded_model->get_mesh(0)->get_indices() == mesh->get_indices());
    REQUIRE(loaded_model->get_material(0));
    CHECK(loaded_model->get_material(0)->get_uid() == material->get_uid());
    const auto loaded_override = std::dynamic_pointer_cast<PBRMaterial>(renderers.front()->get_material());
    REQUIRE(loaded_override);
    CHECK(loaded_override->get_uid() == override_material->get_uid());
    CHECK(loaded_override->get_roughness() == 0.25f);
    auto* transform = renderers.front()->get_owner()->get_component<TransformComponent>();
    REQUIRE(transform);
    CHECK(transform->transform.get_scale().x == 0.5f);

    test_utils::TestContext::reset();
}

TEST_CASE("Scene Serialization", "[scene]") {
    test_utils::TestContext::reset();
    auto* assets = EngineContext::asset();
    REQUIRE(assets);
    const auto directory = assets->get_physical_path("/Game/scene_transform_roundtrip");
    REQUIRE(directory);
    std::filesystem::create_directories(*directory);
    const auto uid = UID::generate();
    const auto scene_path = "/Game/scene_transform_roundtrip/" + uid.to_string() + ".asset";
    {
        auto scene = std::make_shared<Scene>();
        scene->set_uid(uid);
        auto entity = scene->create_entity();
        
        auto transform_comp = entity->add_component<TransformComponent>();
        transform_comp->transform.set_position({10.0f, 20.0f, 30.0f});
        transform_comp->transform.set_scale({2.0f, 2.0f, 2.0f});

        assets->save_asset(scene, scene_path);
        assets->unload_asset(uid);
    }

    {
        auto loaded_scene = assets->load_asset<Scene>(scene_path);

        REQUIRE(loaded_scene != nullptr);
        REQUIRE(loaded_scene->entities_.size() == 1);

        auto* entity = loaded_scene->entities_[0].get();
        auto transform_comp = entity->get_component<TransformComponent>();
        
        REQUIRE(transform_comp != nullptr);

        auto pos = transform_comp->transform.get_position();
        CHECK(pos.x == (10.0f));
        CHECK(pos.y == (20.0f));
        CHECK(pos.z == (30.0f));

        auto scale = transform_comp->transform.get_scale();
        CHECK(scale.x == (2.0f));
        CHECK(scale.y == (2.0f));
        CHECK(scale.z == (2.0f));
    }
    test_utils::TestContext::reset();
}
