#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "samples/sun_temple/scene.h"
#include "engine/core/window/window.h"
#include "engine/function/framework/world.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/gpu_profiler.h"
#include "engine/core/utils/cpu_profiler.h"
#include "engine/core/utils/profile_probe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int benchmark_warmup = 30;
void print_benchmark(const std::vector<RenderSystem::FrameStats>& completed) {
    std::vector<float> gpu_times, device_times;
    uint64_t first = 0, last = 0;
    for (const auto& frame : completed) {
        if (frame.frame_id < benchmark_warmup) continue;
        gpu_times.push_back(frame.gpu_ms); device_times.push_back(frame.device_ms);
        if (!first) first = frame.completed_ticks;
        last = frame.completed_ticks;
    }
    if (gpu_times.empty()) return;
    auto summarize = [](std::vector<float>& times) {
        std::sort(times.begin(), times.end());
        const auto count = times.size();
        const auto index = (count * 95 + 99) / 100 - 1;
        return std::array<double, 2>{std::accumulate(times.begin(), times.end(), 0.0) / count, times[index]};
    };
    const auto gpu = summarize(gpu_times), device = summarize(device_times);
    const double seconds = double(last - first) / CpuProfiler::get_cpu_frequency();
    std::printf("Benchmark (%zu completed frames): %.1f FPS; Device wall mean %.2f ms, p95 %.2f ms; GPU mean %.2f ms, p95 %.2f ms\n",
        gpu_times.size(), seconds > 0 ? (gpu_times.size() - 1) / seconds : 0,
        device[0], device[1], gpu[0], gpu[1]);
}

