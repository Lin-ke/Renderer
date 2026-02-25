#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <filesystem>
#include <fstream>
#include <d3dcompiler.h>

#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/asset/asset_manager.h"

/**
 * @file test/render_resource/test_render_resources.cpp
 * @brief Consolidated unit tests for render resources.
 */

// Log tags for different test sections
DEFINE_LOG_TAG(LogTextureTest, "TextureTest");
DEFINE_LOG_TAG(LogShaderTest, "ShaderTest");
DEFINE_LOG_TAG(LogMaterialTest, "MaterialTest");
DEFINE_LOG_TAG(LogModelTest, "ModelTest");
DEFINE_LOG_TAG(LogFbxMaterial, "FbxMaterial");

// Helper to compile shader for testing
static std::vector<uint8_t> compile_shader_test(const std::string& source, const std::string& entry, const std::string& profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr, entry.c_str(), profile.c_str(), D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &blob, &error_blob);
    
    if (FAILED(hr)) {
        if (error_blob) {
            error_blob->Release();
        }
        return {};
    }
    
    std::vector<uint8_t> code(blob->GetBufferSize());
    memcpy(code.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();
    if (error_blob) error_blob->Release();
    return code;
}

TEST_CASE("Texture RHI Initialization", "[render_resource]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    REQUIRE(EngineContext::rhi() != nullptr);

    Extent3D extent = { 128, 128, 1 };
    auto texture = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
    
    REQUIRE(texture->texture_ != nullptr);
    REQUIRE(texture->texture_view_ != nullptr);
    REQUIRE(texture->get_texture_type() == TextureType::Texture2D);
    
    std::vector<uint32_t> dummy_data(128 * 128, 0xFF0000FF);
    texture->set_data(dummy_data.data(), static_cast<uint32_t>(dummy_data.size() * sizeof(uint32_t)));

    texture.reset();
    test_utils::TestContext::reset();
}

TEST_CASE("Shader Loading and Serialization", "[render_resource]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    const std::string vs_source = R"(
        float4 main(float3 position : POSITION) : SV_POSITION {
            return float4(position, 1.0);
        }
    )";
    auto shader_code = compile_shader_test(vs_source, "main", "vs_5_0");
    REQUIRE(!shader_code.empty());

    std::string virtual_path = "/Game/test_shader.bin";
    auto physical_path_opt = EngineContext::asset()->get_physical_path(virtual_path);
    REQUIRE(physical_path_opt.has_value());
    
    {
        std::ofstream ofs(physical_path_opt->string(), std::ios::binary);
        ofs.write((char*)shader_code.data(), shader_code.size());
    }

    auto shader = std::make_shared<Shader>(virtual_path, SHADER_FREQUENCY_VERTEX, "main");
    REQUIRE(shader->get_file_path() == virtual_path);
    REQUIRE(shader->get_frequency() == SHADER_FREQUENCY_VERTEX);
    REQUIRE(shader->shader_ != nullptr);

    std::string asset_path = "/Game/test_shader_asset.asset";
    EngineContext::asset()->save_asset(shader, asset_path);

    auto loaded_shader = EngineContext::asset()->load_asset<Shader>(asset_path);
    REQUIRE(loaded_shader != nullptr);
    CHECK(loaded_shader->get_file_path() == virtual_path);
    CHECK(loaded_shader->get_frequency() == SHADER_FREQUENCY_VERTEX);
    CHECK(loaded_shader->get_entry() == "main");
    REQUIRE(loaded_shader->shader_ != nullptr);

    test_utils::TestContext::reset();
}

