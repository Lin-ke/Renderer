#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/graph/rdg_handle.h"
#include "engine/function/render/render_pass/forward_pass.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include <memory>
#include <vector>
#include <optional>

// Forward declarations
class MeshRendererComponent;
class CameraComponent;
class RDGBuilder;

namespace render {
    class ForwardPass;
    class PBRForwardPass;
    struct DrawBatch;
}

/**
 * @brief Manages mesh rendering for the engine
 * 
 * RenderMeshManager is responsible for:
 * - Managing render passes (ForwardPass, etc.)
 * - Collecting draw batches from MeshRendererComponents in the scene
 * - Building and executing the render graph
 * - Camera management and per-frame data setup
 */
class RenderMeshManager {
public:
    void init();
    void destroy();
    void tick();

    /**
     * @brief Register a mesh renderer component for rendering
     * @param component The component to register
     */
    void register_mesh_renderer(MeshRendererComponent* component);

    /**
     * @brief Unregister a mesh renderer component
     * @param component The component to unregister
     */
    void unregister_mesh_renderer(MeshRendererComponent* component);

    /**
     * @brief Get the forward pass for configuration
     */
    std::shared_ptr<render::ForwardPass> get_forward_pass() { return forward_pass_; }

    /**
     * @brief Get the PBR forward pass for configuration
     */
    std::shared_ptr<render::PBRForwardPass> get_pbr_forward_pass() { return pbr_forward_pass_; }

    /**
     * @brief Set wireframe rendering mode
     * @param enable true for wireframe, false for solid
     */
    void set_wireframe(bool enable);

    /**
     * @brief Enable or disable PBR rendering
     * @param enable true to use PBRForwardPass, false to use ForwardPass
     */
    void set_pbr_enabled(bool enable) { pbr_enabled_ = enable; }
    bool is_pbr_enabled() const { return pbr_enabled_; }

    /**
     * @brief Set the active camera for rendering
     * @param camera The active camera component
     */
    void set_active_camera(CameraComponent* camera) { active_camera_ = camera; }

    /**
     * @brief Get the active camera
     */
    CameraComponent* get_active_camera() const { return active_camera_; }

    /**
     * @brief Collect draw batches for rendering
     * @param batches Output vector to fill with draw batches
     */
    void collect_draw_batches(std::vector<render::DrawBatch>& batches);

    /**
     * @brief Render collected batches to the given command context (legacy direct rendering)
     * @param context The command context to record rendering commands
     * @param back_buffer_view The texture view of the back buffer
     * @param extent The viewport extent
     */
    void render_batches(RHICommandContextRef context, RHITextureViewRef back_buffer_view, Extent2D extent);
    
    /**
     * @brief Build RDG for rendering all collected batches
     * @param builder RDG builder
     * @param color_target Color attachment target handle
     * @param depth_target Depth attachment target handle (optional)
     */
    void build_rdg(RDGBuilder& builder, RDGTextureHandle color_target, 
                   std::optional<RDGTextureHandle> depth_target);

private:
    void prepare_mesh_pass();
    std::vector<render::DrawBatch> current_batches_;

    std::shared_ptr<render::ForwardPass> forward_pass_;
    std::shared_ptr<render::PBRForwardPass> pbr_forward_pass_;
    bool pbr_enabled_ = false;

    std::vector<MeshRendererComponent*> mesh_renderers_;
    CameraComponent* active_camera_ = nullptr;
    
    bool initialized_ = false;
};