void sample_memory(const char* name) {
    EngineContext::run_device_task([name] {
        ProfileSession::instance().sample_memory(name, EngineContext::rhi()->get_memory_snapshot());
    });
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Usage: xmake run sun_temple <source-directory> [--reimport] [--capture <png-path>] [--frames N] [--benchmark] [--profile] [--profile-probes] [--profile-output <json-path>] [--bloom] [--verbose]");
        return 1;
    }
    std::filesystem::path source_dir = argv[1];
    bool reimport = false;
    bool verbose = false;
    bool benchmark_enabled = false;
    bool profile_enabled = false;
    bool profile_probes = false;
    std::filesystem::path profile_path = utils::get_engine_path() / "build/test-results/sun-temple-profile.json";
    bool bloom_enabled = false;
    std::string capture_path;
    int max_frames = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reimport") reimport = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--benchmark") benchmark_enabled = true;
        else if (arg == "--profile") profile_enabled = true;
        else if (arg == "--profile-probes") { profile_enabled = true; profile_probes = true; }
        else if (arg == "--profile-output" && i + 1 < argc) { profile_enabled = true; profile_path = argv[++i]; }
        else if (arg == "--bloom") bloom_enabled = true;
        else if (arg == "--capture" && i + 1 < argc) capture_path = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else { std::printf("Unknown/incomplete argument: %s\n", argv[i]); return 1; }
    }
    if (max_frames < 0) { std::puts("Frame count must be positive"); return 1; }
    profile_enabled |= benchmark_enabled;
    if (profile_enabled && max_frames == 0) max_frames = profile_probes ? 40 : benchmark_warmup + 300;
    if (benchmark_enabled && max_frames <= benchmark_warmup) {
        std::printf("Benchmark needs more than %d frames for warmup.\n", benchmark_warmup);
        return 1;
    }
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    if (!capture_path.empty()) mode.set(EngineContext::StartMode::HiddenWindow);
    auto& profile = ProfileSession::instance();
    if (profile_enabled && !profile.begin("SunTemple")) return 1;
    if (profile_enabled) {
        profile.set_metadata("source", source_dir.string());
        profile.set_metadata("load_mode", reimport ? "import" : "native asset if available");
        profile.set_metadata("bloom", bloom_enabled ? "enabled" : "disabled");
        profile.set_metadata("resolution", "1280x720");
        profile.set_metadata("threading", "Game / Render / Device");
#ifdef _DEBUG_
        profile.set_metadata("build", "debug");
#else
        profile.set_metadata("build", "release");
#endif
    }
    ProfileProbe::set_enabled(profile_probes);
    if (profile_enabled) profile.set_metadata("profile_probes", profile_probes ? "frame 30" : "disabled");
    if (profile_enabled) profile.set_metadata("pipeline_statistics", "all captured frames");
    const auto engine_init = profile.begin_operation("EngineInit");
    EngineContext::init(mode);
    profile.end_operation(engine_init);
    if (profile_enabled) profile.set_metadata("rhi_debug", EngineContext::rhi()->get_backend_info().enable_debug ? "enabled" : "disabled");
    if (!verbose) Log::set_min_log_level(1);
    auto* assets = EngineContext::asset();
    assets->init((utils::get_engine_path() / "game").string());
    auto* renderer = EngineContext::render_system();
    renderer->set_show_ui(false);
    renderer->bloom_settings_.enabled = bloom_enabled;

    if (profile_enabled) sample_memory("EngineReady");
    int result = 0;
    {
        const auto loading = profile.begin_operation("LoadScene");
        auto first_frame = loading; first_frame.name = "FirstFrameReady";
        std::shared_ptr<Scene> scene;
        {
        ProfileContextScope loading_scope(loading.context);
        auto scene_physical_path = assets->get_physical_path(sun_temple::scene_path);
        if (!reimport && scene_physical_path && std::filesystem::exists(*scene_physical_path)) {
            profile.set_metadata("load_mode", "native asset");
            scene = assets->load_asset<Scene>(sun_temple::scene_path);
        } else if (std::filesystem::is_regular_file(source_dir / "SunTemple.fbx")) {
            std::printf("Importing Sun Temple from %s\n", source_dir.string().c_str());
            profile.set_metadata("load_mode", "import");
            scene = sun_temple::import_scene(source_dir);
            if (scene) assets->save_asset(scene, sun_temple::scene_path);
        }
        profile.end_operation(loading);
        if (profile_enabled) {
            EngineContext::run_device_task([] { EngineContext::rhi()->get_immediate_command()->flush(); });
            sample_memory("SceneLoaded");
        }
        }
        if (!scene) {
            result = 1;
        } else {
            const auto renderers = scene->get_components<MeshRendererComponent>();
            size_t mesh_count = 0;
            for (auto* component : renderers) mesh_count += component->get_submesh_count();
            std::printf("Sun Temple ready: %zu mesh instances in %zu entities. WASD: move; RMB: look; Space/Ctrl: up/down; Ctrl+S: save.\n",
                mesh_count, renderers.size());
            EngineContext::world()->set_active_scene(scene, sun_temple::scene_path);
            auto* camera = scene->get_camera();
            if (camera) camera->set_input_smoothing(0.06f);
            renderer->get_mesh_manager()->set_active_camera(camera);
            auto last = std::chrono::steady_clock::now();
            int frames = 0;
            bool captured = false;
            renderer->take_profile_frame_stats();
            auto& cpu_profiler = CpuProfiler::instance();
            while (true) {
                if (profile_enabled) cpu_profiler.begin_frame();
                if (!EngineContext::window()->process_messages()) break;
                const auto now = std::chrono::steady_clock::now();
                const float delta_time = std::chrono::duration<float>(now - last).count();
                last = now;
                Input::get_instance().tick();
                if (Input::get_instance().is_key_down(Key::LeftControl) && Input::get_instance().is_key_pressed(Key::S)) {
                    EngineContext::world()->save_active_scene();
                }
                EngineContext::world()->tick(delta_time);
                if (!EngineContext::render_frame()) { result = 1; break; }
                ++frames;
                if (profile_enabled) {
                    cpu_profiler.end_frame();
                    if (frames == 1) {
                        if (!EngineContext::wait_for_render()) { result = 1; break; }
                        profile.end_operation(first_frame);
                        sample_memory("FirstFrameReady");
                    }
                }
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
            if (!EngineContext::wait_for_render()) result = 1;
            if (profile_enabled) sample_memory("SteadyState");
            if (benchmark_enabled) print_benchmark(renderer->take_profile_frame_stats());
            EngineContext::world()->set_active_scene(nullptr);
        }
    }
    EngineContext::exit();
    if (profile_enabled) {
        profile.sample_memory("EngineStopped");
        profile.end();
        profile.print();
        if (!profile.write(profile_path)) { std::puts("Failed to write profile report"); result = 1; }
        else std::printf("Profile saved (JSON + offline HTML): %s\n", profile_path.string().c_str());
    }
    return result;
}
