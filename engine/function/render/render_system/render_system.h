#pragma once

#include "engine/function/render/rhi/rhi.h"
#include "engine/core/dependency_graph/dependency_graph.h"
// #include "engine/function/render/render_pass/render_pass.h" //####TODO####

#include "engine/function/render/render_system/render_light_manager.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
// #include "engine/function/render/render_system/render_surface_cache_manager.h"

#include <array>
#include <memory>

//####TODO####: Move to config
static const Extent2D WINDOW_EXTENT = { 1280, 720 };
static const RHIFormat HDR_COLOR_FORMAT = FORMAT_R16G16B16A16_SFLOAT;
static const RHIFormat COLOR_FORMAT = FORMAT_R8G8B8A8_UNORM;
static const RHIFormat DEPTH_FORMAT = FORMAT_D32_SFLOAT;

struct RenderPacket {
    // float delta_time;
    // std::vector<RenderCommand> commands;
};

class RenderSystem {
public:
    void init(void* window_handle); // Match EngineContext
    void destroy();
    void init_glfw();
    void destroy_glfw();

    bool tick(const RenderPacket& packet); // Match EngineContext

    //####TODO####: RenderPass accessors
    // const std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT>& get_passes() { return passes_; }
    
    struct GLFWwindow* get_window() { return window_; }
    RHIFormat get_hdr_color_format() { return HDR_COLOR_FORMAT; }
    RHIFormat get_color_format() { return COLOR_FORMAT; }
    RHIFormat get_depth_format() { return DEPTH_FORMAT; }
    RHISwapchainRef get_swapchain() { return swapchain_; }
    RHIBackendRef get_rhi() { return backend_; }

    std::shared_ptr<RenderMeshManager> get_mesh_manager() { return mesh_manager_; }
    std::shared_ptr<RenderLightManager> get_light_manager() { return light_manager_; }

    // DependencyGraphRef get_rdg_dependency_graph() { return rdg_dependency_graph_; } //####TODO####

private:
    struct GLFWwindow* window_;

    RHIBackendRef backend_;
    RHISurfaceRef surface_;
    RHIQueueRef queue_;
    RHISwapchainRef swapchain_;
    RHICommandPoolRef pool_;

    struct PerFrameCommonResource {
        RHICommandContextRef command; // Changed from RHICommandListRef to match my RHI
        RHISemaphoreRef start_semaphore;
        RHISemaphoreRef finish_semaphore;
        RHIFenceRef fence;
    };
    static const int FRAMES_IN_FLIGHT = 2; // Match test
    std::array<PerFrameCommonResource, FRAMES_IN_FLIGHT> per_frame_common_resources_;
    
    // RenderGlobalSetting global_setting_ = {}; //####TODO####
    // DependencyGraphRef rdg_dependency_graph_;

    // std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT> passes_; //####TODO####

    void init_passes();
    void init_base_resource();
    void update_global_setting();

    std::shared_ptr<RenderMeshManager> mesh_manager_;
    std::shared_ptr<RenderLightManager> light_manager_;
};
