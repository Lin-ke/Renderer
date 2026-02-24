#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <filesystem>
#include <fstream>
#include <d3dcompiler.h>

#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

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
    EngineContext::asset()->init(test_asset_dir);

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
    EngineContext::asset()->init(test_asset_dir);

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
    EngineContext::asset()->init(test_asset_dir);

    SECTION("Material Parameters and Serialization") {
        // Use PBRMaterial for specific properties
        auto material = std::make_shared<PBRMaterial>();
        
        Vec4 diffuse_color = { 1.0f, 0.5f, 0.2f, 1.0f };
        material->set_diffuse(diffuse_color);
        material->set_roughness(0.75f);
        material->set_metallic(0.1f);
        
        REQUIRE(material->get_diffuse().x() == Catch::Approx(1.0f));
        REQUIRE(material->get_roughness() == Catch::Approx(0.75f));
        
        std::string material_path = "/Game/test_material.asset";
        EngineContext::asset()->save_asset(material, material_path);
        
        // Load back as Material, then cast
        auto loaded_asset = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_asset != nullptr);
        
        auto loaded_pbr = std::dynamic_pointer_cast<PBRMaterial>(loaded_asset);
        REQUIRE(loaded_pbr != nullptr);
        REQUIRE(loaded_pbr->get_diffuse().y() == Catch::Approx(0.5f));
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
        REQUIRE(loaded_npr->get_rim_color().z() == Catch::Approx(0.9f));

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
        EngineContext::asset()->init(test_asset_dir);

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
    EngineContext::asset()->init(test_asset_dir);

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

    SECTION("Model Multiple Submeshes") {
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        
        std::string model_path = "/Engine/models/bunny.obj";
        auto model = Model::Load(model_path, setting);
        
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() >= 1);
        
        for (uint32_t i = 0; i < model->get_submesh_count(); i++) {
            auto mesh = model->get_mesh(i);
            REQUIRE(mesh != nullptr);
            REQUIRE(mesh->get_vertex_buffer() != nullptr);
            REQUIRE(mesh->get_index_buffer() != nullptr);
        }
        
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

    SECTION("Model Process Settings") {
        ModelProcessSetting setting1;
        setting1.smooth_normal = true;
        setting1.flip_uv = true;
        setting1.load_materials = false;
        
        CHECK(setting1.smooth_normal == true);
        CHECK(setting1.flip_uv == true);
        CHECK(setting1.load_materials == false);
        
        ModelProcessSetting setting2;
        setting2.smooth_normal = false;
        setting2.flip_uv = true;
        
        std::string model_path = "/Engine/models/bunny.obj";
        auto model = Model::Load(model_path, setting2);
        
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() > 0);
        
        model.reset();
    }

    test_utils::TestContext::reset();
}

TEST_CASE("MeshRenderer Collection and ForwardPass", "[render_resource]") {
    test_utils::TestContext::reset();
    
    auto render_system = EngineContext::render_system();
    REQUIRE(render_system != nullptr);
    
    auto mesh_manager = render_system->get_mesh_manager();
    REQUIRE(mesh_manager != nullptr);
    
    auto scene = std::make_shared<Scene>();
    auto entity = scene->create_entity();
    
    auto transform = entity->add_component<TransformComponent>();
    auto mesh_renderer = entity->add_component<MeshRendererComponent>();
    
    mesh_renderer->on_init();
    mesh_manager->tick();
    
    auto forward_pass = mesh_manager->get_forward_pass();
    REQUIRE(forward_pass != nullptr);
    REQUIRE(forward_pass->get_type() == render::PassType::Forward);
    
    test_utils::TestContext::reset();
}

TEST_CASE("FBX Material System", "[render_resource][fbx]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

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

    SECTION("FBX Model Processing Options") {
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
    }

    test_utils::TestContext::reset();
}

TEST_CASE("MTL Parser System", "[render_resource][mtl_parser]") {
    
    SECTION("NPR LightMap and RIM parameters") {
        // Create a test MTL file
        std::filesystem::path test_mtl = "./assets/models/test_npr.mtl";
        std::filesystem::create_directories(test_mtl.parent_path());
        
        {
            std::ofstream file(test_mtl);
            file << R"(# Test NPR MTL file
newmtl test_npr_material
map_Kd Texture\diffuse.png
Ka 0.2 0.2 0.2
Kd 0.8 0.8 0.8
Ks 0 0 0
Ns 5
d 1
# NPR parameters
map_Ke Texture\lightmap.png
map_Ramp Texture\ramp.png
RimWidth 0.5
RimThreshold 0.1
RimStrength 1.2
RimColor 1.0 0.9 0.8
LambertClamp 0.6
RampOffset 0.1
)";
        }
        
        // Verify file exists
        REQUIRE(std::filesystem::exists(test_mtl));
        
        // Read and verify content
        std::ifstream check_file(test_mtl);
        std::string content((std::istreambuf_iterator<char>(check_file)),
                            std::istreambuf_iterator<char>());
        check_file.close();  // Close file before deletion
        
        CHECK(content.find("map_Ke") != std::string::npos);
        CHECK(content.find("map_Ramp") != std::string::npos);
        CHECK(content.find("RimWidth") != std::string::npos);
        CHECK(content.find("RimThreshold") != std::string::npos);
        CHECK(content.find("RimStrength") != std::string::npos);
        CHECK(content.find("RimColor") != std::string::npos);
        CHECK(content.find("LambertClamp") != std::string::npos);
        CHECK(content.find("RampOffset") != std::string::npos);
        
        // Cleanup
        std::filesystem::remove(test_mtl);
    }

    SECTION("Klee MTL file exists") {
        std::filesystem::path klee_mtl = std::string(ENGINE_PATH) + "/assets/models/Klee/klee.mtl";
        REQUIRE(std::filesystem::exists(klee_mtl));
        
        std::ifstream file(klee_mtl);
        REQUIRE(file.is_open());
        
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        
        // Check for NPR-specific parameters
        CHECK(content.find("map_Ke") != std::string::npos);
        CHECK(content.find("map_Ramp") != std::string::npos);
        CHECK(content.find("RimWidth") != std::string::npos);
        CHECK(content.find("LambertClamp") != std::string::npos);
    }
}
