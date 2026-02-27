#pragma once

/**
 * @file game/earth_moon_ship.h
 * @brief Earth-Moon-Ship orbital mechanics and camera control components
 *
 * Key bindings:
 *   L  - Land / Launch (toggle between surface and orbit)
 *   T  - Transfer orbit (start transfer to other body, or early capture)
 *   V  - Toggle camera mode (first person / third person)
 *   Right Mouse + Drag  - Camera rotation
 *   Scroll Wheel        - Third person camera distance
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
#include <imgui.h>

DECLARE_LOG_TAG(LogEarthMoonShip);
// ============================================================================
// Constants (default values)
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

// ============================================================================
// Enums
// ============================================================================
enum class ShipState {
    LandedOnEarth,
    EarthOrbit,
    TransferToMoon,
    MoonOrbit,
    TransferToEarth,
    LandedOnMoon
};

enum class CameraMode {
    Free,
    FirstPerson,
    ThirdPerson
};

inline const char* ship_state_name(ShipState s) {
    switch (s) {
        case ShipState::LandedOnEarth:   return "LandedOnEarth";
        case ShipState::EarthOrbit:      return "EarthOrbit";
        case ShipState::TransferToMoon:  return "TransferToMoon";
        case ShipState::MoonOrbit:       return "MoonOrbit";
        case ShipState::TransferToEarth: return "TransferToEarth";
        case ShipState::LandedOnMoon:    return "LandedOnMoon";
        default:                         return "Unknown";
    }
}

// ============================================================================
// Utility functions
// ============================================================================
inline float normalize_angle(float angle) {
    while (angle < 0.0f)       angle += 2.0f * PI;
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    return angle;
}

inline Vec3 bezier_eval(const Vec3& p0, const Vec3& p1,
                        const Vec3& p2, const Vec3& p3, float t) {
    float u = 1.0f - t;
    return p0 * (u * u * u)
         + p1 * (3.0f * u * u * t)
         + p2 * (3.0f * u * t * t)
         + p3 * (t * t * t);
}

inline Vec3 bezier_tangent(const Vec3& p0, const Vec3& p1,
                           const Vec3& p2, const Vec3& p3, float t) {
    float u = 1.0f - t;
    return (p1 - p0) * (3.0f * u * u)
         + (p2 - p1) * (6.0f * u * t)
         + (p3 - p2) * (3.0f * t * t);
}

// ============================================================================
// MoonOrbitComponent - Makes the Moon orbit Earth
// ============================================================================
class MoonOrbitComponent : public Component {
    CLASS_DEF(MoonOrbitComponent, Component)
public:
    MoonOrbitComponent() = default;
    virtual ~MoonOrbitComponent() = default;

    void on_init() override { orbit_angle_ = 0.0f; }

    void on_update(float delta_time) override {
        orbit_angle_ += moon_orbit_speed_ * delta_time;
        orbit_angle_ = normalize_angle(orbit_angle_);

        auto* trans = get_owner()->get_component<TransformComponent>();
        if (!trans) return;

        float x = moon_orbit_distance_ * std::cos(orbit_angle_);
        float z = moon_orbit_distance_ * std::sin(orbit_angle_);
        trans->transform.set_position({x, 0.0f, z});
    }

    float get_moon_orbit_distance() const { return moon_orbit_distance_; }
    float get_moon_orbit_speed() const { return moon_orbit_speed_; }

    float get_orbit_angle() const { return orbit_angle_; }

    static void register_class() {
        Registry::add<MoonOrbitComponent>("MoonOrbitComponent")
            .member("orbit_angle", &MoonOrbitComponent::orbit_angle_)
            .member("orbit_distance", &MoonOrbitComponent::moon_orbit_distance_)
            .member("orbit_speed", &MoonOrbitComponent::moon_orbit_speed_);
    }

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
    virtual ~ShipController() = default;

    void set_earth_entity(Entity* e)  { earth_ = e; }
    void set_moon_entity(Entity* e)   { moon_ = e; }
    void set_camera_entity(Entity* e) { camera_ = e; }

    void on_init() override {
        // Debug: verify owner is correct
        if (get_owner()) {
            std::string name = get_owner()->get_name();
            if (name != "Ship") {
                ERR(LogEarthMoonShip, "ShipController attached to wrong entity: '{}', expected 'Ship'!", name);
            }
        }
        
        state_       = ShipState::EarthOrbit;
        camera_mode_ = CameraMode::ThirdPerson;

        earth_orbit_angle_ = 0.0f;
        moon_orbit_angle_  = 0.0f;
        transfer_progress_ = 0.0f;
        landed_angle_      = 0.0f;

        tp_yaw_      = 0.0f;
        tp_pitch_    = 20.0f;
        tp_distance_ = 15.0f;
        fp_yaw_      = 0.0f;
        fp_pitch_    = 0.0f;

        // Initialize orbit parameters (editable via reflection)
        ship_earth_orbit_radius_ = DEFAULT_SHIP_EARTH_ORBIT_RADIUS;
        ship_earth_orbit_speed_  = DEFAULT_SHIP_EARTH_ORBIT_SPEED;
        ship_moon_orbit_radius_  = DEFAULT_SHIP_MOON_ORBIT_RADIUS;
        ship_moon_orbit_speed_   = DEFAULT_SHIP_MOON_ORBIT_SPEED;
        transfer_duration_       = DEFAULT_TRANSFER_DURATION;
        earth_surface_offset_    = DEFAULT_EARTH_SURFACE_OFFSET;
        moon_surface_offset_     = DEFAULT_MOON_SURFACE_OFFSET;

        if (camera_) {
            if (auto* cam = camera_->get_component<CameraComponent>()) {
                cam->set_external_control(true);
            }
        }

        INFO(LogEarthMoonShip, "=== Ship Controller Initialized ===");
    }

    void on_update(float delta_time) override {
        delta_time_ = delta_time;
        handle_input();
        update_ship(delta_time);
        update_camera();
        
        // Decay missed window hint timer
        if (missed_window_timer_ > 0.0f) {
            missed_window_timer_ -= delta_time;
            if (missed_window_timer_ < 0.0f) missed_window_timer_ = 0.0f;
        }
    }

    ShipState   get_state() const       { return state_; }
    CameraMode  get_camera_mode() const { return camera_mode_; }
    
    // Missed window hint
    bool should_show_missed_window_hint() const { return missed_window_timer_ > 0.0f; }
    const char* get_missed_window_message() const { return missed_window_message_; }

    void draw_imgui() {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("Controls", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Ship Controls");
        ImGui::Separator();
        ImGui::BulletText("L  - Land / Launch");
        ImGui::BulletText("T  - Transfer orbit / Early capture");
        ImGui::BulletText("V  - Camera: Free / 1st / 3rd");

        ImGui::Spacing();
        const char* cam_name = "";
        switch (camera_mode_) {
            case CameraMode::Free:        cam_name = "Free"; break;
            case CameraMode::FirstPerson: cam_name = "1st Person"; break;
            case CameraMode::ThirdPerson: cam_name = "3rd Person"; break;
        }
        ImGui::Text("Camera: %s", cam_name);
        ImGui::Text("State:  %s", ship_state_name(state_));

        ImGui::Separator();
        switch (camera_mode_) {
            case CameraMode::Free:
                ImGui::BulletText("WASD  - Move");
                ImGui::BulletText("Q/E   - Down / Up");
                ImGui::BulletText("Shift - Speed boost");
                ImGui::BulletText("RMB   - Look around");
                ImGui::BulletText("Scroll - Adjust speed");
                break;
            case CameraMode::FirstPerson:
                ImGui::BulletText("RMB + Drag - Look around");
                break;
            case CameraMode::ThirdPerson:
                ImGui::BulletText("RMB + Drag - Orbit camera");
                ImGui::BulletText("Scroll     - Distance");
                break;
        }

        ImGui::End();
    }

    static void register_class() {
        Registry::add<ShipController>("ShipController")
            .member("earth_orbit_angle", &ShipController::earth_orbit_angle_)
            .member("earth_orbit_radius", &ShipController::ship_earth_orbit_radius_)
            .member("earth_orbit_speed", &ShipController::ship_earth_orbit_speed_)
            .member("moon_orbit_radius", &ShipController::ship_moon_orbit_radius_)
            .member("moon_orbit_speed", &ShipController::ship_moon_orbit_speed_)
            .member("transfer_duration", &ShipController::transfer_duration_)
            .member("earth_surface_offset", &ShipController::earth_surface_offset_)
            .member("moon_surface_offset", &ShipController::moon_surface_offset_);
    }

private:
    // ====== Input ======
    void handle_input() {
        const auto& input = Input::get_instance();

        if (input.is_key_pressed(Key::V)) {
            switch (camera_mode_) {
                case CameraMode::ThirdPerson: {
                    // Third -> Free: inherit current camera world position
                    camera_mode_ = CameraMode::Free;
                    auto* cam_trans = camera_ ? camera_->get_component<TransformComponent>() : nullptr;
                    if (cam_trans) {
                        // Use world position to support hierarchy
                        free_pos_ = cam_trans->get_world_position();
                        Vec3 euler = cam_trans->transform.get_euler_angle();
                        free_yaw_   = euler.y;
                        free_pitch_ = euler.x;
                    }
                    INFO(LogEarthMoonShip, "Camera: Free");
                    break;
                }
                case CameraMode::Free: {
                    // Free -> First
                    camera_mode_ = CameraMode::FirstPerson;
                    auto* ship_trans = get_owner()->get_component<TransformComponent>();
                    if (ship_trans) {
                        Vec3 euler = ship_trans->transform.get_euler_angle();
                        fp_yaw_   = euler.y;
                        fp_pitch_ = euler.x;
                    }
                    INFO(LogEarthMoonShip, "Camera: First Person");
                    break;
                }
                case CameraMode::FirstPerson: {
                    // First -> Third
                    camera_mode_ = CameraMode::ThirdPerson;
                    INFO(LogEarthMoonShip, "Camera: Third Person");
                    break;
                }
            }
        }

        if (input.is_key_pressed(Key::L)) handle_land_launch();
        if (input.is_key_pressed(Key::T)) handle_transfer();

        if (input.is_mouse_button_down(MouseButton::Right)) {
            float dx, dy;
            input.get_mouse_delta(dx, dy);
            if (camera_mode_ == CameraMode::Free) {
                free_yaw_   -= dx * mouse_sensitivity_;
                free_pitch_ -= dy * mouse_sensitivity_;
                free_pitch_  = std::clamp(free_pitch_, -89.0f, 89.0f);
            } else if (camera_mode_ == CameraMode::FirstPerson) {
                fp_yaw_   -= dx * mouse_sensitivity_;
                fp_pitch_ -= dy * mouse_sensitivity_;
                fp_pitch_  = std::clamp(fp_pitch_, -89.0f, 89.0f);
            } else {
                tp_yaw_   -= dx * mouse_sensitivity_;
                tp_pitch_ -= dy * mouse_sensitivity_;
                tp_pitch_  = std::clamp(tp_pitch_, -70.0f, 80.0f);
            }
        }

        if (camera_mode_ == CameraMode::ThirdPerson) {
            float scroll = input.get_scroll_delta();
            if (std::abs(scroll) > 0.001f) {
                tp_distance_ -= scroll * 2.0f;
                tp_distance_  = std::clamp(tp_distance_, TP_MIN_DIST, TP_MAX_DIST);
            }
        }
    }

    // ====== Land / Launch ======
    void handle_land_launch() {
        switch (state_) {
            case ShipState::EarthOrbit:
                state_ = ShipState::LandedOnEarth;
                landed_angle_ = earth_orbit_angle_;
                INFO(LogEarthMoonShip, "State: EarthOrbit -> LandedOnEarth");
                break;
            case ShipState::LandedOnEarth:
                state_ = ShipState::EarthOrbit;
                earth_orbit_angle_ = landed_angle_;
                INFO(LogEarthMoonShip, "State: LandedOnEarth -> EarthOrbit");
                break;
            case ShipState::MoonOrbit:
                state_ = ShipState::LandedOnMoon;
                landed_angle_ = moon_orbit_angle_;
                INFO(LogEarthMoonShip, "State: MoonOrbit -> LandedOnMoon");
                break;
            case ShipState::LandedOnMoon:
                state_ = ShipState::MoonOrbit;
                moon_orbit_angle_ = landed_angle_;
                INFO(LogEarthMoonShip, "State: LandedOnMoon -> MoonOrbit");
                break;
            default:
                INFO(LogEarthMoonShip, "Cannot land/launch in state: {}",
                     ship_state_name(state_));
                break;
        }
    }

    // ====== Transfer ======
    void handle_transfer() {
        switch (state_) {
            case ShipState::EarthOrbit:
                if (can_transfer_to_moon()) {
                    start_transfer_to_moon();
                } else {
                    float diff = Math::to_angle(
                        normalize_angle(get_moon_angle() - earth_orbit_angle_));
                    INFO(LogEarthMoonShip,
                         "Transfer window closed (Moon {:.0f} deg ahead, need 30-150)",
                         diff);
                    // Trigger missed window hint for UI
                    missed_window_timer_ = 3.0f;
                    missed_window_message_ = "Transfer window closed!\nMoon must be 30-150 degrees ahead.";
                }
                break;
            case ShipState::MoonOrbit:
                if (can_transfer_to_earth()) {
                    start_transfer_to_earth();
                } else {
                    INFO(LogEarthMoonShip,
                         "Transfer window closed. Orbit to Earth-facing side.");
                    // Trigger missed window hint for UI
                    missed_window_timer_ = 3.0f;
                    missed_window_message_ = "Transfer window closed!\nFly to Earth-facing side of Moon.";
                }
                break;
            case ShipState::TransferToMoon:
            case ShipState::TransferToEarth:
                try_early_capture();
                break;
            default:
                INFO(LogEarthMoonShip, "Cannot transfer in state: {}",
                     ship_state_name(state_));
                break;
        }
    }

    // ====== Transfer conditions ======
    bool can_transfer_to_moon() const {
        float diff = normalize_angle(get_moon_angle() - earth_orbit_angle_);
        float deg  = Math::to_angle(diff);
        return deg > 30.0f && deg < 150.0f;
    }

    bool can_transfer_to_earth() const {
        Vec3 moon_pos  = get_moon_position();
        Vec3 earth_pos = get_earth_position();
        Vec3 ship_pos  = get_ship_position();
        Vec3 to_earth  = (earth_pos - moon_pos).normalized();
        Vec3 to_ship   = (ship_pos  - moon_pos).normalized();
        return to_earth.dot(to_ship) > 0.3f;
    }

    // ====== Start transfers ======
    void start_transfer_to_moon() {
        state_             = ShipState::TransferToMoon;
        transfer_progress_ = 0.0f;

        Vec3 earth_pos = get_earth_position();
        Vec3 ship_pos  = get_ship_position();

        // Get moon orbit parameters from MoonOrbitComponent
        float moon_distance = DEFAULT_MOON_ORBIT_DISTANCE;
        float moon_speed = DEFAULT_MOON_ORBIT_SPEED;
        if (moon_) {
            if (auto* moon_orbit = moon_->get_component<MoonOrbitComponent>()) {
                moon_distance = moon_orbit->get_moon_orbit_distance();
                moon_speed = moon_orbit->get_moon_orbit_speed();
            }
        }
        
        float predicted_angle = get_moon_angle() + moon_speed * transfer_duration_;
        Vec3 predicted_moon(
            earth_pos.x + moon_distance * std::cos(predicted_angle),
            0.0f,
            earth_pos.z + moon_distance * std::sin(predicted_angle));

        Vec3 to_earth = (earth_pos - predicted_moon).normalized();
        Vec3 arrival  = predicted_moon + to_earth * ship_moon_orbit_radius_;

        Vec3 start_tan(-std::sin(earth_orbit_angle_), 0.0f,
                        std::cos(earth_orbit_angle_));
        Vec3 moon_dir = (predicted_moon - earth_pos).normalized();
        Vec3 end_tan(-moon_dir.z, 0.0f, moon_dir.x);

        float dist   = (arrival - ship_pos).length();
        float factor = dist * 0.4f;

        transfer_p0_ = ship_pos;
        transfer_p1_ = ship_pos + start_tan * factor;
        transfer_p2_ = arrival  - end_tan   * factor;
        transfer_p3_ = arrival;

        INFO(LogEarthMoonShip,
             "State: EarthOrbit -> TransferToMoon (ETA {:.1f}s)", transfer_duration_);
    }

    void start_transfer_to_earth() {
        state_             = ShipState::TransferToEarth;
        transfer_progress_ = 0.0f;

        Vec3 earth_pos = get_earth_position();
        Vec3 moon_pos  = get_moon_position();
        Vec3 ship_pos  = get_ship_position();

        float arrival_angle = std::atan2(
            ship_pos.z - earth_pos.z, ship_pos.x - earth_pos.x);
        arrival_angle = normalize_angle(
            arrival_angle - ship_earth_orbit_speed_ * transfer_duration_ * 0.3f);

        Vec3 arrival(
            earth_pos.x + ship_earth_orbit_radius_ * std::cos(arrival_angle),
            0.0f,
            earth_pos.z + ship_earth_orbit_radius_ * std::sin(arrival_angle));

        Vec3 from_moon = (ship_pos - moon_pos).normalized();
        Vec3 start_tan(-from_moon.z, 0.0f, from_moon.x);
        Vec3 end_tan(-std::sin(arrival_angle), 0.0f, std::cos(arrival_angle));

        float dist   = (arrival - ship_pos).length();
        float factor = dist * 0.4f;

        transfer_p0_ = ship_pos;
        transfer_p1_ = ship_pos + start_tan * factor;
        transfer_p2_ = arrival  - end_tan   * factor;
        transfer_p3_ = arrival;
        transfer_arrival_angle_ = arrival_angle;

        INFO(LogEarthMoonShip,
             "State: MoonOrbit -> TransferToEarth (ETA {:.1f}s)", transfer_duration_);
    }

    void try_early_capture() {
        Vec3 ship_pos = get_ship_position();

        if (state_ == ShipState::TransferToMoon) {
            Vec3 moon_pos = get_moon_position();
            float dist = (ship_pos - moon_pos).length();
            if (dist < ship_moon_orbit_radius_ * 2.0f) {
                Vec3 rel = ship_pos - moon_pos;
                moon_orbit_angle_ = std::atan2(rel.z, rel.x);
                state_ = ShipState::MoonOrbit;
                INFO(LogEarthMoonShip,
                     "Early capture -> MoonOrbit (dist {:.1f})", dist);
            } else {
                INFO(LogEarthMoonShip,
                     "Too far from Moon ({:.1f}), need < {:.1f}",
                     dist, ship_moon_orbit_radius_ * 2.0f);
            }
        } else {
            Vec3 earth_pos = get_earth_position();
            float dist = (ship_pos - earth_pos).length();
            if (dist < ship_earth_orbit_radius_ * 1.5f) {
                Vec3 rel = ship_pos - earth_pos;
                earth_orbit_angle_ = std::atan2(rel.z, rel.x);
                state_ = ShipState::EarthOrbit;
                INFO(LogEarthMoonShip,
                     "Early capture -> EarthOrbit (dist {:.1f})", dist);
            } else {
                INFO(LogEarthMoonShip,
                     "Too far from Earth ({:.1f}), need < {:.1f}",
                     dist, ship_earth_orbit_radius_ * 1.5f);
            }
        }
    }

    void complete_transfer() {
        if (state_ == ShipState::TransferToMoon) {
            Vec3 moon_pos = get_moon_position();
            Vec3 ship_pos = get_ship_position();
            Vec3 rel = ship_pos - moon_pos;
            moon_orbit_angle_ = std::atan2(rel.z, rel.x);
            state_ = ShipState::MoonOrbit;
            INFO(LogEarthMoonShip, "State: TransferToMoon -> MoonOrbit");
        } else {
            earth_orbit_angle_ = transfer_arrival_angle_;
            state_ = ShipState::EarthOrbit;
            INFO(LogEarthMoonShip, "State: TransferToEarth -> EarthOrbit");
        }
    }

    // ====== Ship Motion ======
    void update_ship(float delta_time) {
        auto* ship_trans = get_owner()->get_component<TransformComponent>();
        if (!ship_trans) return;
        
        // Debug: verify we're updating the correct entity
        if (get_owner()->get_name() != "Ship") {
            ERR(LogEarthMoonShip, "update_ship called on wrong entity: '{}'", get_owner()->get_name());
            return;
        }

        Vec3 earth_pos = get_earth_position();

        switch (state_) {
            case ShipState::EarthOrbit: {
                earth_orbit_angle_ += ship_earth_orbit_speed_ * delta_time;
                earth_orbit_angle_  = normalize_angle(earth_orbit_angle_);

                float x = earth_pos.x + ship_earth_orbit_radius_ * std::cos(earth_orbit_angle_);
                float z = earth_pos.z + ship_earth_orbit_radius_ * std::sin(earth_orbit_angle_);
                ship_trans->transform.set_position({x, earth_pos.y, z});

                float tan_angle = earth_orbit_angle_ + PI / 2.0f;
                ship_trans->transform.set_rotation(
                    {0.0f, Math::to_angle(-tan_angle), 0.0f});
                break;
            }

            case ShipState::MoonOrbit: {
                moon_orbit_angle_ += ship_moon_orbit_speed_ * delta_time;
                moon_orbit_angle_  = normalize_angle(moon_orbit_angle_);

                Vec3 moon_pos = get_moon_position();
                float x = moon_pos.x + ship_moon_orbit_radius_ * std::cos(moon_orbit_angle_);
                float z = moon_pos.z + ship_moon_orbit_radius_ * std::sin(moon_orbit_angle_);
                ship_trans->transform.set_position({x, moon_pos.y, z});

                float tan_angle = moon_orbit_angle_ + PI / 2.0f;
                ship_trans->transform.set_rotation(
                    {0.0f, Math::to_angle(-tan_angle), 0.0f});
                break;
            }

            case ShipState::TransferToMoon:
            case ShipState::TransferToEarth: {
                transfer_progress_ += delta_time / transfer_duration_;
                if (transfer_progress_ >= 1.0f) {
                    transfer_progress_ = 1.0f;
                    complete_transfer();
                }

                Vec3 pos = bezier_eval(
                    transfer_p0_, transfer_p1_, transfer_p2_, transfer_p3_,
                    transfer_progress_);
                ship_trans->transform.set_position(pos);

                Vec3 tan = bezier_tangent(
                    transfer_p0_, transfer_p1_, transfer_p2_, transfer_p3_,
                    transfer_progress_);
                if (tan.squared_length() > 0.001f) {
                    tan = tan.normalized();
                    float yaw   = Math::to_angle(std::atan2(tan.x, tan.z));
                    float pitch = Math::to_angle(
                        std::asin(std::clamp(tan.y, -1.0f, 1.0f)));
                    ship_trans->transform.set_rotation(
                        {pitch, yaw, 0.0f});
                }
                break;
            }

            case ShipState::LandedOnEarth: {
                float x = earth_pos.x + earth_surface_offset_ * std::cos(landed_angle_);
                float z = earth_pos.z + earth_surface_offset_ * std::sin(landed_angle_);
                ship_trans->transform.set_position({x, earth_pos.y, z});
                ship_trans->transform.set_rotation(
                    {0.0f, Math::to_angle(landed_angle_), 0.0f});
                break;
            }

            case ShipState::LandedOnMoon: {
                Vec3 moon_pos = get_moon_position();
                float x = moon_pos.x + moon_surface_offset_ * std::cos(landed_angle_);
                float z = moon_pos.z + moon_surface_offset_ * std::sin(landed_angle_);
                ship_trans->transform.set_position({x, moon_pos.y, z});
                ship_trans->transform.set_rotation(
                    {0.0f, Math::to_angle(landed_angle_), 0.0f});
                break;
            }
        }
    }

    // ====== Camera ======
    void update_camera() {
        if (!camera_) return;
        auto* cam_trans = camera_->get_component<TransformComponent>();
        if (!cam_trans) return;

        Vec3 ship_pos = get_ship_position();

        if (camera_mode_ == CameraMode::Free)
            update_free_camera(cam_trans, delta_time_);
        else if (camera_mode_ == CameraMode::FirstPerson)
            update_fp_camera(cam_trans, ship_pos);
        else
            update_tp_camera(cam_trans, ship_pos);
    }

    void update_free_camera(TransformComponent* ct, float dt) {
        const auto& input = Input::get_instance();

        // Build forward/right from yaw+pitch
        float ry = Math::to_radians(free_yaw_);
        float rp = Math::to_radians(free_pitch_);
        Vec3 forward(
            std::cos(rp) * std::sin(ry),
            std::sin(rp),
            std::cos(rp) * std::cos(ry));
        Vec3 right = Vec3(0.0f, 1.0f, 0.0f).cross(forward).normalized();
        Vec3 up(0.0f, 1.0f, 0.0f);

        float speed = free_speed_;
        if (input.is_key_down(Key::LeftShift)) speed *= 3.0f;

        Vec3 move = Vec3::Zero();
        if (input.is_key_down(Key::W)) move = move + forward;
        if (input.is_key_down(Key::S)) move = move - forward;
        if (input.is_key_down(Key::A)) move = move - right;
        if (input.is_key_down(Key::D)) move = move + right;
        if (input.is_key_down(Key::E)) move = move + up;
        if (input.is_key_down(Key::Q)) move = move - up;

        if (move.squared_length() > 0.001f)
            free_pos_ = free_pos_ + move.normalized() * speed * dt;

        // Scroll to adjust speed
        float scroll = input.get_scroll_delta();
        if (std::abs(scroll) > 0.001f) {
            free_speed_ *= (scroll > 0) ? 1.2f : (1.0f / 1.2f);
            free_speed_ = std::clamp(free_speed_, 1.0f, 200.0f);
        }

        ct->transform.set_position(free_pos_);
        ct->transform.set_rotation({free_pitch_, free_yaw_, 0.0f});
    }

    void update_fp_camera(TransformComponent* ct, const Vec3& ship_pos) {
        ct->transform.set_position(ship_pos + Vec3(0.0f, 1.5f, 0.0f));
        ct->transform.set_rotation({fp_pitch_, fp_yaw_, 0.0f});
    }

    void update_tp_camera(TransformComponent* ct, const Vec3& ship_pos) {
        float rp = Math::to_radians(tp_pitch_);
        float ry = Math::to_radians(tp_yaw_);

        Vec3 offset(
            tp_distance_ * std::cos(rp) * std::sin(ry),
            tp_distance_ * std::sin(rp),
            tp_distance_ * std::cos(rp) * std::cos(ry));

        Vec3 cam_pos = ship_pos + offset;
        ct->transform.set_position(cam_pos);

        Vec3 look = ship_pos - cam_pos;
        float len = look.length();
        if (len > 0.001f) {
            look = look / len;
            float yaw   = Math::to_angle(std::atan2(look.x, look.z));
            float pitch = Math::to_angle(
                std::asin(std::clamp(look.y, -1.0f, 1.0f)));
            ct->transform.set_rotation({pitch, yaw, 0.0f});
        }
    }

    // ====== Helpers (using world-space positions for hierarchy support) ======
    Vec3 get_earth_position() const {
        if (earth_)
            if (auto* t = earth_->get_component<TransformComponent>())
                return t->get_world_position();
        return Vec3::Zero();
    }

    Vec3 get_moon_position() const {
        if (moon_) {
            if (auto* t = moon_->get_component<TransformComponent>())
                return t->get_world_position();
            if (auto* o = moon_->get_component<MoonOrbitComponent>())
                return Vec3(o->get_moon_orbit_distance(), 0.0f, 0.0f);
        }
        return Vec3(DEFAULT_MOON_ORBIT_DISTANCE, 0.0f, 0.0f);
    }

    Vec3 get_ship_position() const {
        if (auto* t = get_owner()->get_component<TransformComponent>())
            return t->get_world_position();
        return Vec3::Zero();
    }

    float get_moon_angle() const {
        if (moon_)
            if (auto* o = moon_->get_component<MoonOrbitComponent>())
                return o->get_orbit_angle();
        return 0.0f;
    }

    // ====== Members ======
    Entity* earth_  = nullptr;
    Entity* moon_   = nullptr;
    Entity* camera_ = nullptr;

    ShipState  state_       = ShipState::EarthOrbit;
    CameraMode camera_mode_ = CameraMode::ThirdPerson;

    float earth_orbit_angle_ = 0.0f;
    float moon_orbit_angle_  = 0.0f;

    float transfer_progress_      = 0.0f;
    float transfer_arrival_angle_ = 0.0f;
    Vec3  transfer_p0_, transfer_p1_, transfer_p2_, transfer_p3_;

    float landed_angle_ = 0.0f;

    float tp_yaw_      = 0.0f;
    float tp_pitch_    = 20.0f;
    float tp_distance_ = 25.0f;
    static constexpr float TP_MIN_DIST = 1.0f;
    static constexpr float TP_MAX_DIST = 50.0f;

    float fp_yaw_   = 0.0f;
    float fp_pitch_ = 0.0f;

    // Free camera
    Vec3  free_pos_   = Vec3(25.0f, 10.0f, 25.0f);
    float free_yaw_   = 0.0f;
    float free_pitch_ = 0.0f;
    float free_speed_ = 20.0f;

    float delta_time_  = 0.0f;
    float mouse_sensitivity_ = 0.3f;
    
    // Missed window hint state
    float missed_window_timer_ = 0.0f;
    const char* missed_window_message_ = "";
    
    // Orbit parameters (editable via reflection/Inspector)
    float ship_earth_orbit_radius_;
    float ship_earth_orbit_speed_;
    float ship_moon_orbit_radius_;
    float ship_moon_orbit_speed_;
    float transfer_duration_;
    float earth_surface_offset_;
    float moon_surface_offset_;
};

CEREAL_REGISTER_TYPE(ShipController);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, ShipController);
