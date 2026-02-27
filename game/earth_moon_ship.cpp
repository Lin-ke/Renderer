#include "earth_moon_ship.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include <imgui.h>

DEFINE_LOG_TAG(LogEarthMoonShip, "EarthMoonShip");

REGISTER_CLASS_IMPL(MoonOrbitComponent)
REGISTER_CLASS_IMPL(ShipController)

// ============================================================================
// Utilities
// ============================================================================
static float normalize_angle(float angle) {
    while (angle < 0.0f)       angle += 2.0f * PI;
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    return angle;
}

static Vec3 bezier_eval(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

static Vec3 bezier_tangent(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float u = 1.0f - t;
    return (p1 - p0) * (3.0f * u * u) + (p2 - p1) * (6.0f * u * t) + (p3 - p2) * (3.0f * t * t);
}

const char* ship_state_name(ShipState s) {
    switch (s) {
        case ShipState::LandedOnEarth:      return "LandedOnEarth";
        case ShipState::EarthOrbit:         return "EarthOrbit";
        case ShipState::TransferToMoon:     return "TransferToMoon";
        case ShipState::MoonOrbit:          return "MoonOrbit";
        case ShipState::TransferToEarth:    return "TransferToEarth";
        case ShipState::LandedOnMoon:       return "LandedOnMoon";
        case ShipState::LandingOnEarth:     return "LandingOnEarth";
        case ShipState::LaunchingFromEarth: return "LaunchingFromEarth";
        case ShipState::LandingOnMoon:      return "LandingOnMoon";
        case ShipState::LaunchingFromMoon:  return "LaunchingFromMoon";
        default:                            return "Unknown";
    }
}

// ============================================================================
// MoonOrbitComponent
// ============================================================================
void MoonOrbitComponent::on_init() { orbit_angle_ = 0.0f; }

void MoonOrbitComponent::on_update(float delta_time) {
    orbit_angle_ = normalize_angle(orbit_angle_ + moon_orbit_speed_ * delta_time);
    if (auto* trans = get_owner()->get_component<TransformComponent>()) {
        trans->transform.set_position({moon_orbit_distance_ * std::cos(orbit_angle_), 0.0f, moon_orbit_distance_ * std::sin(orbit_angle_)});
    }
}

void MoonOrbitComponent::register_class() {
    Registry::add<MoonOrbitComponent>("MoonOrbitComponent")
        .member("orbit_angle", &MoonOrbitComponent::orbit_angle_)
        .member("orbit_distance", &MoonOrbitComponent::moon_orbit_distance_)
        .member("orbit_speed", &MoonOrbitComponent::moon_orbit_speed_);
}

// ============================================================================
// ShipController
// ============================================================================
void ShipController::on_init() {
    state_ = ShipState::EarthOrbit;
    if (camera_) {
        if (auto* cam = camera_->get_component<CameraComponent>()) cam->set_external_control(true);
    }
}

// 辅助函数：通过名字从场景中查找实体
static Entity* find_entity_by_name(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    for (const auto& entity : scene->entities_) {
        if (entity && entity->get_name() == name) {
            return entity.get();
        }
    }
    return nullptr;
}

void ShipController::on_update(float delta_time) {
    dt_ = delta_time;
    
    // 延迟查找实体（从文件加载时指针会丢失）
    auto* scene = EngineContext::world() ? EngineContext::world()->get_active_scene() : nullptr;
    if (!earth_) earth_ = find_entity_by_name(scene, "Earth");
    if (!moon_) moon_ = find_entity_by_name(scene, "Moon");
    
    handle_input();
    update_ship(delta_time);
    update_camera();
    if (missed_window_timer_ > 0.0f) missed_window_timer_ -= delta_time;
}

void ShipController::handle_input() {
    const auto& input = Input::get_instance();

    // Toggle camera mode
    if (input.is_key_pressed(Key::V)) {
        camera_mode_ = (CameraMode)(((int)camera_mode_ + 1) % 3);
        if (camera_mode_ == CameraMode::Free && camera_) {
            if (auto* ct = camera_->get_component<TransformComponent>()) {
                free_pos_ = ct->get_world_position();
                Vec3 euler = ct->transform.get_euler_angle();
                free_yaw_ = euler.y; free_pitch_ = euler.x;
            }
        }
    }

    if (input.is_key_pressed(Key::L)) handle_land_launch();
    if (input.is_key_pressed(Key::T)) handle_transfer();

    // Mouse look
    if (input.is_mouse_button_down(MouseButton::Right)) {
        float dx, dy; input.get_mouse_delta(dx, dy);
        float sens = 0.3f;
        if (camera_mode_ == CameraMode::Free) {
            free_yaw_ -= dx * sens; free_pitch_ = std::clamp(free_pitch_ - dy * sens, -89.0f, 89.0f);
        } else if (camera_mode_ == CameraMode::FirstPerson) {
            fp_yaw_ -= dx * sens; fp_pitch_ = std::clamp(fp_pitch_ - dy * sens, -89.0f, 89.0f);
        } else {
            tp_yaw_ -= dx * sens; tp_pitch_ = std::clamp(tp_pitch_ - dy * sens, -70.0f, 80.0f);
        }
    }

    if (camera_mode_ == CameraMode::ThirdPerson) {
        tp_distance_ = std::clamp(tp_distance_ - input.get_scroll_delta() * 2.0f, 1.0f, 50.0f);
    }
}

void ShipController::set_state(ShipState s) {
    INFO(LogEarthMoonShip, "State: {} -> {}", ship_state_name(state_), ship_state_name(s));
    state_ = s;
}

void ShipController::handle_land_launch() {
    if ((int)state_ >= (int)ShipState::LandingOnEarth) return; // Busy

    if (state_ == ShipState::EarthOrbit) {
        start_transition(ShipState::LandingOnEarth, 3.0f);
    } else if (state_ == ShipState::LandedOnEarth) {
        start_transition(ShipState::LaunchingFromEarth, 3.0f);
    } else if (state_ == ShipState::MoonOrbit) {
        start_transition(ShipState::LandingOnMoon, 2.0f);
    } else if (state_ == ShipState::LandedOnMoon) {
        start_transition(ShipState::LaunchingFromMoon, 2.0f);
    }
}

void ShipController::handle_transfer() {
    if (state_ == ShipState::EarthOrbit) {
        float diff = Math::to_angle(normalize_angle(get_moon_angle() - earth_orbit_angle_));
        if (diff > 30.0f && diff < 150.0f) {
            start_transition(ShipState::TransferToMoon, transfer_duration_);
        } else {
            missed_window_timer_ = 3.0f;
            missed_window_message_ = "Transfer window closed! Moon must be 30-150 degrees ahead.";
        }
    } else if (state_ == ShipState::MoonOrbit) {
        Vec3 to_earth = (get_earth_pos() - get_moon_pos()).normalized();
        Vec3 to_ship  = (get_ship_pos()  - get_moon_pos()).normalized();
        if (to_earth.dot(to_ship) > 0.3f) {
            start_transition(ShipState::TransferToEarth, transfer_duration_);
        } else {
            missed_window_timer_ = 3.0f;
            missed_window_message_ = "Transfer window closed! Fly to Earth-facing side of Moon.";
        }
    } else if (state_ == ShipState::TransferToMoon || state_ == ShipState::TransferToEarth) {
        try_early_capture();
    }
}

void ShipController::start_transition(ShipState target_state, float duration) {
    ShipState old_state = state_;
    trans_source_state_ = old_state;  // Record source state for transfer info
    set_state(target_state);
    trans_progress_ = 0.0f;
    trans_duration_ = duration;
    is_bezier_ = false;

    Vec3 ship_pos = get_ship_pos();
    trans_p0_ = ship_pos;

    if (target_state == ShipState::LandingOnEarth || target_state == ShipState::LandingOnMoon) {
        Vec3 center = (target_state == ShipState::LandingOnEarth) ? get_earth_pos() : get_moon_pos();
        float radius = (target_state == ShipState::LandingOnEarth) ? get_earth_radius() : get_moon_radius();
        Vec3 normal = (ship_pos - center).normalized();
        float offset = (target_state == ShipState::LandingOnEarth) ? 2.0f : 2.0f; // Surface offset
        trans_p3_ = center + normal * (radius + offset);
        landed_angle_ = std::atan2(normal.z, normal.x);
    } else if (target_state == ShipState::LaunchingFromEarth || target_state == ShipState::LaunchingFromMoon) {
        Vec3 center = (target_state == ShipState::LaunchingFromEarth) ? get_earth_pos() : get_moon_pos();
        float radius = (target_state == ShipState::LaunchingFromEarth) ? ship_earth_orbit_radius_ : ship_moon_orbit_radius_;
        Vec3 normal = (ship_pos - center).normalized();
        trans_p3_ = center + normal * radius;
    } else if (target_state == ShipState::TransferToMoon) {
        is_bezier_ = true;
        float moon_speed = DEFAULT_MOON_ORBIT_SPEED;
        if (moon_) if (auto* mo = moon_->get_component<MoonOrbitComponent>()) moon_speed = mo->get_moon_orbit_speed();
        
        float predicted_angle = get_moon_angle() + moon_speed * duration;
        float moon_dist = (get_moon_pos() - get_earth_pos()).length();
        Vec3 moon_dist_vec = {moon_dist * std::cos(predicted_angle), 0, moon_dist * std::sin(predicted_angle)};
        Vec3 predicted_moon = get_earth_pos() + moon_dist_vec;
        
        trans_p3_ = predicted_moon + (predicted_moon - get_earth_pos()).normalized() * ship_moon_orbit_radius_;
        Vec3 start_tan = {-std::sin(earth_orbit_angle_), 0, std::cos(earth_orbit_angle_)};
        Vec3 moon_dir = (predicted_moon - get_earth_pos()).normalized();
        Vec3 end_tan = {-moon_dir.z, 0, moon_dir.x};
        float factor = (trans_p3_ - trans_p0_).length() * 0.4f;
        trans_p1_ = trans_p0_ + start_tan * factor;
        trans_p2_ = trans_p3_ - end_tan * factor;
    } else if (target_state == ShipState::TransferToEarth) {
        is_bezier_ = true;
        trans_arrival_angle_ = normalize_angle(std::atan2(ship_pos.z - get_earth_pos().z, ship_pos.x - get_earth_pos().x) - ship_earth_orbit_speed_ * duration * 0.3f);
        trans_p3_ = get_earth_pos() + Vec3(std::cos(trans_arrival_angle_), 0, std::sin(trans_arrival_angle_)) * ship_earth_orbit_radius_;
        Vec3 start_tan = Vec3(-(ship_pos.z-get_moon_pos().z), 0, ship_pos.x-get_moon_pos().x).normalized();
        Vec3 end_tan = {-std::sin(trans_arrival_angle_), 0, std::cos(trans_arrival_angle_)};
        float factor = (trans_p3_ - trans_p0_).length() * 0.4f;
        trans_p1_ = trans_p0_ + start_tan * factor;
        trans_p2_ = trans_p3_ - end_tan * factor;
    }
}

void ShipController::update_ship(float dt) {
    auto* trans = get_owner()->get_component<TransformComponent>();
    if (!trans) return;

    if ((int)state_ >= (int)ShipState::LandingOnEarth || state_ == ShipState::TransferToMoon || state_ == ShipState::TransferToEarth) {
        trans_progress_ += dt / trans_duration_;
        if (trans_progress_ >= 1.0f) {
            trans_progress_ = 1.0f;
            if (state_ == ShipState::LandingOnEarth) set_state(ShipState::LandedOnEarth);
            else if (state_ == ShipState::LandingOnMoon) set_state(ShipState::LandedOnMoon);
            else if (state_ == ShipState::LaunchingFromEarth) { earth_orbit_angle_ = std::atan2(trans->transform.get_position().z - get_earth_pos().z, trans->transform.get_position().x - get_earth_pos().x); set_state(ShipState::EarthOrbit); }
            else if (state_ == ShipState::LaunchingFromMoon) { moon_orbit_angle_ = std::atan2(trans->transform.get_position().z - get_moon_pos().z, trans->transform.get_position().x - get_moon_pos().x); set_state(ShipState::MoonOrbit); }
            else if (state_ == ShipState::TransferToMoon || state_ == ShipState::TransferToEarth) complete_transfer();
            return;
        }

        Vec3 pos, tan;
        if (is_bezier_) {
            pos = bezier_eval(trans_p0_, trans_p1_, trans_p2_, trans_p3_, trans_progress_);
            tan = bezier_tangent(trans_p0_, trans_p1_, trans_p2_, trans_p3_, trans_progress_);
        } else {
            // For land/launch, update target point dynamically to follow moving moon
            Vec3 start_pos = trans_p0_;
            Vec3 end_pos = trans_p3_;
            if (state_ == ShipState::LaunchingFromMoon) {
                // Target orbit point follows current moon position
                Vec3 moon_pos = get_moon_pos();
                Vec3 normal = (trans_p3_ - trans_p0_).normalized(); // Launch direction
                end_pos = moon_pos + normal * ship_moon_orbit_radius_;
            } else if (state_ == ShipState::LandingOnMoon) {
                // Landing target follows current moon surface
                Vec3 moon_pos = get_moon_pos();
                float radius = get_moon_radius();
                // Use stored landed_angle to compute current surface normal
                Vec3 normal = Vec3(std::cos(landed_angle_), 0, std::sin(landed_angle_));
                end_pos = moon_pos + normal * (radius + 2.0f);
            }
            pos = start_pos + (end_pos - start_pos) * trans_progress_;
            tan = (end_pos - start_pos);
        }
        trans->transform.set_position(pos);
        if (tan.squared_length() > 0.001f) {
            tan = tan.normalized();
            trans->transform.set_rotation({Math::to_angle(std::asin(std::clamp(tan.y, -1.0f, 1.0f))), Math::to_angle(std::atan2(tan.x, tan.z)), 0.0f});
        }
    } else if (state_ == ShipState::EarthOrbit) {
        earth_orbit_angle_ = normalize_angle(earth_orbit_angle_ + ship_earth_orbit_speed_ * dt);
        trans->transform.set_position(get_earth_pos() + Vec3(std::cos(earth_orbit_angle_), 0, std::sin(earth_orbit_angle_)) * ship_earth_orbit_radius_);
        trans->transform.set_rotation({0, Math::to_angle(-(earth_orbit_angle_ + PI/2.0f)), 0});
    } else if (state_ == ShipState::MoonOrbit) {
        moon_orbit_angle_ = normalize_angle(moon_orbit_angle_ + ship_moon_orbit_speed_ * dt);
        trans->transform.set_position(get_moon_pos() + Vec3(std::cos(moon_orbit_angle_), 0, std::sin(moon_orbit_angle_)) * ship_moon_orbit_radius_);
        trans->transform.set_rotation({0, Math::to_angle(-(moon_orbit_angle_ + PI/2.0f)), 0});
    } else if (state_ == ShipState::LandedOnEarth || state_ == ShipState::LandedOnMoon) {
        Vec3 center = (state_ == ShipState::LandedOnEarth) ? get_earth_pos() : get_moon_pos();
        float radius = (state_ == ShipState::LandedOnEarth) ? get_earth_radius() : get_moon_radius();
        float offset = (state_ == ShipState::LandedOnEarth) ? 2.0f : 2.0f; // Surface offset
        trans->transform.set_position(center + Vec3(std::cos(landed_angle_), 0, std::sin(landed_angle_)) * (radius + offset));
        trans->transform.set_rotation({0, Math::to_angle(landed_angle_ + PI/2.0f), 0});
    }
}

void ShipController::complete_transfer() {
    if (state_ == ShipState::TransferToMoon) {
        Vec3 rel = get_ship_pos() - get_moon_pos();
        moon_orbit_angle_ = std::atan2(rel.z, rel.x);
        set_state(ShipState::MoonOrbit);
    } else {
        earth_orbit_angle_ = trans_arrival_angle_;
        set_state(ShipState::EarthOrbit);
    }
}

void ShipController::try_early_capture() {
    Vec3 ship_pos = get_ship_pos();
    if (state_ == ShipState::TransferToMoon) {
        float dist = (ship_pos - get_moon_pos()).length();
        if (dist < ship_moon_orbit_radius_ * 2.0f) {
            moon_orbit_angle_ = std::atan2(ship_pos.z - get_moon_pos().z, ship_pos.x - get_moon_pos().x);
            set_state(ShipState::MoonOrbit);
        }
    } else {
        float dist = (ship_pos - get_earth_pos()).length();
        if (dist < ship_earth_orbit_radius_ * 1.5f) {
            earth_orbit_angle_ = std::atan2(ship_pos.z - get_earth_pos().z, ship_pos.x - get_earth_pos().x);
            set_state(ShipState::EarthOrbit);
        }
    }
}

void ShipController::update_camera() {
    // 延迟查找相机（场景加载时 on_init 可能太早，此时相机还没被添加到场景）
    if (!camera_) {
        if (auto* world = EngineContext::world()) {
            if (auto* scene = world->get_active_scene()) {
                if (auto* cam_comp = scene->get_camera()) {
                    camera_ = cam_comp->get_owner();
                    if (auto* cam = camera_->get_component<CameraComponent>()) 
                        cam->set_external_control(true);
                }
            }
        }
    }
    if (!camera_) return;
    auto* ct = camera_->get_component<TransformComponent>();
    if (!ct) return;

    Vec3 ship_pos = get_ship_pos();
    if (camera_mode_ == CameraMode::Free) {
        const auto& input = Input::get_instance();
        float ry = Math::to_radians(free_yaw_), rp = Math::to_radians(free_pitch_);
        Vec3 fwd = {std::cos(rp)*std::sin(ry), std::sin(rp), std::cos(rp)*std::cos(ry)};
        Vec3 right = Vec3(0,1,0).cross(fwd).normalized();
        float speed = free_speed_ * (input.is_key_down(Key::LeftShift) ? 3.0f : 1.0f);
        Vec3 move = Vec3::Zero();
        if (input.is_key_down(Key::W)) move = move + fwd; if (input.is_key_down(Key::S)) move = move - fwd;
        if (input.is_key_down(Key::A)) move = move - right; if (input.is_key_down(Key::D)) move = move + right;
        if (input.is_key_down(Key::E)) move = move + Vec3(0,1,0); if (input.is_key_down(Key::Q)) move = move - Vec3(0,1,0);
        if (move.squared_length() > 0.001f) free_pos_ = free_pos_ + move.normalized() * speed * dt_;
        ct->transform.set_position(free_pos_);
        ct->transform.set_rotation({free_pitch_, free_yaw_, 0});
    } else if (camera_mode_ == CameraMode::FirstPerson) {
        ct->transform.set_position(ship_pos + Vec3(0, 1.5f, 0));
        ct->transform.set_rotation({fp_pitch_, fp_yaw_, 0});
    } else {
        float rp = Math::to_radians(tp_pitch_), ry = Math::to_radians(tp_yaw_);
        Vec3 cam_pos = ship_pos + Vec3(tp_distance_*std::cos(rp)*std::sin(ry), tp_distance_*std::sin(rp), tp_distance_*std::cos(rp)*std::cos(ry));
        ct->transform.set_position(cam_pos);
        Vec3 look = (ship_pos - cam_pos).normalized();
        ct->transform.set_rotation({Math::to_angle(std::asin(std::clamp(look.y, -1.0f, 1.0f))), Math::to_angle(std::atan2(look.x, look.z)), 0});
    }
}

void ShipController::draw_imgui() {
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


Vec3 ShipController::get_earth_pos() const { return earth_ ? earth_->get_component<TransformComponent>()->get_world_position() : Vec3::Zero(); }
Vec3 ShipController::get_moon_pos() const { return moon_ ? moon_->get_component<TransformComponent>()->get_world_position() : Vec3(DEFAULT_MOON_ORBIT_DISTANCE, 0, 0); }
Vec3 ShipController::get_ship_pos() const { return get_owner()->get_component<TransformComponent>()->get_world_position(); }
float ShipController::get_earth_radius() const { return earth_ ? earth_->get_component<TransformComponent>()->get_world_scale().x : 10.0f; }
float ShipController::get_moon_radius() const { return moon_ ? moon_->get_component<TransformComponent>()->get_world_scale().x : 4.0f; }
float ShipController::get_moon_angle() const { return (moon_ && moon_->get_component<MoonOrbitComponent>()) ? moon_->get_component<MoonOrbitComponent>()->get_orbit_angle() : 0.0f; }

bool ShipController::get_transfer_info(const char** source_state, const char** target_state) const {
    // Show transfer info during active transfers (transfer orbit or landing/launching)
    if (state_ == ShipState::TransferToMoon || state_ == ShipState::TransferToEarth) {
        if (source_state) *source_state = ship_state_name(trans_source_state_);
        if (target_state) *target_state = (state_ == ShipState::TransferToMoon) ? "MoonOrbit" : "EarthOrbit";
        return true;
    }
    // Landing/Launching transitions
    if (state_ == ShipState::LandingOnEarth || state_ == ShipState::LandingOnMoon ||
        state_ == ShipState::LaunchingFromEarth || state_ == ShipState::LaunchingFromMoon) {
        if (source_state) *source_state = ship_state_name(trans_source_state_);
        if (target_state) *target_state = ship_state_name(state_);
        return true;
    }
    return false;
}

void ShipController::register_class() {
    Registry::add<ShipController>("ShipController")
        .member("earth_orbit_radius", &ShipController::ship_earth_orbit_radius_)
        .member("earth_orbit_speed", &ShipController::ship_earth_orbit_speed_)
        .member("moon_orbit_radius", &ShipController::ship_moon_orbit_radius_)
        .member("moon_orbit_speed", &ShipController::ship_moon_orbit_speed_)
        .member("transfer_duration", &ShipController::transfer_duration_);
}
