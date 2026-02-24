#pragma once

#include "engine/function/render/rhi/rhi.h"
#include "engine/core/dependency_graph/dependency_graph.h"
// #include "engine/function/render/render_pass/render_pass.h" //####TODO####

#include "engine/function/render/render_system/render_light_manager.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_system/gizmo_manager.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/depth_pre_pass.h"
#include "engine/function/render/render_pass/depth_visualize_pass.h"
// #include "engine/function/render/render_system/render_surface_cache_manager.h"
#include <imgui.h>

// Forward declarations
class RDGBuilder;
#include <array>
#include <memory>

//####TODO####: Move to config
static const Extent2D WINDOW_EXTENT = { 1280, 720 };
static const RHIFormat HDR_COLOR_FORMAT = FORMAT_R16G16B16A16_SFLOAT;
static const RHIFormat COLOR_FORMAT = FORMAT_R8G8B8A8_UNORM;
static const RHIFormat DEPTH_FORMAT = FORMAT_D32_SFLOAT;

/**
 * @brief RenderPacket contains all data needed for one frame rendering
 * 
 * This is passed from Game Thread to Render Thread (in multi-threaded mode)
 * or used directly (in single-threaded mode)
 */
struct RenderPacket {
    uint32_t frame_index = 0;       // Frame index this packet belongs to
    float delta_time = 0.0f;        // Time since last frame in seconds
    
    // Scene data
    class Scene* active_scene = nullptr;     // Scene to render
    class CameraComponent* active_camera = nullptr;  // Camera for this frame
    
    // Rendering config
    bool enable_forward_pass = true;
    bool enable_post_process = false;
    
    // Debug options
    bool wireframe = false;
    bool visualize_lights = false;
};

struct DefaultRenderResource{
    RHITextureRef fallback_white_texture_;
    RHITextureRef fallback_black_texture_;
    RHITextureRef fallback_normal_texture_;
    void init(RHIBackendRef backend);
};

struct RDGDebugInfo{

};


class RenderSystem {
public:
    void init(void* window_handle);
    void destroy();

    bool tick(const RenderPacket& packet);

    //####TODO####: RenderPass accessors
    // const std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT>& get_passes() { return passes_; }
    
    void* get_window_handle() { return native_window_handle_; }

    // Fallback resources
    RHITextureRef get_fallback_white_texture() { 
        return fallback_resources_.fallback_white_texture_;
    }
    RHITextureRef get_fallback_black_texture() { 
        return fallback_resources_.fallback_black_texture_; 
    }
    RHITextureRef get_fallback_normal_texture() { 
        return fallback_resources_.fallback_normal_texture_; 
    }

    RHIFormat get_hdr_color_format() { return HDR_COLOR_FORMAT; }
    RHIFormat get_color_format() { return COLOR_FORMAT; }
    RHIFormat get_depth_format() { return DEPTH_FORMAT; }
    RHISwapchainRef get_swapchain() { return swapchain_; }
    RHIBackendRef get_rhi() { return backend_; }

    std::shared_ptr<RenderMeshManager> get_mesh_manager() { return mesh_manager_; }
    std::shared_ptr<RenderLightManager> get_light_manager() { return light_manager_; }
    std::shared_ptr<GizmoManager> get_gizmo_manager() { return gizmo_manager_; }
    RHITextureViewRef get_depth_texture_view() { return depth_texture_view_; }
    RHITextureRef get_depth_texture() { return depth_texture_; }
    RHITextureRef get_prepass_depth_texture() { return depth_texture_; }

    /**
     * @brief Cleanup runtime state for testing (keeps system initialized)
     * 
     * This clears per-test rendering state without destroying the render system,
     * allowing fast test reset.
     */
    void cleanup_for_test();

    // UI Methods
    void render_ui_begin();
    void render_ui(RHICommandContextRef command);
    
private:
    // RDG methods
    void build_and_execute_rdg(uint32_t frame_index, const RenderPacket& packet);
    void build_fallback_rdg(RDGBuilder& builder);
    
