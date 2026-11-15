#include <catch2/catch_test_macros.hpp>

#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
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
