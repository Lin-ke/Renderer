#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "samples/sun_temple/scene.h"

#include <cstdlib>
#include <unordered_set>

TEST_CASE("Sun Temple imports materials and renders from native assets", "[.][sun_temple]") {
    test_utils::TestContext::reset();
    const char* source_dir = std::getenv("SUN_TEMPLE_DIR");
    if (!source_dir) SKIP("Set SUN_TEMPLE_DIR to the ORCA SunTemple directory to run this integration test");
    const std::string scene_path = "/Game/sun_temple_import_test/scene.asset";
    auto scene = sun_temple::import_scene(source_dir, scene_path);
    REQUIRE(scene);
    auto renderers = scene->get_components<MeshRendererComponent>();
    REQUIRE(renderers.size() > 1);
    std::unordered_set<Material*> materials;
    size_t mesh_instances = 0;
    std::vector<std::string> failures;
    for (auto* renderer : renderers) {
        auto model = renderer->get_model();
        if (!model) { failures.push_back("Missing model"); continue; }
        mesh_instances += model->get_slot_count();
        for (uint32_t i = 0; i < model->get_slot_count(); ++i) {
            auto material = std::dynamic_pointer_cast<PBRMaterial>(model->get_material(i));
            if (!material) { failures.push_back("Missing PBR material"); continue; }
            if (!materials.insert(material.get()).second) continue;
            auto diffuse_texture = material->get_diffuse_texture();
            auto normal_texture = material->get_normal_texture();
            auto arm_texture = material->get_arm_texture();
            if (!diffuse_texture || !normal_texture || !arm_texture ||
                !diffuse_texture->texture_ || !normal_texture->texture_ || !arm_texture->texture_) {
                failures.push_back(material->get_name() + ": missing GPU texture");
                continue;
            }
            const auto diffuse = diffuse_texture->texture_->get_info().format;
            const auto normal = normal_texture->texture_->get_info().format;
            const auto arm = arm_texture->texture_->get_info().format;
            if ((diffuse != FORMAT_BC1_SRGB && diffuse != FORMAT_BC3_SRGB) || normal != FORMAT_BC5_UNORM ||
                (arm != FORMAT_BC1_UNORM && arm != FORMAT_BC3_UNORM))
                failures.push_back(material->get_name() + ": unexpected texture format");
        }
    }
    CAPTURE(failures);
    CHECK(failures.empty());
    CHECK(mesh_instances == 1056);
    CHECK(materials.size() == 48);

    auto* assets = EngineContext::asset();
    assets->save_asset(scene, scene_path);
    std::unordered_set<UID> visited;
    std::function<void(AssetRef)> unload = [&](AssetRef asset) {
        if (!asset || !visited.insert(asset->get_uid()).second) return;
        asset->traverse_deps(unload);
        assets->unload_asset(asset->get_uid());
    };
    unload(scene);
    scene.reset();

    test_utils::RenderTestApp::Config config;
    config.scene_path = scene_path;
    config.max_frames = 3;
    config.capture_frame = 3;
    config.screenshot_path = (utils::get_engine_path() / "build/test-results/sun_temple.png").string();
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
}
