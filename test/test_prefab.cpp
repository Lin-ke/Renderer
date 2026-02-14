#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/prefab_component.h"
#include "engine/core/log/Log.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/utils/file_cleaner.h"
#include <fstream>

// Mock component to test property override
class HealthComponent : public Component {
    CLASS_DEF(HealthComponent, Component)
public:
    int max_hp = 100;
    bool is_alive = true;
    static void register_class() {
        Registry::add<HealthComponent>("HealthComponent")
        .member("max_hp", &HealthComponent::max_hp)
        .member("alive", &HealthComponent::is_alive);
    }
};


REGISTER_CLASS_IMPL(HealthComponent)

CEREAL_REGISTER_TYPE(HealthComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, HealthComponent);

TEST_CASE("Prefab Modifications Test", "[prefab_mod]") {
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    
    UID prefab_uid;

    // Phase 1: Create Prefab with HealthComponent
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto prefab = std::make_shared<Prefab>();
        prefab->set_root_entity(std::make_unique<Entity>());
        auto* hp = prefab->get_root_entity()->add_component<HealthComponent>();
        hp->max_hp = 100;
        hp->is_alive = true;

        EngineContext::asset()->save_asset(prefab, "/Game/monster.asset");
        prefab_uid = prefab->get_uid();
        EngineContext::exit();
    }

    // Phase 2: Instantiate and Add Modification
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto prefab = EngineContext::asset()->load_asset<Prefab>(prefab_uid);
        auto scene = std::make_shared<Scene>();
        auto* instance = scene->instantiate(prefab);
        
        // Manual "Override" Logic (Simulated Editor Action)
        auto* prefab_comp = instance->get_component<PrefabComponent>();
        REQUIRE(prefab_comp != nullptr);

        // 1. Apply modification to Component
        auto* hp = instance->get_component<HealthComponent>();
        REQUIRE(hp != nullptr);
        hp->set_property("max_hp", "500"); // Direct setter
        hp->set_property("alive", "false"); 
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);

        // 2. Automatic modification generation happens during save
        // No manual push_back needed!

        // Save Scene
        EngineContext::asset()->save_asset(scene, "/Game/scene_mod.asset");
        EngineContext::exit();
    }

    // Phase 3: Verify JSON Structure and Reload
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        // Verify loaded data
        auto scene = EngineContext::asset()->load_asset<Scene>("/Game/scene_mod.asset");
        auto* instance = scene->entities_[0].get();
        auto* prefab_comp = instance->get_component<PrefabComponent>();
        
        REQUIRE(prefab_comp->modifications.size() == 2);
        CHECK(prefab_comp->modifications[0].target_component == "HealthComponent");
        CHECK(prefab_comp->modifications[1].field_path == "alive");

        // Check Entity data (already 500/false because serialization saved state)
        auto* hp = instance->get_component<HealthComponent>();
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);

        // --- Verify Application Logic ---
        // 1. Reset property to default (Simulate "Revert" or "Load from Prefab then apply")
        hp->max_hp = 100;
        hp->is_alive = true;
        
        // 2. Apply modifications
        prefab_comp->apply_modifications(instance);
        
        // 3. Check if property is updated
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);

        EngineContext::exit();
    }
}

