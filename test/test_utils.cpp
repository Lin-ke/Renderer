#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "test_utils.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/input/input.h"
#include "engine/core/window/window.h"
#include "engine/core/utils/profiler.h"
#include "engine/core/utils/cpu_profiler.h"
#include "engine/core/utils/path_utils.h"
#include <fstream>
#include <chrono>
#include <iomanip>

namespace test_utils {

// TestContext static members
bool TestContext::engine_initialized_ = false;
std::filesystem::path TestContext::project_dir_;

bool TestContext::init_engine() {
    if (engine_initialized_) return true;

    const auto work_dir = utils::get_engine_path() / "build/test-work";
    std::error_code error;
    std::filesystem::create_directories(work_dir, error);
    if (error) {
        std::cerr << "Cannot create test workspace: " << error.message() << '\n';
        return false;
    }
    project_dir_ = work_dir / UID::generate().to_string();
    if (!std::filesystem::create_directory(project_dir_, error)) {
        std::cerr << "Cannot create test project " << project_dir_ << ' ' << error.message() << '\n';
        return false;
    }

    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::HiddenWindow);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    if (auto* render_system = EngineContext::render_system()) render_system->set_show_ui(false);
    
    if (auto am = EngineContext::asset()) {
        am->init(project_dir_);
    }
    
    engine_initialized_ = true;
    return true;
}

void TestContext::shutdown_engine() {
    if (!engine_initialized_) return;
    
    EngineContext::exit();
    engine_initialized_ = false;

    std::error_code error;
    const auto workspace = std::filesystem::canonical(utils::get_engine_path(), error);
    if (error) return;
    const auto project = std::filesystem::canonical(project_dir_, error);
    if (!error && project.parent_path() == workspace / "build/test-work" &&
        project.filename() == project_dir_.filename()) {
        std::filesystem::remove_all(project, error);
    }
    if (error) std::cerr << "Cannot clean test project " << project_dir_ << ' ' << error.message() << '\n';
    project_dir_.clear();
}

void TestContext::reset() {
    if (!engine_initialized_) return;
    
    // 1. Clear active scene first
    if (auto world = EngineContext::world()) {
        world->set_active_scene(nullptr);
    }
    
    // 2. Wait for GPU and cleanup render system state
    if (auto rs = EngineContext::render_system()) {
        rs->cleanup_for_test();
    }
    
    if (auto am = EngineContext::asset()) {
        am->tick();
    }
}

std::shared_ptr<Model> import_test_model(const std::string& source_path, const std::string& asset_path,
    const ModelProcessSetting& settings) {
    const auto source = EngineContext::asset()->get_physical_path(source_path);
    if (!source) return nullptr;
    ModelImporter importer;
    return importer.import_model(source->string(), asset_path, settings);
}

SceneLoadResult SceneLoader::load(const std::string& virtual_path, 
                                   bool set_active) {
    SceneLoadResult result;
    
    auto am = EngineContext::asset();
    if (!am) {
        result.error_msg = "AssetManager is null";
        return result;
    }
    
    // Resolve virtual path to physical path
    auto phys_path_opt = am->get_physical_path(virtual_path);
    if (!phys_path_opt) {
        result.error_msg = "Failed to resolve physical path for: " + virtual_path;
        return result;
    }
    
    std::string phys_path = phys_path_opt->generic_string();
    
    // Check if file exists
    std::ifstream check_file(phys_path);
    if (!check_file.is_open()) {
        result.error_msg = "Scene file does not exist: " + phys_path;
        return result;
    }
    check_file.close();
    
    // Get UID from registered path
    UID scene_uid = am->get_uid_by_path(phys_path);
    
    // Load scene
    auto scene = am->load_asset<Scene>(scene_uid);
    if (!scene) {
        result.error_msg = "Failed to load scene from UID: " + scene_uid.to_string();
        return result;
    }
    
    // Find camera
    CameraComponent* camera = scene->get_camera();
    if (!camera) {
        result.error_msg = "No camera found in loaded scene";
        return result;
    }
    
    // Set as active scene if requested
    if (set_active) {
        EngineContext::world()->set_active_scene(scene, virtual_path);
        if (auto mesh_manager = EngineContext::render_system()->get_mesh_manager()) {
            mesh_manager->set_active_camera(camera);
        }
    }
    
    result.scene = scene;
    result.camera = camera;
    result.success = true;
    return result;
}

