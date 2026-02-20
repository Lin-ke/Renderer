#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/reflect_inspector.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/core/log/Log.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"

#include <imgui.h>
#include <ImGuizmo.h>

//####TODO####: Render Passes
// #include "engine/function/render/render_pass/gpu_culling_pass.h"
// ...

DECLARE_LOG_TAG(LogRenderSystem);
DEFINE_LOG_TAG(LogRenderSystem, "RenderSystem");

// Helper to get entity type icon based on components
static std::string get_entity_icon(Entity* entity) {
    if (!entity) return "?";
    if (entity->get_component<DirectionalLightComponent>()) return "[D]"; // Directional light
    if (entity->get_component<PointLightComponent>()) return "[P]";       // Point light
    if (entity->get_component<MeshRendererComponent>()) return "[M]";     // Mesh
    if (entity->get_component<CameraComponent>()) return "[C]";           // Camera
    return "[E]"; // Generic Entity
}

// Helper to get entity display name
static std::string get_entity_name(Entity* entity) {
    if (!entity) return "Unknown";
    
    if (entity->get_component<DirectionalLightComponent>()) return "Directional Light";
    if (entity->get_component<PointLightComponent>()) return "Point Light";
    if (entity->get_component<MeshRendererComponent>()) return "Mesh";
    if (entity->get_component<CameraComponent>()) return "Camera";
    return "Entity";
}

// Note: GLFW has been removed. Window creation is now handled by the Window class.
// RenderSystem receives a native window handle (HWND) via init().

void RenderSystem::init(void* window_handle) {
    INFO(LogRenderSystem, "RenderSystem Initialized");

    // Store native window handle (HWND)
    native_window_handle_ = window_handle;
    
    if (!native_window_handle_) {
        ERR(LogRenderSystem, "Window handle is null!");
        return;
    }

    // Initialize RHI backend first so it's available for managers
    init_base_resource();
    
    // Initialize ImGui with native window handle (Win32)
    if (backend_ && native_window_handle_) {
        backend_->init_imgui(native_window_handle_);
    }

    light_manager_ = std::make_shared<RenderLightManager>();
    mesh_manager_ = std::make_shared<RenderMeshManager>();
    gizmo_manager_ = std::make_shared<GizmoManager>();
    
    light_manager_->init();
    mesh_manager_->init();
    gizmo_manager_->init();

    init_passes();
}

void RenderSystem::init_base_resource() {
    RHIBackendInfo info = {};
    info.type = BACKEND_DX11;
    info.enable_debug = true;
    backend_ = RHIBackend::init(info); 
    
    if (!backend_) {
        WARN(LogRenderSystem, "RHI Backend not initialized!");
        return;
    }

    surface_ = backend_->create_surface(native_window_handle_);
    queue_ = backend_->get_queue({ QUEUE_TYPE_GRAPHICS, 0 });
    
    RHISwapchainInfo sw_info = {};
    sw_info.surface = surface_;
    sw_info.present_queue = queue_;
    sw_info.image_count = FRAMES_IN_FLIGHT;
    sw_info.extent = WINDOW_EXTENT;
    sw_info.format = COLOR_FORMAT;
    
    swapchain_ = backend_->create_swapchain(sw_info);
    
    // Create depth buffer
    RHITextureInfo depth_info = {};
    depth_info.format = DEPTH_FORMAT;
    depth_info.extent = { WINDOW_EXTENT.width, WINDOW_EXTENT.height, 1 };
    depth_info.mip_levels = 1;
    depth_info.array_layers = 1;
    depth_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    depth_info.type = RESOURCE_TYPE_DEPTH_STENCIL;
    
    depth_texture_ = backend_->create_texture(depth_info);
    
    RHITextureViewInfo depth_view_info = {};
    depth_view_info.texture = depth_texture_;
    depth_view_info.format = DEPTH_FORMAT;
    depth_view_info.view_type = VIEW_TYPE_2D;
    depth_view_info.subresource.aspect = TEXTURE_ASPECT_DEPTH;
    depth_view_info.subresource.level_count = 1;
    depth_view_info.subresource.layer_count = 1;
    
    depth_texture_view_ = backend_->create_texture_view(depth_view_info);
    
    pool_ = backend_->create_command_pool({ queue_ });

    for(uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        per_frame_common_resources_[i].command = backend_->create_command_context(pool_);
        per_frame_common_resources_[i].start_semaphore = backend_->create_semaphore();
        per_frame_common_resources_[i].finish_semaphore = backend_->create_semaphore();
        per_frame_common_resources_[i].fence = backend_->create_fence(true);
    }
}

void RenderSystem::init_passes() {
    // passes_[MESH_DEPTH_PASS] = std::make_shared<DepthPass>();
    // ...
    // for(auto& pass : passes_) { if(pass) pass->init(); }
}

