#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render_resource/test_texture.cpp
 * @brief Unit tests for Texture render resource.
 */

DEFINE_LOG_TAG(LogTextureTest, "TextureTest");

TEST_CASE("Texture RHI Initialization", "[render_resource]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    EngineContext::asset()->init(test_asset_dir);

    INFO(LogTextureTest, "Checking RHI backend...");
    REQUIRE(EngineContext::rhi() != nullptr);

    Extent3D extent = { 128, 128, 1 };
    auto texture = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
    
    REQUIRE(texture->texture_ != nullptr);
    REQUIRE(texture->texture_view_ != nullptr);
    REQUIRE(texture->get_texture_type() == TextureType::Texture2D);
    
    std::vector<uint32_t> dummy_data(128 * 128, 0xFF0000FF);
    texture->set_data(dummy_data.data(), static_cast<uint32_t>(dummy_data.size() * sizeof(uint32_t)));

    texture.reset();
    EngineContext::exit();
}
