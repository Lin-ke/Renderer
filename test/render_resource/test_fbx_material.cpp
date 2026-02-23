#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render_resource/test_fbx_material.cpp
 * @brief Tests for FBX model loading with material support
 */

DEFINE_LOG_TAG(LogFbxMaterial, "FbxMaterial");

TEST_CASE("FBX Model Loading with Materials", "[render_resource][fbx]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    // Load FBX model with materials using Model::Load
    std::string model_path = "/Engine/models/material_ball/material_ball.fbx";
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = true;  // Enable material loading
    setting.flip_uv = false;
    
    auto model = Model::Load(model_path, setting);
    
    // Verify model loaded successfully
    REQUIRE(model != nullptr);
    REQUIRE(model->get_submesh_count() > 0);
    INFO(LogFbxMaterial, "Loaded {}: {} submeshes", model_path, model->get_submesh_count());
    
    // Verify each submesh has valid data
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        auto mesh = model->get_mesh(i);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->get_vertex_buffer() != nullptr);
        REQUIRE(mesh->get_index_buffer() != nullptr);
    }
    
    EngineContext::exit();
}

TEST_CASE("FBX Model Without Materials", "[render_resource][fbx]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    std::string model_path = "/Engine/models/material_ball/material_ball.fbx";
    
    // Load without materials using Model::Load
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;  // Disable material loading
    
    auto model = Model::Load(model_path, setting);
    REQUIRE(model != nullptr);
    REQUIRE(model->get_submesh_count() > 0);
    
    // Verify no materials loaded
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        REQUIRE(model->get_material(i) == nullptr);
    }
    
    EngineContext::exit();
}

TEST_CASE("FBX Model Processing Options", "[render_resource][fbx]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    std::string model_path = "/Engine/models/material_ball/material_ball.fbx";
    
    SECTION("Smooth normals") {
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.load_materials = false;
        
        auto model = Model::Load(model_path, setting);
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() > 0);
        
        // Verify normals exist
        for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
            auto mesh = model->get_mesh(i);
            REQUIRE(mesh != nullptr);
            REQUIRE(!mesh->get_normals().empty());
        }
    }
    
    SECTION("Flip UV") {
        ModelProcessSetting setting;
        setting.flip_uv = true;
        setting.load_materials = false;
        
        auto model = Model::Load(model_path, setting);
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() > 0);
    }
    
    EngineContext::exit();
}
