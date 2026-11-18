#pragma once

#include <array>
#include <span>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

#include <stb_image_write.h>

// Forward declarations
class Scene;
class Model;
struct ModelProcessSetting;
class CameraComponent;
struct RenderPacket;
class RDGBuilder;
struct CpuProfileFrame;

namespace test_utils {

// expected(x, y) returns RGBA. Compare every pixel, report one functional result.
template<class Expected>
bool image_matches(std::span<const uint8_t> pixels, uint32_t width, Expected expected, int tolerance = 0) {
    if (!width || pixels.empty() || pixels.size() % (size_t(width) * 4)) return false;
    for (size_t index = 0; index < pixels.size() / 4; ++index) {
        const auto color = expected(uint32_t(index % width), uint32_t(index / width));
        for (size_t channel = 0; channel < 4; ++channel) {
            const int delta = int(pixels[index * 4 + channel]) - int(color[channel]);
            if (delta < -tolerance || delta > tolerance) return false;
        }
    }
    return true;
}

/**
 * @brief Export CpuProfiler frames to Chrome Tracing JSON format.
 * Open the output file in chrome://tracing or https://ui.perfetto.dev
 * @param filename Output file path
 * @param frames The frames to export
 * @return true on success
 */
bool export_chrome_tracing(const std::string& filename,
                           const std::vector<const CpuProfileFrame*>& frames);

/**
 * @brief Export all profiler history frames to Chrome Tracing JSON.
 * Convenience wrapper that pulls frames from CpuProfiler::instance().
 */
bool export_profiler_trace(const std::string& filename);

/**
 * @brief Save screenshot data to PNG file
 * @param filename Output file path
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param data Raw RGBA pixel data
 * @return true on success
 */
inline bool save_screenshot_png(const std::string& filename, uint32_t width, uint32_t height, const std::vector<uint8_t>& data) {
    if (!width || !height || uint64_t(width) * height * 4 != data.size()) return false;
    std::error_code error;
    auto parent = std::filesystem::path(filename).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) return false;
    int stride_in_bytes = width * 4;
    int result = stbi_write_png(filename.c_str(), static_cast<int>(width), static_cast<int>(height), 4, data.data(), stride_in_bytes);
    return result != 0;
}

/**
 * @brief Calculate average brightness of image
 * @param data Raw RGBA pixel data
 * @return Average brightness (0-255), or 0 if data is empty
 */
inline float calculate_average_brightness(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0.0f;
    
    uint64_t total = 0;
    size_t pixel_count = data.size() / 4;
    
    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t r = data[i * 4 + 0];
        uint32_t g = data[i * 4 + 1];
        uint32_t b = data[i * 4 + 2];
        total += (r + g + b) / 3;
    }
    
    return static_cast<float>(total) / static_cast<float>(pixel_count);
}

std::shared_ptr<Model> import_test_model(const std::string& source_path, const std::string& asset_path,
    const ModelProcessSetting& settings);

/**
 * @brief Result of scene loading operation
 */
struct SceneLoadResult {
    std::shared_ptr<Scene> scene;
    CameraComponent* camera = nullptr;
    bool success = false;
    std::string error_msg;
    
    bool is_valid() const { return success && scene && camera; }
};

/**
 * @brief Utility class for loading test scenes with proper initialization
 * 
 * Handles the common pattern of:
 * 1. Resolving virtual path to physical path
 * 2. Checking file existence
 * 3. Looking up UID from path
 * 4. Loading scene with dependencies
 * 5. Initializing components
 * 6. Finding camera and setting up render
 */
class SceneLoader {
public:
    /**
     * @brief Load a scene from virtual path with full initialization
     * @param virtual_path Virtual path like "/Game/scene.asset"
     * @param set_active Whether to set as active scene in World
     * @return SceneLoadResult with scene, camera, and status
     */
    static SceneLoadResult load(const std::string& virtual_path, 
                                 bool set_active = true);
    
};

/**
 * @brief Utility for render tests handling scene creation and rendering
 */
class RenderTestApp {
public:
    using SceneCreateFunc = std::function<bool(const std::string&)>;
    using BuildRDGFunc = std::function<void(class RDGBuilder&, const RenderPacket&)>;

    struct Config {
        std::string scene_path;
        int max_frames = 60;
        int capture_frame = 30; // 0 to disable
        std::string screenshot_path; // Empty: build/test-results/<scene-name>.png
        SceneCreateFunc create_scene_func = nullptr;
        BuildRDGFunc build_rdg_func = nullptr;  // Custom RDG building function
    };

    static bool run(const Config& config, std::vector<uint8_t>& out_screenshot_data, int* out_frames = nullptr);
};

/**
 * @brief Global test context for managing Engine lifecycle across tests
 * 
 * This allows tests to share a single Engine instance, significantly
 * reducing test execution time by avoiding repeated init/exit cycles.
 */
class TestContext {
public:
    /**
     * @brief Initialize the global Engine (called once before all tests)
     */
    static bool init_engine();
    
    /**
     * @brief Shutdown the global Engine (called once after all tests)
     */
    static void shutdown_engine();
    
    /**
     * @brief Reset test state between tests without restarting Engine
     * 
     * This clears:
     * - Active scene
     * - Mesh renderer registrations
     * - Render batches
     * - Other per-test runtime state
     */
    static void reset();
    
    /**
     * @brief Per-run directory mounted as the test project
     */
    static const std::filesystem::path& project_dir() { return project_dir_; }

private:
    static bool engine_initialized_;
    static std::filesystem::path project_dir_;
};

} // namespace test_utils
