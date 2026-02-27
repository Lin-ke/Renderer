#pragma once

/**
 * @file game/earth_moon_ship.h
 * @brief Earth-Moon-Ship orbital mechanics and camera control components
 */

#include "engine/function/framework/component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/class_db.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

DECLARE_LOG_TAG(LogEarthMoonShip);

// ============================================================================
// Constants & Enums
// ============================================================================
static constexpr float DEFAULT_MOON_ORBIT_DISTANCE     = 100.0f;
static constexpr float DEFAULT_MOON_ORBIT_SPEED        = 0.05f;
static constexpr float DEFAULT_SHIP_EARTH_ORBIT_RADIUS = 35.0f;
static constexpr float DEFAULT_SHIP_EARTH_ORBIT_SPEED  = 0.5f;
static constexpr float DEFAULT_SHIP_MOON_ORBIT_RADIUS  = 10.0f;
static constexpr float DEFAULT_SHIP_MOON_ORBIT_SPEED   = 1.0f;
static constexpr float DEFAULT_TRANSFER_DURATION       = 8.0f;
static constexpr float DEFAULT_EARTH_SURFACE_OFFSET    = 6.0f;
static constexpr float DEFAULT_MOON_SURFACE_OFFSET     = 2.0f;

enum class ShipState {
    LandedOnEarth, EarthOrbit, TransferToMoon, MoonOrbit, TransferToEarth, LandedOnMoon,
    LandingOnEarth, LaunchingFromEarth, LandingOnMoon, LaunchingFromMoon
};

enum class CameraMode { Free, FirstPerson, ThirdPerson };

const char* ship_state_name(ShipState s);

// ============================================================================
// MoonOrbitComponent - Makes the Moon orbit Earth
// ============================================================================
class MoonOrbitComponent : public Component {
    CLASS_DEF(MoonOrbitComponent, Component)
public:
    MoonOrbitComponent() = default;
    void on_init() override;
    void on_update(float delta_time) override;

    float get_moon_orbit_distance() const { return moon_orbit_distance_; }
    float get_moon_orbit_speed() const { return moon_orbit_speed_; }
    float get_orbit_angle() const { return orbit_angle_; }

    static void register_class();

private:
    float orbit_angle_ = 0.0f;
    float moon_orbit_distance_ = DEFAULT_MOON_ORBIT_DISTANCE;
    float moon_orbit_speed_ = DEFAULT_MOON_ORBIT_SPEED;
};

CEREAL_REGISTER_TYPE(MoonOrbitComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, MoonOrbitComponent);

// ============================================================================
// ShipController - Orbital mechanics + Camera control
// ============================================================================
class ShipController : public Component {
    CLASS_DEF(ShipController, Component)
public:
    ShipController() = default;
    
    void set_earth_entity(Entity* e)  { earth_ = e; }
    void set_moon_entity(Entity* e)   { moon_ = e; }
    void set_camera_entity(Entity* e) { camera_ = e; }

    void on_init() override;
    void on_update(float delta_time) override;
    void draw_imgui();

    ShipState   get_state() const       { return state_; }
    CameraMode  get_camera_mode() const { return camera_mode_; }
    bool        should_show_missed_window_hint() const { return missed_window_timer_ > 0.0f; }
    const char* get_missed_window_message() const { return missed_window_message_.c_str(); }
    
    // Transfer info: returns true if currently transferring, fills source/target state names
    bool        get_transfer_info(const char** source_state, const char** target_state) const;

    static void register_class();

private:
    void handle_input();
    void handle_land_launch();
    void handle_transfer();
    
    void update_ship(float dt);
    void update_camera();
    
    // Helpers
    Vec3  get_earth_pos() const;
    Vec3  get_moon_pos() const;
    Vec3  get_ship_pos() const;
    float get_earth_radius() const;
    float get_moon_radius() const;
    float get_moon_angle() const;
    void  set_state(ShipState s);

    // Transitions
    void start_transition(ShipState target_state, float duration);
    void complete_transfer();
    void try_early_capture();

    // Data
    Entity *earth_ = nullptr, *moon_ = nullptr, *camera_ = nullptr;
    ShipState state_ = ShipState::EarthOrbit;
    CameraMode camera_mode_ = CameraMode::ThirdPerson;

    float earth_orbit_angle_ = 0.0f, moon_orbit_angle_ = 0.0f;
    float landed_angle_ = 0.0f, dt_ = 0.0f;

    // Transition data
    float trans_progress_ = 0.0f, trans_duration_ = 3.0f;
    Vec3  trans_p0_, trans_p1_, trans_p2_, trans_p3_;
    float trans_arrival_angle_ = 0.0f;
    bool  is_bezier_ = false;
    ShipState trans_source_state_ = ShipState::EarthOrbit;  // Source state of current transition

    // Camera data
    float tp_yaw_ = 0.0f, tp_pitch_ = 20.0f, tp_distance_ = 25.0f;
    float fp_yaw_ = 0.0f, fp_pitch_ = 0.0f;
    Vec3  free_pos_ = {25, 10, 25};
    float free_yaw_ = 0.0f, free_pitch_ = 0.0f, free_speed_ = 20.0f;

    // UI/Settings
    std::string missed_window_message_;
    float missed_window_timer_ = 0.0f;
    
    float ship_earth_orbit_radius_ = DEFAULT_SHIP_EARTH_ORBIT_RADIUS;
    float ship_earth_orbit_speed_ = DEFAULT_SHIP_EARTH_ORBIT_SPEED;
    float ship_moon_orbit_radius_ = DEFAULT_SHIP_MOON_ORBIT_RADIUS;
    float ship_moon_orbit_speed_ = DEFAULT_SHIP_MOON_ORBIT_SPEED;
    float transfer_duration_ = DEFAULT_TRANSFER_DURATION;
};

CEREAL_REGISTER_TYPE(ShipController);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, ShipController);
