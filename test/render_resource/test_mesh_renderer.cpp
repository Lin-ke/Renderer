#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"

/**
 * @file test/render_resource/test_mesh_renderer.cpp
 * @brief Unit tests for MeshRenderer component integration.
 */

TEST_CASE("MeshRenderer Collection and ForwardPass", "[render_resource]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset_);
    mode.set(EngineContext::StartMode::Render_);
    
    EngineContext::init(mode);
    
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
    
    EngineContext::exit();
}
