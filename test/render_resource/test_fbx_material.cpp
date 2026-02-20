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
    INFO(LogFbxMaterial, "Starting FBX model loading test...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    // Load FBX model with materials
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/material_ball.fbx";
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = true;  // Enable material loading
    setting.flip_uv = false;
    
    INFO(LogFbxMaterial, "Loading FBX model: {}", model_path);
    auto model = std::make_shared<Model>(model_path, setting);
    
    // Verify model loaded successfully
    REQUIRE(model->get_submesh_count() > 0);
    INFO(LogFbxMaterial, "FBX model loaded with {} submeshes", model->get_submesh_count());
    
    // Verify each submesh has valid data
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        const auto& submesh = model->submesh(i);
        REQUIRE(submesh.mesh != nullptr);
        REQUIRE(submesh.vertex_buffer != nullptr);
        REQUIRE(submesh.index_buffer != nullptr);
        
        INFO(LogFbxMaterial, "Submesh {}: {} vertices, {} indices", 
             i, 
             submesh.mesh->position.size(),
             submesh.mesh->index.size());
        
        // Check material
        auto material = model->get_material(i);
        if (material) {
            INFO(LogFbxMaterial, "Submesh {} has material: roughness={}, metallic={}",
                 i, material->get_roughness(), material->get_metallic());
        } else {
            INFO(LogFbxMaterial, "Submesh {} has no material (FBX may not have embedded materials)", i);
        }
    }
    
    EngineContext::exit();
}

TEST_CASE("FBX Model Without Materials", "[render_resource][fbx]") {
    INFO(LogFbxMaterial, "Testing FBX model without material loading...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/material_ball.fbx";
    
    // Load without materials
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;  // Disable material loading
    
    auto model = std::make_shared<Model>(model_path, setting);
    REQUIRE(model->get_submesh_count() > 0);
    
    // Verify no materials loaded
    for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
        REQUIRE(model->get_material(i) == nullptr);
    }
    
    INFO(LogFbxMaterial, "FBX without materials test passed");
    
    EngineContext::exit();
}

TEST_CASE("FBX Model Processing Options", "[render_resource][fbx]") {
    INFO(LogFbxMaterial, "Testing FBX model processing options...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/material_ball.fbx";
    
    SECTION("Smooth normals") {
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.load_materials = false;
        
        auto model = std::make_shared<Model>(model_path, setting);
        REQUIRE(model->get_submesh_count() > 0);
        
        // Verify normals exist
        for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
            REQUIRE(!model->submesh(i).mesh->normal.empty());
        }
        INFO(LogFbxMaterial, "Smooth normals test passed");
    }
    
    SECTION("Flip UV") {
        ModelProcessSetting setting;
        setting.flip_uv = true;
        setting.load_materials = false;
        
        auto model = std::make_shared<Model>(model_path, setting);
        REQUIRE(model->get_submesh_count() > 0);
        INFO(LogFbxMaterial, "Flip UV test passed");
    }
    
    EngineContext::exit();
}