TEST_CASE("Material System", "[render_resource]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    SECTION("Material Parameters and Serialization") {
        // Use PBRMaterial for specific properties
        auto material = std::make_shared<PBRMaterial>();
        
        Vec4 diffuse_color = { 1.0f, 0.5f, 0.2f, 1.0f };
        material->set_diffuse(diffuse_color);
        material->set_roughness(0.75f);
        material->set_metallic(0.1f);
        
        REQUIRE(material->get_diffuse().x == Catch::Approx(1.0f));
        REQUIRE(material->get_roughness() == Catch::Approx(0.75f));
        
        std::string material_path = "/Game/test_material.asset";
        EngineContext::asset()->save_asset(material, material_path);
        
        // Load back as Material, then cast
        auto loaded_asset = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_asset != nullptr);
        
        auto loaded_pbr = std::dynamic_pointer_cast<PBRMaterial>(loaded_asset);
        REQUIRE(loaded_pbr != nullptr);
        REQUIRE(loaded_pbr->get_diffuse().y == Catch::Approx(0.5f));
        REQUIRE(loaded_pbr->get_metallic() == Catch::Approx(0.1f));

        loaded_asset.reset();
        loaded_pbr.reset();
    }

    SECTION("NPR Material Parameters and Serialization") {
        // Use NPRMaterial
        auto material = std::make_shared<NPRMaterial>();
        
        material->set_rim_strength(0.8f);
        material->set_rim_width(0.4f);
        Vec3 rim_color = { 0.1f, 0.2f, 0.9f };
        material->set_rim_color(rim_color);
        
        REQUIRE(material->get_rim_strength() == Catch::Approx(0.8f));
        
        std::string material_path = "/Game/test_npr_material.asset";
        EngineContext::asset()->save_asset(material, material_path);
        
        // Load back as Material, then cast
        auto loaded_asset = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_asset != nullptr);
        REQUIRE(loaded_asset->get_material_type() == MaterialType::NPR);
        
        auto loaded_npr = std::dynamic_pointer_cast<NPRMaterial>(loaded_asset);
        REQUIRE(loaded_npr != nullptr);
        REQUIRE(loaded_npr->get_rim_width() == Catch::Approx(0.4f));
        REQUIRE(loaded_npr->get_rim_color().z == Catch::Approx(0.9f));

        loaded_asset.reset();
        loaded_npr.reset();
    }

    SECTION("Material Texture Dependencies") {
        Extent3D extent = { 64, 64, 1 };
        auto texture = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
        texture->texture_id_ = 123; 
        
        std::string texture_path = "/Game/test_tex.binasset";
        EngineContext::asset()->save_asset(texture, texture_path);
        UID tex_uid = texture->get_uid();

        // Use PBRMaterial for texture dependencies test
        auto material = std::make_shared<PBRMaterial>();
        material->set_diffuse_texture(texture);
        
        std::string material_path = "/Game/dep_material.asset";
        EngineContext::asset()->save_asset(material, material_path);

        material.reset();
        texture.reset();
        
        test_utils::TestContext::reset();
        
        // Cold reload
        auto loaded_material = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_material != nullptr);
        
        auto loaded_pbr = std::dynamic_pointer_cast<PBRMaterial>(loaded_material);
        REQUIRE(loaded_pbr != nullptr);
        REQUIRE(loaded_pbr->get_diffuse_texture() != nullptr);
        CHECK(loaded_pbr->get_diffuse_texture()->get_uid() == tex_uid);

        loaded_material.reset();
    }

    test_utils::TestContext::reset();
}

TEST_CASE("Model System", "[render_resource]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    SECTION("Model Loading with bunny.obj") {
        REQUIRE(EngineContext::rhi() != nullptr);
        
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.flip_uv = false;
        setting.load_materials = false;
        
        std::string model_path = "/Engine/models/bunny.obj";
        auto model = Model::Load(model_path, setting);
        
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() > 0);
        
        auto mesh = model->get_mesh(0);
        REQUIRE(mesh != nullptr);
        REQUIRE(mesh->get_positions().size() > 0);
        REQUIRE(mesh->get_indices().size() > 0);
        
        REQUIRE(mesh->get_vertex_buffer() != nullptr);
        REQUIRE(mesh->get_index_buffer() != nullptr);
        
        model.reset();
    }

    SECTION("Mesh Data Structure") {
        std::vector<Vec3> positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
        std::vector<Vec3> normals = { Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1) };
        std::vector<Vec2> tex_coords = { Vec2(0, 0), Vec2(1, 0), Vec2(0, 1) };
        std::vector<uint32_t> indices = { 0, 1, 2 };
        
        auto mesh = Mesh::Create(positions, normals, {}, tex_coords, indices, "mesh1");
        REQUIRE(mesh != nullptr);
        
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

    test_utils::TestContext::reset();
}

TEST_CASE("FBX Material System", "[render_resource][fbx]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    SECTION("FBX Model Loading with Materials") {
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
    }

    SECTION("FBX Model Without Materials") {
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
    }

    test_utils::TestContext::reset();
}
