#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "test/test_utils.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/framework/component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/prefab_component.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/math/math.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/file_cleaner.h"
#include <string>
#include <any>
#include <fstream>

/**
 * @file test_reflection.cpp
 * @brief Unit tests for Reflection system and Asset serialization including Prefab.
 */

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

class AnyTestComponent : public Component {
    CLASS_DEF(AnyTestComponent, Component)
public:
    float speed_ = 10.5f;
    int health_ = 100;
    std::string name_ = "Player";
    static void register_class() {
        Registry::add<AnyTestComponent>("AnyTestComponent")
            .member("speed", &AnyTestComponent::speed_)
            .member("health", &AnyTestComponent::health_)
            .member("name", &AnyTestComponent::name_);
    }
};

CEREAL_REGISTER_TYPE(AnyTestComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, AnyTestComponent);

TEST_CASE("Reflection std::any Access", "[reflection]") {
    test_utils::TestContext::reset();
    AnyTestComponent::register_class();

    auto* class_info = ClassDB::get().get_class_info("AnyTestComponent");
    REQUIRE(class_info != nullptr);

    AnyTestComponent comp;
    
    // Test Speed (float)
    {
        auto it = class_info->property_map.find("speed");
        REQUIRE(it != class_info->property_map.end());
        const auto& prop = class_info->properties[it->second];

        std::any val = prop.getter_any(&comp);
        REQUIRE(val.has_value());
        REQUIRE(val.type() == typeid(float));
        CHECK(std::any_cast<float>(val) == 10.5f);

        prop.setter_any(&comp, std::any(20.0f));
        CHECK(comp.speed_ == 20.0f);

        val = prop.getter_any(&comp);
        CHECK(std::any_cast<float>(val) == 20.0f);

        prop.setter_any(&comp, std::any(500)); 
        CHECK(comp.speed_ == 20.0f);
    }

    // Test Health (int)
    {
        auto it = class_info->property_map.find("health");
        REQUIRE(it != class_info->property_map.end());
        const auto& prop = class_info->properties[it->second];

        std::any val = prop.getter_any(&comp);
        REQUIRE(val.type() == typeid(int));
        CHECK(std::any_cast<int>(val) == 100);

        prop.setter_any(&comp, std::any(50));
        CHECK(comp.health_ == 50);
    }

    // Test Name (std::string)
    {
        auto it = class_info->property_map.find("name");
        REQUIRE(it != class_info->property_map.end());
        const auto& prop = class_info->properties[it->second];

        std::any val = prop.getter_any(&comp);
        REQUIRE(val.type() == typeid(std::string));
        CHECK(std::any_cast<std::string>(val) == "Player");

        prop.setter_any(&comp, std::any(std::string("Enemy")));
        CHECK(comp.name_ == "Enemy");
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Simplified Serialization", "[reflection]") {
    test_utils::TestContext::reset();
    // 1. Test Primitive (int)
    {
        int val = 500;
        std::string serialized = ReflectScheme::serialize(val);
        CHECK(serialized == "500");

        int deserialized = 0;
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized == 500);
    }

    // 2. Test String
    {
        std::string val = "Player";
        std::string serialized = ReflectScheme::serialize(val);
        CHECK(serialized == "\"Player\"");

        std::string deserialized;
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized == "Player");
    }

    // 3. Test Complex Type (Vec2)
    {
        Vec2 val(1.5f, 2.5f);
        std::string serialized = ReflectScheme::serialize(val);
        CHECK(serialized == "\"1.500000 2.500000\"");

        Vec2 deserialized(0, 0);
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized.x == 1.5f);
        CHECK(deserialized.y == 2.5f);
    }

    // 4. Test Legacy Format Compatibility
    {
        std::string legacy = "{\"value0\": 100}";
        int val = 0;
        ReflectScheme::deserialize(legacy, val);
        CHECK(val == 100);
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Prefab Modifications", "[prefab]") {
    test_utils::TestContext::reset();
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    
    UID prefab_uid = UID::generate();

    // Phase 1: Create Prefab with HealthComponent
    {
        auto prefab = std::make_shared<Prefab>();
        prefab->set_root_entity(std::make_unique<Entity>());
        auto* hp = prefab->get_root_entity()->add_component<HealthComponent>();
        hp->max_hp = 100;
        hp->is_alive = true;

        EngineContext::asset()->save_asset(prefab, "/Game/monster.asset");
        prefab_uid = prefab->get_uid();
    }

    // Phase 2: Instantiate and Add Modification
    {
        auto prefab = EngineContext::asset()->load_asset<Prefab>(prefab_uid);
        auto scene = std::make_shared<Scene>();
        auto* instance = scene->instantiate(prefab);
        
        auto* prefab_comp = instance->get_component<PrefabComponent>();
        REQUIRE(prefab_comp != nullptr);

        auto* hp = instance->get_component<HealthComponent>();
        REQUIRE(hp != nullptr);
        hp->set_property("max_hp", "500");
        hp->set_property("alive", "false"); 
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);

        EngineContext::asset()->save_asset(scene, "/Game/scene_mod.asset");
    }

    // Phase 3: Verify JSON Structure and Reload
    {
        auto scene = EngineContext::asset()->load_asset<Scene>("/Game/scene_mod.asset");
        auto* instance = scene->entities_[0].get();
        auto* prefab_comp = instance->get_component<PrefabComponent>();
        
        REQUIRE(prefab_comp->modifications.size() == 2);
        CHECK(prefab_comp->modifications[0].target_component == "HealthComponent");
        CHECK(prefab_comp->modifications[1].field_path == "alive");

        auto* hp = instance->get_component<HealthComponent>();
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);

        // Verify Application Logic
        hp->max_hp = 100;
        hp->is_alive = true;
        
        prefab_comp->apply_modifications(instance);
        
        CHECK(hp->max_hp == 500);
        CHECK(hp->is_alive == false);
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Complex Prefab System", "[prefab]") {
    test_utils::TestContext::reset();
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    UID ball_uid, cube_uid;

    // Phase 1: Create and Save Two Prefabs
    {
        // 1. Create "Ball" Prefab at (1, 2, 3)
        {
            auto ball_prefab = std::make_shared<Prefab>();
            ball_prefab->set_root_entity(std::make_unique<Entity>());
            auto* t = ball_prefab->get_root_entity()->add_component<TransformComponent>();
            t->transform.set_position({1.0f, 2.0f, 3.0f});

            std::string path = "/Game/ball.asset";
            EngineContext::asset()->save_asset(ball_prefab, path);
            ball_uid = ball_prefab->get_uid();

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

        }
    }

    // Phase 2: Instantiate Multiple Copies and Distinct Prefabs
    {
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
        CHECK(t1->transform.get_position().x == 1.0f);

        // 2. Instantiate Ball Copy 2 (verify independent copy)
        auto* ball2 = scene->instantiate(ball_prefab);
        REQUIRE(ball2 != nullptr);
        auto* t2 = ball2->get_component<TransformComponent>();
        CHECK(t2->transform.get_position().x == 1.0f);

        // Modify Ball 1, Ball 2 should NOT change
        t1->transform.set_position({99.0f, 99.0f, 99.0f});
        CHECK(t2->transform.get_position().x == 1.0f);

        // 3. Instantiate Cube (verify distinct prefab)
        auto* cube1 = scene->instantiate(cube_prefab);
        REQUIRE(cube1 != nullptr);
        auto* t3 = cube1->get_component<TransformComponent>();
        CHECK(t3->transform.get_position().x == 10.0f);

        // Check Dependencies
        auto* pc1 = ball1->get_component<PrefabComponent>();
        auto* pc2 = ball2->get_component<PrefabComponent>();
        auto* pc3 = cube1->get_component<PrefabComponent>();

        REQUIRE(pc1->prefab->get_uid() == ball_uid);
        REQUIRE(pc2->prefab->get_uid() == ball_uid);
        REQUIRE(pc3->prefab->get_uid() == cube_uid);

        // Save Scene
        EngineContext::asset()->save_asset(scene, "/Game/complex_scene.asset");
    }

    // Phase 3: Reload and Verify
    {
        auto scene = EngineContext::asset()->load_asset<Scene>("/Game/complex_scene.asset");
        REQUIRE(scene != nullptr);
        REQUIRE(scene->entities_.size() == 3);

        // Order is preserved in vector
        auto* ent1 = scene->entities_[0].get(); // Ball 1 (Modified)
        auto* ent2 = scene->entities_[1].get(); // Ball 2 (Original)
        auto* ent3 = scene->entities_[2].get(); // Cube 1 (Original)

        // Verify Data
        CHECK(ent1->get_component<TransformComponent>()->transform.get_position().x == 99.0f);
        CHECK(ent2->get_component<TransformComponent>()->transform.get_position().x == 1.0f);
        CHECK(ent3->get_component<TransformComponent>()->transform.get_position().x == 10.0f);

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
        CHECK(pc2->prefab->get_uid() == ball_uid);
        
        REQUIRE(pc3->prefab != nullptr);
        CHECK(pc3->prefab->get_uid() == cube_uid);

        // Verify AssetManager caching
        CHECK(pc1->prefab == pc2->prefab);
    }
    test_utils::TestContext::reset();
}
