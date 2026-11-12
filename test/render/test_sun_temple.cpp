#include <catch2/catch_test_macros.hpp>
#include "test/test_utils.h"
#include "samples/sun_temple/scene.h"

#include <cstdlib>
#include <unordered_set>

TEST_CASE("Sun Temple imports materials and renders from native assets", "[.][sun_temple]") {
    test_utils::TestContext::reset();
    const char* source_dir = std::getenv("SUN_TEMPLE_DIR");
    if (!source_dir) SKIP("Set SUN_TEMPLE_DIR to the ORCA SunTemple directory to run this integration test");
    Log::set_min_log_level(1);
    auto model = sun_temple::import_model(source_dir);
    REQUIRE(model);
    REQUIRE(model->get_slot_count() == 1056);
    std::unordered_set<Material*> materials;
    for (uint32_t i = 0; i < model->get_slot_count(); ++i) {
        auto material = std::dynamic_pointer_cast<PBRMaterial>(model->get_material(i));
        REQUIRE(material);
        if (!materials.insert(material.get()).second) continue;
        CAPTURE(material->get_name());
        REQUIRE(material->get_diffuse_texture());
        REQUIRE(material->get_diffuse_texture()->texture_);
        REQUIRE(material->get_normal_texture());
        REQUIRE(material->get_normal_texture()->texture_);
        REQUIRE(material->get_arm_texture());
        REQUIRE(material->get_arm_texture()->texture_);
        CHECK(material->get_diffuse_texture()->texture_->get_info().format == FORMAT_R8G8B8A8_SRGB);
        CHECK(material->get_normal_texture()->texture_->get_info().format == FORMAT_R8G8B8A8_UNORM);
        CHECK(material->get_arm_texture()->texture_->get_info().format == FORMAT_R8G8B8A8_UNORM);
    }
    CHECK(materials.size() == 48);
    auto bounds = model->get_bounding_box();
    CHECK(bounds.max.x - bounds.min.x > 250.0f);
    CHECK(bounds.max.z - bounds.min.z > 240.0f);

    // Force deserialization rather than returning the just-saved instances.
    auto* assets = EngineContext::asset();
    assets->save_asset(model, sun_temple::model_path);
    std::unordered_set<UID> visited;
    std::function<void(AssetRef)> unload = [&](AssetRef asset) {
        if (!asset || !visited.insert(asset->get_uid()).second) return;
        asset->traverse_deps(unload);
        assets->unload_asset(asset->get_uid());
    };
    unload(model);
    model.reset();
    model = assets->load_asset<Model>(sun_temple::model_path);
    REQUIRE(model);
    REQUIRE(model->get_slot_count() == 1056);
    REQUIRE(model->get_mesh(0)->get_vertex_buffer());
    auto material = std::dynamic_pointer_cast<PBRMaterial>(model->get_material(0));
    REQUIRE(material);
    REQUIRE(material->get_arm_texture());
    REQUIRE(material->get_arm_texture()->texture_);

    test_utils::RenderTestApp::Config config;
    config.scene_path = sun_temple::scene_path;
    config.max_frames = 3;
    config.capture_frame = 3;
    config.screenshot_path = (utils::get_engine_path() / "build/test-results/sun_temple.png").string();
    config.create_scene_func = [model, source_dir](const std::string& path) {
        auto scene = sun_temple::create_scene(model, source_dir);
        EngineContext::asset()->save_asset(scene, path);
        return true;
    };
    std::vector<uint8_t> pixels;
    REQUIRE(test_utils::RenderTestApp::run(config, pixels));
    REQUIRE_FALSE(pixels.empty());
    auto brightness = test_utils::calculate_average_brightness(pixels);
    CHECK(brightness > 10.0f);
    CHECK(brightness < 240.0f);
    size_t colored = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const int high = std::max({pixels[i], pixels[i + 1], pixels[i + 2]});
        const int low = std::min({pixels[i], pixels[i + 1], pixels[i + 2]});
        if (high - low > 20) ++colored;
    }
    CHECK(colored > pixels.size() / 4 / 10);
    test_utils::TestContext::reset();
    Log::set_min_log_level(0);
}
