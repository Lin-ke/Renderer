#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include <bitset>

TEST_CASE("MeshRenderer Collection and ForwardPass Test", "[mesh]") {
    // Initialize engine with Asset and Render modes
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset_);
    mode.set(EngineContext::StartMode::Render_);
    
    // Note: Render mode might try to initialize RHI and open a window
    // In some CI environments this might fail, but let's try.
    EngineContext::init(mode);
    
    auto render_system = EngineContext::render_system();
    REQUIRE(render_system != nullptr);
    
    auto mesh_manager = render_system->get_mesh_manager();
    REQUIRE(mesh_manager != nullptr);
    
    auto scene = std::make_shared<Scene>();
    auto entity = scene->create_entity();
    
    auto transform = entity->add_component<TransformComponent>();
    auto mesh_renderer = entity->add_component<MeshRendererComponent>();
    
    // Initialize component (registers with mesh manager)
    mesh_renderer->on_init();
    
    // Tick the mesh manager to trigger collection and pass building
    // This will also try to execute the RDG which might fail if RHI is not fully functional
    // but the collection logic should be exercised.
    mesh_manager->tick();
    
    auto forward_pass = mesh_manager->get_forward_pass();
    REQUIRE(forward_pass != nullptr);
    REQUIRE(forward_pass->get_type() == render::PassType::Forward);
    
    EngineContext::exit();
}