bool export_chrome_tracing(const std::string& filename,
                           const std::vector<const CpuProfileFrame*>& frames) {
    const auto parent = std::filesystem::path(filename).parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }
    std::ofstream ofs(filename);
    if (!ofs.is_open()) return false;
    
    ofs << "{\"traceEvents\":[";
    bool first = true;
    
    for (const auto* frame : frames) {
        if (!frame) continue;
        uint64_t freq = frame->cpu_frequency;
        if (freq == 0) continue;
        
        for (const auto& td : frame->threads) {
            for (const auto& scope : td.scopes) {
                // Chrome tracing uses microseconds
                double start_us = static_cast<double>(scope.start_ticks) * 1000000.0 / static_cast<double>(freq);
                double dur_us = static_cast<double>(scope.end_ticks - scope.start_ticks) * 1000000.0 / static_cast<double>(freq);
                
                if (!first) ofs << ",";
                first = false;
                
                ofs << "\n{\"name\":\"" << scope.name << "\""
                    << ",\"cat\":\"function\""
                    << ",\"ph\":\"X\""
                    << ",\"ts\":" << std::fixed << std::setprecision(1) << start_us
                    << ",\"dur\":" << dur_us
                    << ",\"pid\":0"
                    << ",\"tid\":" << td.thread_id
                    << "}";
            }
        }
    }
    
    ofs << "\n],\"displayTimeUnit\":\"ms\"}";
    return true;
}

bool export_profiler_trace(const std::string& filename) {
    auto& profiler = CpuProfiler::instance();
    size_t count = profiler.get_history_size();
    std::vector<const CpuProfileFrame*> frames;
    frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (auto* f = profiler.get_history_frame(i)) {
            frames.push_back(f);
        }
    }
    return export_chrome_tracing(filename, frames);
}

bool RenderTestApp::run(const Config& config, std::vector<uint8_t>& out_screenshot_data, int* out_frames) {
    if (out_frames) *out_frames = 0;
    out_screenshot_data.clear();
    // Enable profiler and start a frame for asset loading phase
    auto& profiler = CpuProfiler::instance();
    profiler.set_enabled(true);
    profiler.begin_frame();

    // 1. Create Scene
    if (config.create_scene_func) {
        PROFILE_SCOPE("CreateScene");
        if (!config.create_scene_func(config.scene_path)) {
            profiler.end_frame();
            return false;
        }
    }

    // 2. Load Scene
    SceneLoadResult result;
    {
        PROFILE_SCOPE("LoadScene");
        result = SceneLoader::load(config.scene_path, true);
    }
    if (!result.success) {
        std::cerr << "Render test scene load failed: " << result.error_msg << '\n';
        profiler.end_frame();
        return false;
    }

    // End asset loading frame — all asset timing is captured here
    profiler.end_frame();

    // Export profiler trace for analysis
    std::string trace_path = (utils::get_engine_path() / "build/test-results/profile_trace.json").string();
    export_profiler_trace(trace_path);

    // 3. Set up custom RDG build function if provided
    if (config.build_rdg_func) {
        EngineContext::render_system()->set_custom_rdg_build_func(config.build_rdg_func);
    }

    // 4. Render
    int frames = 0;
    bool screenshot_taken = false;
    
    auto* window = EngineContext::window();
    
    while (frames < config.max_frames) {
        // Process window messages for input events
        if (window && !window->process_messages()) {
            break;
        }
        
        Input::get_instance().tick();
        EngineContext::world()->tick(0.016f);
        
        auto packet = EngineContext::render_system()->make_render_packet(
            result.scene.get(), result.camera, frames);
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) break;
        
        frames++;
        
        if (config.capture_frame > 0 && frames == config.capture_frame && !screenshot_taken) {
            Extent2D extent;
            if (EngineContext::render_system()->capture_frame(out_screenshot_data, extent)) {
                auto path = config.screenshot_path.empty() ?
                    (utils::get_engine_path() / "build/test-results" / (std::filesystem::path(config.scene_path).stem().string() + ".png")).string() : config.screenshot_path;
                screenshot_taken = save_screenshot_png(path, extent.width, extent.height, out_screenshot_data);
            }
        }
        
    }
    
    // Clean up custom RDG build function
    if (config.build_rdg_func) {
        EngineContext::render_system()->clear_custom_rdg_build_func();
    }

    if (out_frames) {
        *out_frames = frames;
    }
    return frames == config.max_frames && (config.capture_frame == 0 || screenshot_taken);
}

} // namespace test_utils
