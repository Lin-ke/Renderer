#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "samples/sun_temple/scene.h"
#include "engine/core/window/window.h"
#include "engine/function/framework/world.h"
#include "engine/function/input/input.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/gpu_profiler.h"
#include "engine/core/utils/cpu_profiler.h"

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
constexpr std::array benchmark_scopes = {
    "RenderSystem_Managers", "RenderSystem_SceneRender", "RenderSystem_Submit",
    "RenderSystem_Present", "RenderSystem_ResourceTick"
};

struct Benchmark {
    std::vector<float> frame_times;
    std::array<double, benchmark_scopes.size()> cpu_totals{};
    double gpu_total = 0;
    size_t visible_batches = 0;

    void record(const CpuProfileFrame& frame, RenderSystem::FrameStats stats) {
        frame_times.push_back(frame.duration_ms);
        visible_batches += stats.visible_batches;
        for (const auto& thread : frame.threads) {
            for (const auto& scope : thread.scopes) {
                for (size_t i = 0; i < benchmark_scopes.size(); ++i) {
                    if (std::string_view(scope.name) == benchmark_scopes[i]) {
                        cpu_totals[i] += scope.duration_ms(frame.cpu_frequency);
                    }
                }
            }
        }
        gpu_total += stats.gpu_ms;
    }

    void print() {
        if (frame_times.empty()) return;
        std::sort(frame_times.begin(), frame_times.end());
        const size_t count = frame_times.size();
        const double mean = std::accumulate(frame_times.begin(), frame_times.end(), 0.0) / count;
        const size_t p95 = count - count / 20 - 1;
        std::printf("Benchmark (%zu frames): %.1f FPS, mean %.2f ms, p95 %.2f ms\n",
            count, 1000.0 / mean, mean, frame_times[p95]);
        std::printf("Mean ms: managers %.2f, recording %.2f, submit %.2f, present %.2f, resource tick %.2f, GPU passes %.2f\n",
            cpu_totals[0] / count, cpu_totals[1] / count, cpu_totals[2] / count,
            cpu_totals[3] / count, cpu_totals[4] / count, gpu_total / count);
        std::printf("Mean visible mesh batches: %.1f\n", static_cast<double>(visible_batches) / count);
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Usage: xmake run sun_temple <source-directory> [--reimport] [--capture <png-path>] [--frames N] [--benchmark] [--verbose]");
        return 1;
    }
    std::filesystem::path source_dir = argv[1];
    bool reimport = false;
    bool verbose = false;
    bool benchmark_enabled = false;
    std::string capture_path;
    int max_frames = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reimport") reimport = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--benchmark") benchmark_enabled = true;
        else if (arg == "--capture" && i + 1 < argc) capture_path = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else { std::printf("Unknown/incomplete argument: %s\n", argv[i]); return 1; }
    }
    if (benchmark_enabled && max_frames == 0) max_frames = benchmark_warmup + 300;
    if (benchmark_enabled && max_frames <= benchmark_warmup) {
        std::printf("Benchmark needs more than %d frames for warmup.\n", benchmark_warmup);
        return 1;
    }
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    if (!capture_path.empty()) mode.set(EngineContext::StartMode::HiddenWindow);
    EngineContext::init(mode);
    if (!verbose) Log::set_min_log_level(1);
    auto* assets = EngineContext::asset();
    assets->init((utils::get_engine_path() / "game").string());
    auto* renderer = EngineContext::render_system();
    renderer->set_show_ui(false);

    int result = 0;
    {
        std::shared_ptr<Scene> scene;
        auto scene_physical_path = assets->get_physical_path(sun_temple::scene_path);
        if (!reimport && scene_physical_path && std::filesystem::exists(*scene_physical_path)) {
            scene = assets->load_asset<Scene>(sun_temple::scene_path);
        } else if (std::filesystem::is_regular_file(source_dir / "SunTemple.fbx")) {
            std::printf("Importing Sun Temple from %s\n", source_dir.string().c_str());
            scene = sun_temple::import_scene(source_dir);
            if (scene) assets->save_asset(scene, sun_temple::scene_path);
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
            Benchmark benchmark;
            auto& cpu_profiler = CpuProfiler::instance();
            while (true) {
                if (benchmark_enabled) cpu_profiler.begin_frame();
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
                if (benchmark_enabled) {
                    cpu_profiler.end_frame();
                    if (frames > benchmark_warmup) {
                        benchmark.record(cpu_profiler.get_display_frame(), renderer->get_frame_stats());
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
            if (benchmark_enabled) benchmark.print();
            if (!capture_path.empty() && !captured) result = 1;
            if (!EngineContext::wait_for_render()) result = 1;
            EngineContext::world()->set_active_scene(nullptr);
        }
    }
    EngineContext::exit();
    return result;
}