TEST_CASE("Complex Prefab System Test", "[prefab]") {
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    UID ball_uid, cube_uid;

    // Phase 1: Create and Save Two Prefabs
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 1: Create Prefabs ---");
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        // 1. Create "Ball" Prefab at (1, 2, 3)
        {
            auto ball_prefab = std::make_shared<Prefab>();
            ball_prefab->set_root_entity(std::make_unique<Entity>());
            auto* t = ball_prefab->get_root_entity()->add_component<TransformComponent>();
            t->transform.set_position({1.0f, 2.0f, 3.0f});

            std::string path = "/Game/ball.asset";
            EngineContext::asset()->save_asset(ball_prefab, path);
            ball_uid = ball_prefab->get_uid();
            INFO(LogAsset, "Ball Prefab UID: {}", ball_uid.to_string());
        }

        // 2. Create "Cube" Prefab at (10, 0, 0)
        {
            auto cube_prefab = std::make_shared<Prefab>();
            cube_prefab->set_root_entity(std::make_unique<Entity>());
            auto* t = cube_prefab->get_root_entity()->add_component<TransformComponent>();
            t->transform.set_position({10.0f, 0.0f, 0.0f});

            std::string path = "/Game/cube.asset";
            EngineContext::asset()->save_asset(cube_prefab, path);
            cube_uid = cube_prefab->get_uid();
            INFO(LogAsset, "Cube Prefab UID: {}", cube_uid.to_string());
        }

        EngineContext::exit();
    }

    // Phase 2: Instantiate Multiple Copies and Distinct Prefabs
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 2: Instantiate Multiple ---");
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto ball_prefab = EngineContext::asset()->load_asset<Prefab>(ball_uid);
        auto cube_prefab = EngineContext::asset()->load_asset<Prefab>(cube_uid);
        REQUIRE(ball_prefab != nullptr);
        REQUIRE(cube_prefab != nullptr);
        REQUIRE(ball_prefab->get_root_entity() != nullptr);
        REQUIRE(cube_prefab->get_root_entity() != nullptr);

        auto scene = std::make_shared<Scene>();

        // 1. Instantiate Ball Copy 1
        auto* ball1 = scene->instantiate(ball_prefab);
        REQUIRE(ball1 != nullptr);
        auto* t1 = ball1->get_component<TransformComponent>();
        CHECK(t1->transform.get_position().x() == 1.0f);

        // 2. Instantiate Ball Copy 2 (verify independent copy)
        auto* ball2 = scene->instantiate(ball_prefab);
        REQUIRE(ball2 != nullptr);
        auto* t2 = ball2->get_component<TransformComponent>();
        CHECK(t2->transform.get_position().x() == 1.0f);

        // Modify Ball 1, Ball 2 should NOT change
        t1->transform.set_position({99.0f, 99.0f, 99.0f});
        CHECK(t2->transform.get_position().x() == 1.0f); // Still default

        // 3. Instantiate Cube (verify distinct prefab)
        auto* cube1 = scene->instantiate(cube_prefab);
        REQUIRE(cube1 != nullptr);
        auto* t3 = cube1->get_component<TransformComponent>();
        CHECK(t3->transform.get_position().x() == 10.0f);

        // Check Dependencies
        auto* pc1 = ball1->get_component<PrefabComponent>();
        auto* pc2 = ball2->get_component<PrefabComponent>();
        auto* pc3 = cube1->get_component<PrefabComponent>();

        REQUIRE(pc1->prefab->get_uid() == ball_uid);
        REQUIRE(pc2->prefab->get_uid() == ball_uid);
        REQUIRE(pc3->prefab->get_uid() == cube_uid);

        // Save Scene
        EngineContext::asset()->save_asset(scene, "/Game/complex_scene.asset");

        EngineContext::exit();
    }

    // Phase 3: Reload and Verify
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 3: Verify Reload ---");
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto scene = EngineContext::asset()->load_asset<Scene>("/Game/complex_scene.asset");
        REQUIRE(scene != nullptr);
        REQUIRE(scene->entities_.size() == 3);

        // Order is preserved in vector
        auto* ent1 = scene->entities_[0].get(); // Ball 1 (Modified)
        auto* ent2 = scene->entities_[1].get(); // Ball 2 (Original)
        auto* ent3 = scene->entities_[2].get(); // Cube 1 (Original)

        // Verify Data
        CHECK(ent1->get_component<TransformComponent>()->transform.get_position().x() == 99.0f);
        CHECK(ent2->get_component<TransformComponent>()->transform.get_position().x() == 1.0f);
        CHECK(ent3->get_component<TransformComponent>()->transform.get_position().x() == 10.0f);

        // Verify Dependencies
        auto* pc1 = ent1->get_component<PrefabComponent>();
        auto* pc2 = ent2->get_component<PrefabComponent>();
        auto* pc3 = ent3->get_component<PrefabComponent>();

        REQUIRE(pc1 != nullptr);
        REQUIRE(pc2 != nullptr);
        REQUIRE(pc3 != nullptr);

        // Check that assets are loaded and correct
        REQUIRE(pc1->prefab != nullptr);
        CHECK(pc1->prefab->get_uid() == ball_uid);
        REQUIRE(pc1->prefab->get_root_entity() != nullptr);
        
        REQUIRE(pc2->prefab != nullptr);
        CHECK(pc2->prefab->get_uid() == ball_uid); // Same prefab asset instance?
        
        REQUIRE(pc3->prefab != nullptr);
        CHECK(pc3->prefab->get_uid() == cube_uid);

        // Verify AssetManager caching (pc1->prefab and pc2->prefab should be the same pointer)
        CHECK(pc1->prefab == pc2->prefab);

        EngineContext::exit();
    }
}
