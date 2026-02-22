#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render_resource/test_model.cpp
 * @brief Unit tests for Model and Mesh render resources.
 */

DEFINE_LOG_TAG(LogModelTest, "ModelTest");

TEST_CASE("Model Loading with bunny.obj", "[render_resource]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.flip_uv = false;
    setting.load_materials = false;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto model = std::make_shared<Model>(model_path, setting);
    
    REQUIRE(model != nullptr);
    REQUIRE(model->get_submesh_count() > 0);
    
    auto mesh = model->get_mesh(0);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->get_positions().size() > 0);
    REQUIRE(mesh->get_indices().size() > 0);
    
    REQUIRE(mesh->get_vertex_buffer() != nullptr);
    REQUIRE(mesh->get_index_buffer() != nullptr);
    
    model.reset();
    EngineContext::exit();
}

TEST_CASE("Model Multiple Submeshes", "[render_resource]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto model = std::make_shared<Model>(model_path, setting);
    
    REQUIRE(model->get_submesh_count() >= 1);
    
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        auto mesh = model->get_mesh(i);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->get_vertex_buffer() != nullptr);
        REQUIRE(mesh->get_index_buffer() != nullptr);
    }
    
    model.reset();
    EngineContext::exit();
}

TEST_CASE("Mesh Data Structure", "[render_resource]") {
    std::vector<Vec3> positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
    std::vector<Vec3> normals = { Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1) };
    std::vector<Vec2> tex_coords = { Vec2(0, 0), Vec2(1, 0), Vec2(0, 1) };
    std::vector<uint32_t> indices = { 0, 1, 2 };
    
    auto mesh = Mesh::Create(positions, normals, {}, tex_coords, indices, "mesh1");
    
    CHECK(mesh->get_index_count() / 3 == 1); // 1 triangle
    
    std::vector<Vec3> positions2 = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1) };
    std::vector<Vec3> normals2 = { Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0) };
    std::vector<uint32_t> indices2 = { 0, 1, 2 };
    
    auto mesh2 = Mesh::Create(positions2, normals2, {}, {}, indices2, "mesh2");
    
    mesh->merge(*mesh2);
    
    CHECK(mesh->get_vertex_count() == 6);
    CHECK(mesh->get_index_count() == 6);
    CHECK(mesh->get_index_count() / 3 == 2); // 2 triangles
}

TEST_CASE("Model Process Settings", "[render_resource]") {
    ModelProcessSetting setting1;
    setting1.smooth_normal = true;
    setting1.flip_uv = true;
    setting1.load_materials = false;
    
    CHECK(setting1.smooth_normal == true);
    CHECK(setting1.flip_uv == true);
    CHECK(setting1.load_materials == false);
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    ModelProcessSetting setting2;
    setting2.smooth_normal = false;
    setting2.flip_uv = true;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto model = std::make_shared<Model>(model_path, setting2);
    
    REQUIRE(model != nullptr);
    REQUIRE(model->get_submesh_count() > 0);
    
    model.reset();
    EngineContext::exit();
}