bool RenderSystem::tick(const RenderPacket& packet) {
    // ENGINE_TIME_SCOPE(RenderSystem::Tick);
    
    // Note: Window close check is now handled by Window::process_messages() in EngineContext

    // Use frame_index from packet for multi-threaded safety
    uint32_t frame_index = packet.frame_index;
    
    // Update active camera in mesh manager if provided
    if (packet.active_camera) {
        mesh_manager_->set_active_camera(packet.active_camera);
    }
    
    // Tick managers (collect render data)
    mesh_manager_->tick();
    light_manager_->tick(frame_index);
    update_global_setting();

    // Execute rendering using frame_index from packet for thread safety
    auto& resource = per_frame_common_resources_[frame_index];
    resource.fence->wait();
    
    RHITextureRef swapchain_texture = swapchain_->get_new_frame(nullptr, resource.start_semaphore);
    RHICommandContextRef command = resource.command;
    
    Extent2D extent = swapchain_->get_extent();
    
    command->begin_command();
    {
        // Create render pass that loads clear and stores
        RHIRenderPassInfo rp_info = {};
        rp_info.extent = extent;
        
        // Create texture view for back buffer
        RHITextureViewInfo view_info = {};
        view_info.texture = swapchain_texture;
        RHITextureViewRef back_buffer_view = backend_->create_texture_view(view_info);
        
        rp_info.color_attachments[0].texture_view = back_buffer_view;
        rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
        rp_info.color_attachments[0].clear_color = {0.1f, 0.2f, 0.4f, 1.0f};  // Dark blue background
        rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;
        
        // Depth attachment
        rp_info.depth_stencil_attachment.texture_view = depth_texture_view_;
        rp_info.depth_stencil_attachment.load_op = ATTACHMENT_LOAD_OP_CLEAR;
        rp_info.depth_stencil_attachment.clear_depth = 1.0f;
        rp_info.depth_stencil_attachment.clear_stencil = 0;
        rp_info.depth_stencil_attachment.store_op = ATTACHMENT_STORE_OP_STORE;
        
        RHIRenderPassRef render_pass = backend_->create_render_pass(rp_info);
        command->begin_render_pass(render_pass);
        
        // Render all mesh batches (bunny, etc.)
        mesh_manager_->render_batches(command, back_buffer_view, extent);
        
        //####TODO####: Other render passes
        // for(auto& pass : passes_) { if(pass) pass->build(...); }
        
        command->end_render_pass();
        
        // Cleanup render pass resources before UI
        render_pass->destroy();
        back_buffer_view->destroy();
    }
    command->end_command();
    
    // Render ImGui and Gizmo
    // Note: This happens outside the main render pass
    if (backend_) {
        backend_->imgui_new_frame();
        
        // Begin ImGuizmo frame (must be called after ImGui::NewFrame)
        ImGuizmo::BeginFrame();
        
        // Build UI windows FIRST (so they get input priority)
        if (show_ui_) {
            // Scene Hierarchy Window
            draw_scene_hierarchy(packet.active_scene);
            
            // Inspector Window for selected entity
            draw_inspector_panel();
            
            // Buffer Debug Window
            draw_buffer_debug();
            
            ImGui::Begin("Renderer Debug", &show_ui_);
            
            // Wireframe toggle
            if (ImGui::Checkbox("Wireframe", &wireframe_mode_)) {
                if (mesh_manager_) {
                    mesh_manager_->set_wireframe(wireframe_mode_);
                }
            }
            
            // Gizmo controls
            if (gizmo_manager_) {
                gizmo_manager_->draw_controls();
            }
            
            // Frame info
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                        1000.0f / ImGui::GetIO().Framerate, 
                        ImGui::GetIO().Framerate);
            
            ImGui::End();
        }
        
        // Draw gizmo LAST (on top of everything)
        // Use a stable fullscreen window for gizmo rendering
        if (gizmo_manager_ && selected_entity_ && packet.active_camera) {
            ImGuiIO& io = ImGui::GetIO();
            
            // Create a transparent window for gizmo (between hierarchy and inspector)
            float hierarchy_width = 250.0f;
            float inspector_width = 300.0f;
            float gizmo_x = hierarchy_width;
            float gizmo_width = io.DisplaySize.x - hierarchy_width - inspector_width;
            
            ImGui::SetNextWindowPos(ImVec2(gizmo_x, 0));
            ImGui::SetNextWindowSize(ImVec2(gizmo_width, io.DisplaySize.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            
            ImGui::Begin("GizmoViewport", nullptr, 
                ImGuiWindowFlags_NoTitleBar | 
                ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoInputs);
            
            // Draw main transform gizmo
            ImVec2 window_pos = ImGui::GetWindowPos();
            ImVec2 window_size = ImGui::GetWindowSize();
            gizmo_manager_->draw_gizmo(packet.active_camera, selected_entity_, 
                                       window_pos, window_size);
            
            // Draw light gizmo for selected entity if it's a light
            if (packet.active_camera) {
                draw_light_gizmo(packet.active_camera, selected_entity_, 
                                Extent2D{static_cast<uint32_t>(io.DisplaySize.x), 
                                        static_cast<uint32_t>(io.DisplaySize.y)});
            }
            
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        
        // Render ImGui
        backend_->imgui_render();
    }
    
    command->execute(resource.fence, resource.start_semaphore, resource.finish_semaphore);
    swapchain_->present(resource.finish_semaphore);
    
    return true;
}

void RenderSystem::destroy() {
    // Shutdown ImGui
    if (backend_) {
        backend_->imgui_shutdown();
    }
    
    // Cleanup managers first (they may hold RHI resources)
    if (gizmo_manager_) {
        gizmo_manager_->shutdown();
        gizmo_manager_.reset();
    }
    if (mesh_manager_) {
        mesh_manager_->destroy();
        mesh_manager_.reset();
    }
    if (light_manager_) {
        light_manager_->destroy();
        light_manager_.reset();
    }
    
    // Clear per-frame resources
    for (auto& resource : per_frame_common_resources_) {
        resource.command.reset();
        resource.start_semaphore.reset();
        resource.finish_semaphore.reset();
        resource.fence.reset();
    }
    
    // Cleanup RHI resources in reverse order of creation
    // Important: swapchain must be destroyed before pool/surface
    pool_.reset();
    swapchain_.reset();
    queue_.reset();
    surface_.reset();
    
    // Destroy backend last (after all RHI resources are released)
    if (backend_) {
        backend_->destroy();
        backend_.reset();
        // Important: Reset the static backend instance so next init() creates a fresh one
        RHIBackend::reset_backend();
    }
}

void RenderSystem::update_global_setting() {
    /* //####TODO####
    global_setting_.totalTicks = EngineContext::get_current_tick();
    global_setting_.totalTickTime += EngineContext::get_delta_time();
    // ...
    EngineContext::render_resource()->set_render_global_setting(global_setting_);
    */
}

// render_ui_begin is now integrated into tick()
void RenderSystem::render_ui_begin() {
    // UI building is done directly in tick() after imgui_new_frame()
}

// render_ui is now integrated into tick()
void RenderSystem::render_ui(RHICommandContextRef command) {
    (void)command;
}

// Draw Scene Hierarchy panel
void RenderSystem::draw_scene_hierarchy(Scene* scene) {
    ImGuiIO& io = ImGui::GetIO();
    float hierarchy_width = 250.0f;
    
    // Left side, fill top to bottom
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(hierarchy_width, io.DisplaySize.y));
    
    ImGui::Begin("Scene Hierarchy", nullptr, 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoResize);
    
    if (scene) {
        for (const auto& entity : scene->entities_) {
            if (entity) {
                draw_entity_node(entity.get());
            }
        }
    }
    
    ImGui::End();
}

// Recursively draw entity tree node
void RenderSystem::draw_entity_node(Entity* entity) {
    if (!entity) return;
    
    // Get entity display info
    std::string icon = get_entity_icon(entity);
    std::string name = get_entity_name(entity);
    std::string label = icon + " " + name + "##" + std::to_string(reinterpret_cast<uintptr_t>(entity));
    
    // Check if this entity is selected
    bool is_selected = (selected_entity_ == entity);
    
    // Tree node flags
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (is_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    
    // Check if entity has children (for now, assume no children hierarchy)
    bool has_children = false;  // Simplified for now
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    
    bool node_open = ImGui::TreeNodeEx(label.c_str(), flags);
    
    // Handle selection click
    if (ImGui::IsItemClicked()) {
        selected_entity_ = entity;
        INFO(LogRenderSystem, "Selected entity: {}", name);
    }
    
    if (node_open) {
        // Recursively draw children (when hierarchy is implemented)
        // for (auto& child : entity->get_children()) { draw_entity_node(child); }
        ImGui::TreePop();
    }
}

// Draw Inspector panel for selected entity
void RenderSystem::draw_inspector_panel() {
    ImGuiIO& io = ImGui::GetIO();
    float inspector_width = 300.0f;
    
    // Right side, fill top to bottom
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - inspector_width, 0));
    ImGui::SetNextWindowSize(ImVec2(inspector_width, io.DisplaySize.y));
    
    ImGui::Begin("Inspector", nullptr, 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoResize);
    
    if (selected_entity_) {
        // Entity header
        std::string icon = get_entity_icon(selected_entity_);
        std::string name = get_entity_name(selected_entity_);
        ImGui::Text("%s %s", icon.c_str(), name.c_str());
        ImGui::Separator();
        
        // Draw all components using reflection
        auto& inspector = ReflectInspector::get();
        
        for (const auto& comp_ptr : selected_entity_->get_components()) {
            if (!comp_ptr) continue;
            
            Component* comp = comp_ptr.get();
            std::string class_name(comp->get_component_type_name());
            
            // Use CollapsingHeader for each component
            if (ImGui::CollapsingHeader(class_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                inspector.draw_component(comp);
            }
        }
    } else {
        ImGui::Text("No entity selected");
        ImGui::Text("Click an entity in Scene Hierarchy to inspect");
    }
    
    ImGui::End();
}

// Draw Buffer Debug panel
void RenderSystem::draw_buffer_debug() {
    if (!show_ui_) return;

    ImGui::Begin("Buffer Debug");

    if (ImGui::CollapsingHeader("Render Targets", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Depth Buffer
        if (depth_texture_view_) {
            ImGui::Text("Depth Buffer");
            void* tex_id = depth_texture_view_->raw_handle();
            if (tex_id) {
                // Calculate aspect ratio for display
                float width = 320.0f;
                float height = width * (float)WINDOW_EXTENT.height / (float)WINDOW_EXTENT.width;
                // Note: Depth buffer visualization in ImGui might need a special shader 
                // to make it visible (currently it might look all white or black depending on values)
                ImGui::Image(tex_id, ImVec2(width, height));
            } else {
                ImGui::Text("Depth SRV not available");
            }
        }
    }

    ImGui::End();
}

// Draw light gizmo visualization
void RenderSystem::draw_light_gizmo(CameraComponent* camera, Entity* entity, const Extent2D& extent) {
    if (!camera || !entity) return;
    
    auto* transform = entity->get_component<TransformComponent>();
    if (!transform) return;
    
    Vec3 position = transform->transform.get_position();
    
    // Project 3D position to screen space
    Mat4 view = camera->get_view_matrix();
    Mat4 proj = camera->get_projection_matrix();
    Mat4 view_proj = proj * view;
    
    Vec4 pos_clip = view_proj * Vec4(position.x(), position.y(), position.z(), 1.0f);
    if (pos_clip.w() <= 0) return; // Behind camera
    
    Vec3 pos_ndc = pos_clip.head<3>() / pos_clip.w();
    ImVec2 screen_pos;
    screen_pos.x = (pos_ndc.x() * 0.5f + 0.5f) * extent.width;
    screen_pos.y = (1.0f - (pos_ndc.y() * 0.5f + 0.5f)) * extent.height; // Flip Y for screen coords
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Draw Directional Light gizmo (sun icon)
    if (entity->get_component<DirectionalLightComponent>()) {
        ImU32 color = IM_COL32(255, 255, 0, 255); // Yellow
        float radius = 15.0f;
        
        // Draw circle with rays
        draw_list->AddCircle(screen_pos, radius, color, 16, 2.0f);
        // Draw rays
        for (int i = 0; i < 8; ++i) {
            float angle = i * 3.14159f / 4.0f;
            ImVec2 inner(screen_pos.x + cosf(angle) * (radius + 2), 
                        screen_pos.y + sinf(angle) * (radius + 2));
            ImVec2 outer(screen_pos.x + cosf(angle) * (radius + 10), 
                        screen_pos.y + sinf(angle) * (radius + 10));
            draw_list->AddLine(inner, outer, color, 2.0f);
        }
        // Label
        draw_list->AddText(ImVec2(screen_pos.x + 20, screen_pos.y - 8), color, "[D]");
    }
    
    // Draw Point Light gizmo (bulb icon)
    if (auto* point_light = entity->get_component<PointLightComponent>()) {
        Vec3 color_vec = point_light->get_color();
        ImU32 color = IM_COL32(
            static_cast<int>(color_vec.x() * 255),
            static_cast<int>(color_vec.y() * 255),
            static_cast<int>(color_vec.z() * 255),
            255
        );
        float radius = 12.0f;
        
        // Draw filled circle (bulb)
        draw_list->AddCircleFilled(screen_pos, radius, color);
        draw_list->AddCircle(screen_pos, radius, IM_COL32(255, 255, 255, 255), 16, 2.0f);
        
        // Draw range indicator (wire circle)
        float range = point_light->get_bounding_sphere().radius;
        // Approximate screen radius based on distance
        float dist = (camera->get_position() - position).norm();
        float screen_radius = (range / dist) * extent.height * 0.5f;
        if (screen_radius > 5 && screen_radius < 200) {
            draw_list->AddCircle(screen_pos, screen_radius, color, 32, 1.0f);
        }
        // Label
        draw_list->AddText(ImVec2(screen_pos.x + 15, screen_pos.y - 8), color, "[P]");
    }
}
