#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/function/framework/component.h"
#include "engine/core/reflect/class_db.h"
#include <string>
#include <any>

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
    // Ensure class is registered
    AnyTestComponent::register_class();

    auto* class_info = ClassDB::get().get_class_info("AnyTestComponent");
    REQUIRE(class_info != nullptr);

    AnyTestComponent comp;
    
    // Test Speed (float)
    {
        auto it = class_info->property_map.find("speed");
        REQUIRE(it != class_info->property_map.end());
        const auto& prop = class_info->properties[it->second];

        // 1. Get Value via std::any
        std::any val = prop.getter_any(&comp);
        REQUIRE(val.has_value());
        REQUIRE(val.type() == typeid(float));
        CHECK(std::any_cast<float>(val) == 10.5f);

        // 2. Set Value via std::any
        prop.setter_any(&comp, std::any(20.0f));
        CHECK(comp.speed_ == 20.0f);

        // 3. Verify Get reflects change
        val = prop.getter_any(&comp);
        CHECK(std::any_cast<float>(val) == 20.0f);

        // 4. Test Invalid Type Set (int instead of float) - should fail silently or not change
        prop.setter_any(&comp, std::any(500)); 
        CHECK(comp.speed_ == 20.0f); // Should not change
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
}
