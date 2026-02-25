#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/math_print.h"
#include "engine/core/utils/file_cleaner.h"

#include <filesystem>
#include <fstream>
#include <vector>

/**
 * @file test_scene_core.cpp
 * @brief Core scene tests including serialization and dependency management.
 */

DEFINE_LOG_TAG(LogSceneCore, "SceneCore");

static const std::string TEST_SCENE_FILE = "/Game/test_deps_scene.asset";
static const std::string KLEE_MODEL_PATH = "/Engine/models/Klee/klee.fbx";
static const std::string TEST_MODEL_PATH = "/Engine/models/bunny.obj";

TEST_CASE("Scene Dependency System", "[scene]") {
    test_utils::TestContext::reset();
    
    try {
        std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
        std::filesystem::create_directories(test_asset_dir);
        REQUIRE(EngineContext::rhi() != nullptr);
        REQUIRE(EngineContext::asset() != nullptr);
        
        // Collect available models to test
        std::vector<std::string> models_to_test;
        if (std::filesystem::exists(TEST_MODEL_PATH)) {
            models_to_test.push_back(TEST_MODEL_PATH);
        } else {
            WARN(LogSceneCore, "Bunny model not found at: {}", TEST_MODEL_PATH);
        }
        
        if (std::filesystem::exists(KLEE_MODEL_PATH)) {
            models_to_test.push_back(KLEE_MODEL_PATH);
        } else {
            WARN(LogSceneCore, "Klee model not found at: {}", KLEE_MODEL_PATH);
        }

        if (models_to_test.empty()) {
            WARN(LogSceneCore, "No test models found. Skipping test.");
            test_utils::TestContext::reset();
            REQUIRE(true);
            return;
        }

        // Use first available model for save/load test
        const auto& model_path = models_to_test[0];
        bool is_klee = (model_path == KLEE_MODEL_PATH);
        
        // ==========================================
        // Phase 1: Create and Save Scene
        // ==========================================
        {
            auto scene = std::make_shared<Scene>();
            
            // Create camera entity
            auto* camera_ent = scene->create_entity();
            auto* cam_trans = camera_ent->add_component<TransformComponent>();
            cam_trans->transform.set_position({0.0f, 1.0f, 3.0f});
            auto* cam_comp = camera_ent->add_component<CameraComponent>();
            cam_comp->set_fov(60.0f);
            
            // Create directional light
            auto* light_ent = scene->create_entity();
            auto* light_trans = light_ent->add_component<TransformComponent>();
            light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
            auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
            light_comp->set_color({1.0f, 1.0f, 1.0f});
            light_comp->set_intensity(1.5f);
            
            // Create model entity with mesh renderer
            auto* model_ent = scene->create_entity();
            auto* model_trans = model_ent->add_component<TransformComponent>();
            model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
            model_trans->transform.set_scale({0.5f, 0.5f, 0.5f});
            
            // Load model
            ModelProcessSetting setting;
            setting.smooth_normal = true;
            setting.load_materials = !is_klee;
            setting.flip_uv = false;
            
            std::shared_ptr<Model> test_model = Model::Load(model_path, setting);
            
            REQUIRE(test_model != nullptr);
            uint32_t submesh_count = test_model->get_submesh_count();
            REQUIRE(submesh_count > 0);
            
            INFO(LogSceneCore, "Loaded {}: {} submeshes", is_klee ? "Klee" : "Bunny", submesh_count);
            
            // Add mesh renderer component
            auto* mesh_renderer = model_ent->add_component<MeshRendererComponent>();
            mesh_renderer->set_model(test_model);
            
            // For Klee model, manually create materials
            if (is_klee) {
                for (uint32_t i = 0; i < test_model->get_submesh_count(); i++) {
                    auto material = std::make_shared<PBRMaterial>();
                    material->set_diffuse({1.0f, 1.0f, 1.0f, 1.0f});
                    material->set_roughness(0.5f);
                    material->set_metallic(0.0f);
                    mesh_renderer->set_material(material, i);
                }
            } else {
                // Bunny - use materials from model if available
                for (uint32_t i = 0; i < test_model->get_submesh_count(); i++) {
                    auto mat = test_model->get_material(i);
                    if (mat) {
                        mesh_renderer->set_material(mat, i);
                    }
                }
            }
            
            // Save asset dependencies (syncs UIDs)
            mesh_renderer->save_asset_deps();
            REQUIRE(!mesh_renderer->get_model()->get_uid().is_empty());
            
            // Verify dependency traversal finds Model
            std::vector<std::shared_ptr<Asset>> deps;
            scene->traverse_deps([&deps](std::shared_ptr<Asset> asset) {
                deps.push_back(asset);
            });
            
            bool found_model = false;
            for (auto& dep : deps) {
                if (dep->get_asset_type() == AssetType::Model) found_model = true;
            }
            REQUIRE(found_model);
            
            // Save scene to file
            EngineContext::asset()->save_asset(scene, TEST_SCENE_FILE);
            INFO(LogSceneCore, "Scene saved to {}", TEST_SCENE_FILE);
        }
        
        // ==========================================
        // Phase 2: Load and Verify Scene
        // ==========================================
        {
            auto loaded_scene = EngineContext::asset()->load_asset<Scene>(TEST_SCENE_FILE);
            
            REQUIRE(loaded_scene != nullptr);
            INFO(LogSceneCore, "Scene loaded from {}", TEST_SCENE_FILE);
            
            // Load asset dependencies (models, materials, etc.)
            loaded_scene->load_asset_deps();
            
            // Verify entity count (camera, light, model = 3 entities)
            REQUIRE(loaded_scene->entities_.size() == 3);
            
            // Find and verify camera entity
            CameraComponent* loaded_cam = nullptr;
            TransformComponent* loaded_cam_trans = nullptr;
            for (auto& entity : loaded_scene->entities_) {
                if (auto* cam = entity->get_component<CameraComponent>()) {
                    loaded_cam = cam;
                    loaded_cam_trans = entity->get_component<TransformComponent>();
                    break;
                }
            }
            REQUIRE(loaded_cam != nullptr);
            REQUIRE(loaded_cam_trans != nullptr);
            CHECK(loaded_cam->get_fov() == 60.0f);
            
            // Find and verify light entity
            DirectionalLightComponent* loaded_light = nullptr;
            for (auto& entity : loaded_scene->entities_) {
                if (auto* light = entity->get_component<DirectionalLightComponent>()) {
                    loaded_light = light;
                    break;
                }
            }
            REQUIRE(loaded_light != nullptr);
            CHECK(loaded_light->get_color().x == 1.0f);
            CHECK(loaded_light->get_intensity() == 1.5f);
            
            // Find and verify model entity
            MeshRendererComponent* loaded_mesh_renderer = nullptr;
            TransformComponent* loaded_model_trans = nullptr;
            for (auto& entity : loaded_scene->entities_) {
                if (auto* mesh = entity->get_component<MeshRendererComponent>()) {
                    loaded_mesh_renderer = mesh;
                    loaded_model_trans = entity->get_component<TransformComponent>();
                    break;
                }
            }
            REQUIRE(loaded_mesh_renderer != nullptr);
            REQUIRE(loaded_model_trans != nullptr);
            
            // Verify transform data
            auto scale = loaded_model_trans->transform.get_scale();
            CHECK(scale.x == 0.5f);
            CHECK(scale.y == 0.5f);
            CHECK(scale.z == 0.5f);
            
            // Verify model reference is restored
            auto loaded_model = loaded_mesh_renderer->get_model();
            REQUIRE(loaded_model != nullptr);
            CHECK(loaded_model->get_submesh_count() > 0);
            
            INFO(LogSceneCore, "Scene verification completed: {} entities, model with {} submeshes",
                 loaded_scene->entities_.size(), loaded_model->get_submesh_count());
        }
        
        // Clean up auto-generated UUID-named dependency files
        size_t cleaned = test_utils::cleanup_uuid_named_assets(test_asset_dir + "/assets");
        if (cleaned > 0) {
            INFO(LogSceneCore, "Cleaned up {} auto-generated UUID-named asset files", cleaned);
        }
        
        test_utils::TestContext::reset();
    } catch (const std::exception& e) {
        ERR(LogSceneCore, "Test failed: {}", e.what());
        test_utils::TestContext::reset();
        FAIL("Test failed with exception");
    }
}

TEST_CASE("Scene Serialization", "[scene]") {
    test_utils::TestContext::reset();
    {
        auto scene = std::make_shared<Scene>();
        auto entity = scene->create_entity();
        
        auto transform_comp = entity->add_component<TransformComponent>();
        transform_comp->transform.set_position({10.0f, 20.0f, 30.0f});
        transform_comp->transform.set_scale({2.0f, 2.0f, 2.0f});

        std::string scene_path = "/Game/test_scene.asset";
        EngineContext::asset()->save_asset(scene, scene_path);
    }

    {
        INFO(LogAsset, "--- Phase 2: Loading Scene ---");
        std::string scene_path = "/Game/test_scene.asset";
        auto loaded_scene = EngineContext::asset()->load_asset<Scene>(scene_path);

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
