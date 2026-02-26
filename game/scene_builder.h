#pragma once

/**
 * @file game/scene_builder.h
 * @brief Creates the Earth-Moon-Ship scene
 *
 * Separated from main so the test can also include and use it.
 */

#include "game/earth_moon_ship.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/asset/asset_manager.h"

// Virtual paths — these resolve relative to /Game/ when asset->init("game")
static const std::string EARTH_MODEL_PATH = "/Game/earth/Planet.fbx";
static const std::string SHIP_MODEL_PATH  = "/Game/ship/ship.fbx";
static const std::string MOON_MODEL_PATH  = "/Game/moon/Moon 2K.fbx";
static const std::string SCENE_SAVE_PATH  = "/Game/earth_moon_ship_scene.asset";

inline bool create_earth_moon_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();

    auto earth_physical = EngineContext::asset()->get_physical_path(EARTH_MODEL_PATH);
    auto ship_physical  = EngineContext::asset()->get_physical_path(SHIP_MODEL_PATH);
    auto moon_physical  = EngineContext::asset()->get_physical_path(MOON_MODEL_PATH);

    if (!earth_physical) {
        ERR(LogEarthMoonShip, "Earth model not found at {}", EARTH_MODEL_PATH);
        return false;
    }
    if (!ship_physical) {
        ERR(LogEarthMoonShip, "Ship model not found at {}", SHIP_MODEL_PATH);
        return false;
    }

    // 1. Skybox
    auto* skybox_ent  = scene->create_entity("Skybox");
    skybox_ent->add_component<TransformComponent>();
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();

    auto panorama_texture = std::make_shared<Texture>("/Engine/textures/cosmic.jpg");
    if (panorama_texture && panorama_texture->texture_) {
        auto skybox_mat = std::make_shared<SkyboxMaterial>();
        skybox_mat->set_intensity(1.0f);
        skybox_mat->set_panorama_texture(panorama_texture);
        skybox_comp->set_material(skybox_mat);
        skybox_comp->set_skybox_scale(2000.0f);
    }

    // 2. Sun
    auto* light_ent   = scene->create_entity("Sun");
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({50.0f, 100.0f, 50.0f});
    light_trans->transform.set_rotation({-45.0f, 45.0f, 0.0f});
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    light_comp->set_color({1.0f, 0.95f, 0.9f});
    light_comp->set_intensity(2.0f);

    // 3. Earth
    auto* earth_ent   = scene->create_entity("Earth");
    auto* earth_trans = earth_ent->add_component<TransformComponent>();
    earth_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    earth_trans->transform.set_scale({10.0f, 10.0f, 10.0f});

    ModelProcessSetting earth_setting;
    earth_setting.smooth_normal  = false;  // Keep low-poly flat-shaded look
    earth_setting.load_materials = true;
    earth_setting.flip_uv        = true;

    auto earth_model = Model::Load(EARTH_MODEL_PATH, earth_setting);
    if (earth_model) {
        auto* renderer = earth_ent->add_component<MeshRendererComponent>();
        renderer->set_model(earth_model);
        renderer->save_asset_deps();
    }

    // 4. Moon (uses its own model if available, else Earth model)
    auto* moon_ent   = scene->create_entity("Moon");
    auto* moon_trans = moon_ent->add_component<TransformComponent>();
    moon_trans->transform.set_position({DEFAULT_MOON_ORBIT_DISTANCE, 0.0f, 0.0f});
    moon_trans->transform.set_scale({4.0f, 4.0f, 4.0f});
    moon_ent->add_component<MoonOrbitComponent>();

    ModelProcessSetting moon_setting;
    moon_setting.smooth_normal  = true;
    moon_setting.load_materials = true;
    moon_setting.flip_uv        = true;

    std::shared_ptr<Model> moon_model;
    if (moon_physical) {
        moon_model = Model::Load(MOON_MODEL_PATH, moon_setting);
    }
    auto& moon_render_model = moon_model ? moon_model : earth_model;

    if (moon_render_model) {
        auto* renderer = moon_ent->add_component<MeshRendererComponent>();
        renderer->set_model(moon_render_model);
        renderer->save_asset_deps();
    }

    // 5. Ship (parent-child: ShipRoot for gameplay, ShipVisual for model offset)
    auto* ship_ent   = scene->create_entity("Ship");
    auto* ship_trans = ship_ent->add_component<TransformComponent>();
    ship_trans->transform.set_position({DEFAULT_SHIP_EARTH_ORBIT_RADIUS, 0.0f, 0.0f});

    // Child entity holds the mesh with a static rotation offset to correct
    // the FBX model's default orientation (pitch -90 to lay flat, yaw 90
    // to align the forward axis).
    auto* ship_visual = ship_ent->create_child("ShipVisual");
    auto* visual_trans = ship_visual->add_component<TransformComponent>();
    visual_trans->transform.set_rotation({0.0f, 90.0f, 0.0f});
    visual_trans->transform.set_scale({.4f, .4f, .4f});

    ModelProcessSetting ship_setting;
    ship_setting.smooth_normal  = true;
    ship_setting.load_materials = true;
    ship_setting.flip_uv        = true;

    auto ship_model = Model::Load(SHIP_MODEL_PATH, ship_setting);
    if (ship_model) {
        auto* renderer = ship_visual->add_component<MeshRendererComponent>();
        renderer->set_model(ship_model);
        renderer->save_asset_deps();
    }

    // 6. Camera
    auto* cam_ent   = scene->create_entity("MainCamera");
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({25.0f, 10.0f, 25.0f});
    auto* cam_comp = cam_ent->add_component<CameraComponent>();
    cam_comp->set_fov(60.0f);
    cam_comp->set_near(0.1f);
    cam_comp->set_far(5000.0f);

    // 7. ShipController (must be after camera)
    auto* ship_ctrl = ship_ent->add_component<ShipController>();
    ship_ctrl->set_earth_entity(earth_ent);
    ship_ctrl->set_moon_entity(moon_ent);
    ship_ctrl->set_camera_entity(cam_ent);

    INFO(LogEarthMoonShip, "Scene created with {} entities", scene->entities_.size());
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}
