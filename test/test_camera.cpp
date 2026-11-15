#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/input/input.h"
#include "test/test_utils.h"

TEST_CASE("Camera input smoothing preserves movement across frame rates", "[camera]") {
    test_utils::TestContext::reset();

    Scene scene;
    auto* fast_entity = scene.create_entity("Fast");
    fast_entity->add_component<TransformComponent>();
    auto* fast_camera = fast_entity->add_component<CameraComponent>();
    auto* slow_entity = scene.create_entity("Slow");
    slow_entity->add_component<TransformComponent>();
    auto* slow_camera = slow_entity->add_component<CameraComponent>();
    fast_camera->set_input_smoothing(0.06f);
    slow_camera->set_input_smoothing(0.06f);

    auto& input = Input::get_instance();
    input.on_key_down(Key::W);
    for (int frame = 0; frame < 60; ++frame) fast_camera->on_update(1.0f / 60);
    slow_camera->on_update(1.0f);
    input.on_key_up(Key::W);

    const float distance_when_released = fast_camera->get_position().z;
    CHECK(distance_when_released < 20.0f);
    CHECK(distance_when_released == Catch::Approx(slow_camera->get_position().z).margin(0.001f));
    for (int frame = 0; frame < 60; ++frame) fast_camera->on_update(1.0f / 60);
    CHECK(fast_camera->get_position().z == Catch::Approx(20.0f).margin(0.001f));

    test_utils::TestContext::reset();
}

TEST_CASE("Camera mouse smoothing gradually applies the full drag", "[camera]") {
    test_utils::TestContext::reset();

    Scene scene;
    auto* entity = scene.create_entity("Camera");
    auto* transform = entity->add_component<TransformComponent>();
    auto* camera = entity->add_component<CameraComponent>();
    camera->set_input_smoothing(0.06f);

    auto& input = Input::get_instance();
    int original_x, original_y;
    input.get_mouse_position(original_x, original_y);
    input.tick();
    input.on_mouse_button_down(MouseButton::Right);
    input.on_mouse_move(original_x + 40, original_y);
    input.tick();
    camera->on_update(1.0f / 60);
    input.on_mouse_button_up(MouseButton::Right);

    const float first_yaw = transform->transform.get_euler_angle().y;
    CHECK(first_yaw < 0.0f);
    CHECK(first_yaw > -20.0f);
    for (int frame = 0; frame < 60; ++frame) camera->on_update(1.0f / 60);
    CHECK(transform->transform.get_euler_angle().y == Catch::Approx(-20.0f).margin(0.001f));

    input.on_mouse_move(original_x, original_y);
    input.tick();
    test_utils::TestContext::reset();
}