    // RDG Visualization
    void draw_rdg_visualizer();
    void capture_rdg_info(class RDGBuilder& builder);
    
    void render_imgui_and_gizmo(const RenderPacket& packet, const Extent2D& extent);

public:
    // Entity selection for gizmo
    void set_selected_entity(Entity* entity) { selected_entity_ = entity; }
    Entity* get_selected_entity() const { return selected_entity_; }
    
    // Debug settings
    bool wireframe_mode_ = false;
    bool show_ui_ = true;
    bool show_buffer_debug_ = true;   // Toggle for buffer debug visualization (default ON for debugging)

    // DependencyGraphRef get_rdg_dependency_graph() { return rdg_dependency_graph_; } //####TODO####

private:

    void create_fallback_resources();

    void* native_window_handle_ = nullptr;
    DefaultRenderResource fallback_resources_ = {};

    RHIBackendRef backend_;
    RHISurfaceRef surface_;
    RHIQueueRef queue_;
    RHISwapchainRef swapchain_;
    RHITextureRef depth_texture_;
    RHITextureViewRef depth_texture_view_;

    
public:
    RHICommandPoolRef pool_;
    
    // Cached swapchain back buffer views - one per frame in flight
    std::array<RHITextureViewRef, FRAMES_IN_FLIGHT> swapchain_buffer_views_;
    std::array<RHIRenderPassRef, FRAMES_IN_FLIGHT> swapchain_render_passes_;
    bool swapchain_views_initialized_ = false;

    struct PerFrameCommonResource {
        RHICommandContextRef command; // Changed from RHICommandListRef to match my RHI
        RHISemaphoreRef start_semaphore;
        RHISemaphoreRef finish_semaphore;
        RHIFenceRef fence;
    };
    std::array<PerFrameCommonResource, FRAMES_IN_FLIGHT> per_frame_common_resources_;
    
    // RenderGlobalSetting global_setting_ = {}; //####TODO####
    // DependencyGraphRef rdg_dependency_graph_;

    // std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT> passes_; //####TODO####

    void init_passes();
    void init_base_resource();
    void update_global_setting();
    
    // UI Helpers
    void draw_scene_hierarchy(class Scene* scene);
    void draw_entity_node(class Entity* entity);
    void draw_inspector_panel();
    void draw_buffer_debug();
    void draw_light_gizmo(class CameraComponent* camera, class Entity* entity, const Extent2D& extent);

    std::shared_ptr<RenderMeshManager> mesh_manager_;
    std::shared_ptr<RenderLightManager> light_manager_;
    std::shared_ptr<GizmoManager> gizmo_manager_;
    std::shared_ptr<render::ForwardPass> forward_pass_;
    std::shared_ptr<render::DepthPrePass> depth_prepass_;
    std::shared_ptr<render::DepthVisualizePass> depth_visualize_pass_;
    
    // Depth buffer visualization
    RHITextureRef depth_visualize_texture_;
    
    // RDG Visualization data
    struct RDGNodeInfo {
        std::string name;
        std::string type;
        uint32_t id;
        bool is_pass;
        float x, y;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
    };
    struct RDGEdgeInfo {
        uint32_t from_id;
        uint32_t to_id;
        std::string label;
    };
    std::vector<RDGNodeInfo> last_rdg_nodes_;
    std::vector<RDGEdgeInfo> last_rdg_edges_;
    std::mutex rdg_info_mutex_;
    bool show_rdg_visualizer_ = true;
    bool rdg_graph_layout_dirty_ = true;
    ImVec2 rdg_graph_offset_ = ImVec2(50, 50);
    float rdg_graph_scale_ = 1.0f;
    RHITextureViewRef depth_visualize_texture_view_;
    bool depth_visualize_initialized_ = false;

    Entity* selected_entity_ = nullptr;
};
