#include "game/scene_builder.h"

#include "engine/core/utils/path_utils.h"
#include "engine/main/engine_context.h"
#include "engine/core/window/window.h"
#include "engine/function/input/input.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/core/log/Log.h"
#include "engine/configs.h"

#include <bitset>
#include <chrono>
#include <thread>
#include <fstream>

DEFINE_LOG_TAG(LogGame, "Game");

// ============================================================================
// Scene Loading (mirrors test_utils::SceneLoader::load)
// ============================================================================
static bool load_and_activate_scene(const std::string& virtual_path) {
    auto* am = EngineContext::asset();
    if (!am) return false;

    auto phys_opt = am->get_physical_path(virtual_path);
    if (!phys_opt) {
        ERR(LogGame, "Cannot resolve path: {}", virtual_path);
        return false;
    }

    std::ifstream check(phys_opt->generic_string());
    if (!check.is_open()) {
        ERR(LogGame, "Scene file does not exist: {}", phys_opt->generic_string());
        return false;
    }
    check.close();

    UID uid = am->get_uid_by_path(phys_opt->generic_string());
    auto scene = am->load_asset<Scene>(uid);
    if (!scene) {
        ERR(LogGame, "Failed to load scene (UID {})", uid.to_string());
        return false;
    }

    auto* camera = scene->get_camera();
    if (!camera) {
        ERR(LogGame, "No camera in scene");
        return false;
    }

    EngineContext::world()->set_active_scene(scene, virtual_path);
    if (auto mesh_mgr = EngineContext::render_system()->get_mesh_manager()) {
        mesh_mgr->set_active_camera(camera);
    }

    INFO(LogGame, "Scene loaded: {} entities, camera OK", scene->entities_.size());
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    INFO(LogGame, "========================================");
    INFO(LogGame, "Earth-Moon-Ship Demo");
    INFO(LogGame, "========================================");

    // 1. Init engine
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);

    // 2. Init asset manager — "game" makes /Game/ resolve to game/assets/
    if (EngineContext::asset()) {
        EngineContext::asset()->init((utils::get_engine_path() / "game").string());
    }

    // 3. Create scene (saves to /Game/earth_moon_ship_scene.asset)
    // if (!create_earth_moon_scene(SCENE_SAVE_PATH)) {
    //     ERR(LogGame, "Failed to create scene, exiting");
    //     EngineContext::exit();
    //     return 1;
    // }

    // 4. Load scene into the world
    if (!load_and_activate_scene(SCENE_SAVE_PATH)) {
        ERR(LogGame, "Failed to load scene, exiting");
        EngineContext::exit();
        return 1;
    }

    // 5. Register custom UI callbacks for ship controls
    auto* active_scene_ptr = EngineContext::world()
        ? EngineContext::world()->get_active_scene()
        : nullptr;
    if (active_scene_ptr && EngineContext::render_system()) {
        for (auto& entity : active_scene_ptr->entities_) {
            if (!entity) continue;
            auto* ctrl = entity->get_component<ShipController>();
            if (ctrl) {
                auto* rs = EngineContext::render_system();
                
                // Callback 1: Ship control tips (always visible)
                rs->add_custom_ui_callback("ship_tips", [ctrl]() {
                    ctrl->draw_imgui();
                });
                
                // Callback 2: Transfer info (shown during active transfer)
                rs->add_custom_ui_callback("transfer_info", [ctrl]() {
                    const char* source_state = nullptr;
                    const char* target_state = nullptr;
                    if (!ctrl->get_transfer_info(&source_state, &target_state)) return;
                    
                    ImGui::SetNextWindowPos(
                        ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 150, 100), 
                        ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.85f);
                    ImGui::Begin("##TransferInfo", nullptr,
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoDecoration |
                        ImGuiWindowFlags_NoFocusOnAppearing);
                    
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "TRANSFER INFO");
                    ImGui::Separator();
                    ImGui::Text("Transferring:");
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s -> %s", source_state, target_state);
                    
                    ImGui::End();
                });
                
                // Callback 3: Missed window hint (shown when transfer fails)
                rs->add_custom_ui_callback("missed_window_hint", [ctrl]() {
                    if (!ctrl->should_show_missed_window_hint()) return;
                    
                    ImGui::SetNextWindowPos(
                        ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 150, 180), 
                        ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.85f);
                    ImGui::Begin("##MissedWindow", nullptr,
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoDecoration |
                        ImGuiWindowFlags_NoFocusOnAppearing);
                    
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "TRANSFER WINDOW CLOSED");
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", ctrl->get_missed_window_message());
                    
                    ImGui::End();
                });
                
                INFO(LogGame, "Registered ship UI callbacks");
                break;
            }
        }
    }

    // 6. Main loop
    INFO(LogGame, "Entering main loop...");

    auto last_time = std::chrono::steady_clock::now();
    uint32_t frame_count = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        float delta_time = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        // Process messages
        auto* window = EngineContext::window();
        if (window && !window->process_messages()) {
            INFO(LogGame, "Window closed");
            break;
        }

        // Tick input
        Input::get_instance().tick();
        
        // Handle Ctrl+S to save scene
        if (Input::get_instance().is_key_down(Key::LeftControl) && 
            Input::get_instance().is_key_pressed(Key::S)) {
            if (EngineContext::world()) {
                EngineContext::world()->save_active_scene();
            }
        }

        // Tick world (components: MoonOrbitComponent, ShipController, CameraComponent)
        if (EngineContext::world()) {
            EngineContext::world()->tick(delta_time);
        }

        // Prepare render packet
        RenderPacket packet;
        packet.frame_index = frame_count % FRAMES_IN_FLIGHT;

        Scene* active_scene = EngineContext::world()
                                  ? EngineContext::world()->get_active_scene()
                                  : nullptr;
        packet.active_scene = active_scene;

        if (active_scene) {
            packet.active_camera = active_scene->get_camera();
        }

        // Render
        if (EngineContext::render_system()) {
            if (!EngineContext::render_system()->tick(packet)) {
                INFO(LogGame, "RenderSystem returned false, exiting");
                break;
            }
        }

        frame_count++;
    }

    INFO(LogGame, "========================================");
    INFO(LogGame, "Rendered {} frames, exiting", frame_count);
    INFO(LogGame, "========================================");

    EngineContext::exit();
    return 0;
}
