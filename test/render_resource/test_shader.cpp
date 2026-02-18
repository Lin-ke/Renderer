#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/shader.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"
#include <fstream>
#include <d3dcompiler.h>

/**
 * @file test/render_resource/test_shader.cpp
 * @brief Unit tests for Shader render resource.
 */

DEFINE_LOG_TAG(LogShaderTest, "ShaderTest");

// Helper to compile shader
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

TEST_CASE("Shader Loading and Serialization", "[render_resource]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    
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
    REQUIRE(shader->shader_ != nullptr);

    std::string asset_path = "/Game/test_shader_asset.asset";
    EngineContext::asset()->save_asset(shader, asset_path);

    auto loaded_shader = EngineContext::asset()->load_asset<Shader>(asset_path);
    REQUIRE(loaded_shader != nullptr);
    CHECK(loaded_shader->get_file_path() == virtual_path);
    CHECK(loaded_shader->get_frequency() == SHADER_FREQUENCY_VERTEX);
    CHECK(loaded_shader->get_entry() == "main");
    REQUIRE(loaded_shader->shader_ != nullptr);

    EngineContext::exit();
}
