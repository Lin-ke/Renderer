#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test_model.cpp
 * @brief Unit tests for Model render resource using bunny.obj.
 */

DEFINE_LOG_TAG(LogModelTest, "ModelTest");

TEST_CASE("Model Loading with bunny.obj", "[model]") {
    // Initialize engine with required systems
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset_);
    mode.set(EngineContext::StartMode::Window_);
    mode.set(EngineContext::StartMode::Render_);
    mode.set(EngineContext::StartMode::Single_Thread_);
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    INFO(LogModelTest, "Testing Model loading with bunny.obj...");
    
    // Verify RHI backend is available
    REQUIRE(EngineContext::rhi() != nullptr);
    
    // Test loading bunny.obj with default settings
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.flip_uv = false;
    setting.load_materials = false;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto model = std::make_shared<Model>(model_path, setting);
    
    // Verify model loaded successfully
    REQUIRE(model != nullptr);
    REQUIRE(model->get_submesh_count() > 0);
    
    // Check first submesh
    const SubmeshData& submesh = model->submesh(0);
    REQUIRE(submesh.mesh != nullptr);
    REQUIRE(submesh.mesh->position.size() > 0);
    REQUIRE(submesh.mesh->index.size() > 0);
    
    // Verify GPU buffers were created
    REQUIRE(submesh.vertex_buffer != nullptr);
    REQUIRE(submesh.index_buffer != nullptr);
    
    INFO(LogModelTest, "Bunny model loaded: {} vertices, {} indices", 
         submesh.mesh->position.size(), submesh.mesh->index.size());
    
    // Clean up
    model.reset();
    EngineContext::exit();
}

TEST_CASE("Model Multiple Submeshes", "[model]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset_);
    mode.set(EngineContext::StartMode::Window_);
    mode.set(EngineContext::StartMode::Render_);
    mode.set(EngineContext::StartMode::Single_Thread_);
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto model = std::make_shared<Model>(model_path, setting);
    
    // Bunny.obj should have at least one submesh
    REQUIRE(model->get_submesh_count() >= 1);
    
    // Test accessing all submeshes
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        const SubmeshData& submesh = model->submesh(i);
        REQUIRE(submesh.mesh != nullptr);
        REQUIRE(submesh.vertex_buffer != nullptr);
        REQUIRE(submesh.index_buffer != nullptr);
    }
    
    model.reset();
    EngineContext::exit();
}

TEST_CASE("Mesh Data Structure", "[model]") {
    // Test Mesh structure functionality
    Mesh mesh;
    
    // Add triangle data
    mesh.position = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
    mesh.normal = { Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1) };
    mesh.tex_coord = { Vec2(0, 0), Vec2(1, 0), Vec2(0, 1) };
    mesh.index = { 0, 1, 2 };
    
    // Verify triangle count
    CHECK(mesh.triangle_num() == 1);
    
    // Test mesh merge
    Mesh mesh2;
    mesh2.position = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1) };
    mesh2.normal = { Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0) };
    mesh2.tex_coord = { Vec2(0, 0), Vec2(1, 0), Vec2(0, 1) };
    mesh2.index = { 0, 1, 2 };
    
    mesh.merge(mesh2);
    
    // After merge: 6 vertices, 6 indices (2 triangles)
    CHECK(mesh.position.size() == 6);
    CHECK(mesh.index.size() == 6);
    CHECK(mesh.triangle_num() == 2);
}

TEST_CASE("Model Process Settings", "[model]") {
    // Test different process settings
    ModelProcessSetting setting1;
    setting1.smooth_normal = true;
    setting1.flip_uv = true;
    setting1.load_materials = false;
    
    CHECK(setting1.smooth_normal == true);
    CHECK(setting1.flip_uv == true);
    CHECK(setting1.load_materials == false);
    
    // Test with flip_uv enabled
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset_);
    mode.set(EngineContext::StartMode::Window_);
    mode.set(EngineContext::StartMode::Render_);
    mode.set(EngineContext::StartMode::Single_Thread_);
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
