#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "samples/sun_temple/scene.h"
#include "engine/core/window/window.h"
#include "engine/function/framework/world.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system/render_system.h"

#include <chrono>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Usage: xmake run sun_temple <source-directory> [--reimport] [--capture <png-path>] [--frames N] [--verbose]");
        return 1;
    }
    std::filesystem::path source_dir = argv[1];
    bool reimport = false;
    bool verbose = false;
    std::string capture_path;
    int max_frames = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reimport") reimport = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--capture" && i + 1 < argc) capture_path = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else { std::printf("Unknown/incomplete argument: %s\n", argv[i]); return 1; }
    }
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    if (!capture_path.empty()) mode.set(EngineContext::StartMode::HiddenWindow);
    EngineContext::init(mode);
    if (!verbose) Log::set_min_log_level(1);
    auto* assets = EngineContext::asset();
    assets->init((utils::get_engine_path() / "game").string());
    auto* renderer = EngineContext::render_system();
    renderer->set_show_ui(false);

    int result = 0;
    {
        ModelRef model;
        std::shared_ptr<Scene> scene;
        auto physical_path = assets->get_physical_path(sun_temple::model_path);
        if (!reimport && physical_path && std::filesystem::exists(*physical_path)) {
            model = assets->load_asset<Model>(sun_temple::model_path);
        } else {
            if (std::filesystem::is_regular_file(source_dir / "SunTemple.fbx")) {
                std::printf("Importing Sun Temple from %s\n", source_dir.string().c_str());
                model = sun_temple::import_model(source_dir);
            }
            if (model) assets->save_asset(model, sun_temple::model_path);
        }
        auto scene_physical_path = assets->get_physical_path(sun_temple::scene_path);
        if (!reimport && scene_physical_path && std::filesystem::exists(*scene_physical_path)) {
            scene = assets->load_asset<Scene>(sun_temple::scene_path);
        } else {
            scene = sun_temple::create_scene(model, source_dir);
            if (scene) assets->save_asset(scene, sun_temple::scene_path);
        }
        if (!scene) {
            result = 1;
        } else {
            std::printf("Sun Temple ready: %u meshes, %llu vertices. WASD: move; RMB: look; Space/Ctrl: up/down; Ctrl+S: save.\n",
                model ? model->get_slot_count() : 0, static_cast<unsigned long long>(model ? model->get_total_vertex_count() : 0));
            EngineContext::world()->set_active_scene(scene, sun_temple::scene_path);
            renderer->get_mesh_manager()->set_active_camera(scene->get_camera());
            auto last = std::chrono::steady_clock::now();
            int frames = 0;
            bool captured = false;
            while (EngineContext::window()->process_messages()) {
                const auto now = std::chrono::steady_clock::now();
                const float delta_time = std::chrono::duration<float>(now - last).count();
                last = now;
                Input::get_instance().tick();
                if (Input::get_instance().is_key_down(Key::LeftControl) && Input::get_instance().is_key_pressed(Key::S)) {
                    EngineContext::world()->save_active_scene();
                }
                EngineContext::world()->tick(delta_time);
                RenderPacket packet;
                packet.frame_index = frames % FRAMES_IN_FLIGHT;
                packet.active_scene = scene.get();
                packet.active_camera = scene->get_camera();
                if (!renderer->tick(packet)) { result = 1; break; }
                ++frames;
                if (!capture_path.empty() && frames == 3) {
                    std::vector<uint8_t> pixels;
                    Extent2D extent;
                    if (renderer->capture_frame(pixels, extent)) {
                        auto parent = std::filesystem::path(capture_path).parent_path();
                        if (!parent.empty()) std::filesystem::create_directories(parent);
                        captured = stbi_write_png(capture_path.c_str(), extent.width, extent.height, 4,
                            pixels.data(), extent.width * 4) != 0;
                    }
                    if (!captured) result = 1;
                    std::printf("Sun Temple screenshot %s: %s\n", captured ? "saved" : "failed", capture_path.c_str());
                    if (max_frames == 0) break;
                }
                if (max_frames > 0 && frames >= max_frames) break;
            }
            if (!capture_path.empty() && !captured) result = 1;
            EngineContext::world()->set_active_scene(nullptr);
            renderer->cleanup_for_test();
        }
    }
    EngineContext::exit();
    return result;
}
