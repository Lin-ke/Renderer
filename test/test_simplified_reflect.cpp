#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/core/reflect/class_db.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/math/math.h"
#include <string>
#include <iostream>

TEST_CASE("Simplified Serialization", "[reflection]") {
    // 1. Test Primitive (int)
    {
        int val = 500;
        std::string serialized = ReflectScheme::serialize(val);
        std::cout << "DEBUG: int(500) serialized to: [" << serialized << "]" << std::endl;
        CHECK(serialized == "500");

        int deserialized = 0;
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized == 500);
    }

    // 2. Test String
    {
        std::string val = "Player";
        std::string serialized = ReflectScheme::serialize(val);
        std::cout << "DEBUG: string(Player) serialized to: [" << serialized << "]" << std::endl;
        CHECK(serialized == "\"Player\"");

        std::string deserialized;
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized == "Player");
    }

    // 3. Test Complex Type (Vec2)
    {
        Vec2 val(1.5f, 2.5f);
        std::string serialized = ReflectScheme::serialize(val);
        std::cout << "DEBUG: Vec2(1.5, 2.5) serialized to: [" << serialized << "]" << std::endl;
        CHECK(serialized == "\"1.500000 2.500000\"");

        Vec2 deserialized(0, 0);
        ReflectScheme::deserialize(serialized, deserialized);
        CHECK(deserialized.x() == 1.5f);
        CHECK(deserialized.y() == 2.5f);
    }

    // 4. Test Legacy Format Compatibility
    {
        std::string legacy = "{\"value0\": 100}";
        int val = 0;
        ReflectScheme::deserialize(legacy, val);
        CHECK(val == 100);
    }
}
