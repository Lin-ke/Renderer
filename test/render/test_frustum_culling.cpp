#include "test/test_assertions.h"

#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/render/render_pass/draw_material.h"
#include "engine/main/engine_context.h"
#include "test/test_utils.h"

TEST_CASE("Mesh batches cull against the camera frustum conservatively", "[render][frustum]") {
    test_utils::TestContext::reset();

    {
        Scene scene;
        auto* entity = scene.create_entity();
        auto* transform = entity->add_component<TransformComponent>();
        auto* renderer = entity->add_component<MeshRendererComponent>();
        auto model = std::make_shared<Model>();
        auto add_triangle = [&](float left, float right, float depth) {
            model->add_slot(Mesh::Create({{left, 0.0f, depth}, {right, 0.0f, depth}, {left, 0.2f, depth}},
                {}, {}, {}, {0, 1, 2}), nullptr);
        };
        add_triangle(0.0f, 0.2f, 3.0f);
        add_triangle(2.9f, 3.1f, 3.0f);
        add_triangle(4.0f, 4.2f, 3.0f);
        add_triangle(0.0f, 0.2f, -2.0f);
        add_triangle(0.0f, 0.2f, 11.0f);
        renderer->set_model(model);

        const auto projection = Math::perspective(Math::to_radians(90.0f), 1.0f, 0.1f, 10.0f);
        std::vector<render::DrawBatch> batches;
        renderer->collect_draw_batch(batches, &projection);
        REQUIRE(batches.size() == 2);
        CHECK(batches[0].vertex_buffer == model->get_vertex_buffer(0)->position_buffer_);
        CHECK(batches[1].vertex_buffer == model->get_vertex_buffer(1)->position_buffer_);

        transform->transform.set_scale({-1.0f, 2.0f, 1.0f});
        transform->transform.set_position({6.0f, 0.0f, 0.0f});
        batches.clear();
        renderer->collect_draw_batch(batches, &projection);
        REQUIRE(batches.size() == 2);
        CHECK(batches[0].vertex_buffer == model->get_vertex_buffer(1)->position_buffer_);
        CHECK(batches[1].vertex_buffer == model->get_vertex_buffer(2)->position_buffer_);

        batches.clear();
        renderer->collect_draw_batch(batches);
        CHECK(batches.size() == model->get_slot_count());
    }

    test_utils::TestContext::reset();
}

TEST_CASE("Render packets keep instance and material state after the scene changes", "[render][render_packet]") {
    test_utils::TestContext::reset();
    {
        auto renderer = EngineContext::render_system();
        auto scene = std::make_unique<Scene>();
        auto* entity = scene->create_entity();
        auto* transform = entity->add_component<TransformComponent>();
        auto* mesh_renderer = entity->add_component<MeshRendererComponent>();
        auto material = std::make_shared<PBRMaterial>();
        material->set_diffuse({1.0f, 0.0f, 0.0f, 1.0f});
        auto model = std::make_shared<Model>();
        model->add_slot(Mesh::Create({{0, 0, 3}, {1, 0, 3}, {0, 1, 3}}, {}, {}, {}, {0, 1, 2}), material);
        mesh_renderer->set_model(model);

        Scene other_scene;
        auto* other = other_scene.create_entity();
        other->add_component<TransformComponent>();
        auto* other_renderer = other->add_component<MeshRendererComponent>();
        other_renderer->set_model(model);
        other_renderer->on_init();

        auto first = renderer->make_render_packet(scene.get(), nullptr, 8);
        transform->transform.set_position({5.0f, 0.0f, 0.0f});
        material->set_diffuse({0.0f, 1.0f, 0.0f, 1.0f});
        auto second = renderer->make_render_packet(scene.get(), nullptr, 9);
        scene.reset();
        model.reset();

        REQUIRE(first.instances.size() == 1);
        REQUIRE(second.instances.size() == 1);
        CHECK(first.frame_id == 8);
        CHECK(second.frame_id == 9);
        CHECK(first.instances[0].model_matrix.m[3][0] == 0.0f);
        CHECK(second.instances[0].model_matrix.m[3][0] == 5.0f);
        REQUIRE(first.instances[0].material_snapshot->pbr);
        REQUIRE(second.instances[0].material_snapshot->pbr);
        CHECK(first.instances[0].material_snapshot->pbr->albedo.x == 1.0f);
        CHECK(second.instances[0].material_snapshot->pbr->albedo.y == 1.0f);
        CHECK(first.instances[0].vertex_buffer);
    }
    test_utils::TestContext::reset();
}
