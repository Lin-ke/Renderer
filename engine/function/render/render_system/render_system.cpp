#include "engine/function/render/render_system/render_system.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/profiler.h"
#include "engine/core/utils/profiler_widget.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/world.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/graph/rdg_edge.h"
#include "engine/function/render/graph/rdg_node.h"
#include "engine/function/render/render_system/reflect_inspector.h"
#include "engine/function/render/render_system/gpu_profiler_widget.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/main/engine_context.h"


#include <algorithm>
#include <mutex>
#include <unordered_map>


#include <ImGuizmo.h>
#include <imgui.h>


DECLARE_LOG_TAG(LogRenderSystem);
DEFINE_LOG_TAG(LogRenderSystem, "RenderSystem");

static std::string get_entity_icon(Entity *entity) {
	if (!entity) {
		return "?";
	}
	if (entity->get_component<SkyboxComponent>()) {
		return "[S]"; // Skybox
	}
	if (entity->get_component<DirectionalLightComponent>()) {
		return "[D]"; // Directional light
	}
	if (entity->get_component<PointLightComponent>()) {
		return "[P]"; // Point light
	}
	if (entity->get_component<MeshRendererComponent>()) {
		return "[M]"; // Mesh
	}
	if (entity->get_component<CameraComponent>()) {
		return "[C]"; // Camera
	}
	return "[E]"; // Generic Entity
}

static std::string get_entity_name(Entity *entity) {
	if (!entity) {
		return "Unknown";
	}

	// Use entity name if set
	const std::string& entity_name = entity->get_name();
	if (!entity_name.empty()) {
		return entity_name;
	}

	// Fallback to component type name
	if (entity->get_component<SkyboxComponent>()) {
		return "Skybox";
	}
	if (entity->get_component<DirectionalLightComponent>()) {
		return "Directional Light";
	}
	if (entity->get_component<PointLightComponent>()) {
		return "Point Light";
	}
	if (entity->get_component<MeshRendererComponent>()) {
		return "Mesh";
	}
	if (entity->get_component<CameraComponent>()) {
		return "Camera";
	}
	return "Entity";
}

void RenderSystem::init(void *window_handle) {
	INFO(LogRenderSystem, "RenderSystem Initialized");

	native_window_handle_ = window_handle;

	if (!native_window_handle_) {
		ERR(LogRenderSystem, "Window handle is null!");
		return;
	}

	init_base_resource();
	create_fallback_resources();

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

	// Create cached texture views and render passes for swapchain back buffers
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		RHITextureRef back_buffer = swapchain_->get_texture(i);
		if (back_buffer) {
			RHITextureViewInfo view_info = {};
			view_info.texture = back_buffer;
			swapchain_buffer_views_[i] = backend_->create_texture_view(view_info);

			// Create a persistent render pass for this back buffer
			RHIRenderPassInfo rp_info = {};
			rp_info.extent = swapchain_->get_extent();
			rp_info.color_attachments[0].texture_view = swapchain_buffer_views_[i];
			rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
			rp_info.color_attachments[0].clear_color = { 0.1f, 0.2f, 0.4f, 1.0f };
			rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;

			// Note: depth_texture_view_ is common for all frames
			// But we need to set it here if we want a static render pass
			// For now, let's just initialize the passes later when depth is ready
		}
	}
	swapchain_views_initialized_ = true;

	RHITextureInfo depth_info = {};
	depth_info.format = DEPTH_FORMAT;
	depth_info.extent = { WINDOW_EXTENT.width, WINDOW_EXTENT.height, 1 };
	depth_info.mip_levels = 1;
	depth_info.array_layers = 1;
	depth_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
	depth_info.type = RESOURCE_TYPE_DEPTH_STENCIL | RESOURCE_TYPE_TEXTURE;

	depth_texture_ = backend_->create_texture(depth_info);

	RHITextureViewInfo depth_view_info = {};
	depth_view_info.texture = depth_texture_;
	depth_view_info.format = DEPTH_FORMAT;
	depth_view_info.view_type = VIEW_TYPE_2D;
	depth_view_info.subresource.aspect = TEXTURE_ASPECT_DEPTH;
	depth_view_info.subresource.level_count = 1;
	depth_view_info.subresource.layer_count = 1;

	depth_texture_view_ = backend_->create_texture_view(depth_view_info);
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		if (swapchain_buffer_views_[i]) {
			RHIRenderPassInfo rp_info = {};
			rp_info.extent = swapchain_->get_extent();
			rp_info.color_attachments[0].texture_view = swapchain_buffer_views_[i];
			rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
			rp_info.color_attachments[0].clear_color = { 0.1f, 0.2f, 0.4f, 1.0f };
			rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;

			rp_info.depth_stencil_attachment.texture_view = depth_texture_view_;
			rp_info.depth_stencil_attachment.load_op = ATTACHMENT_LOAD_OP_CLEAR;
			rp_info.depth_stencil_attachment.clear_depth = 1.0f;
			rp_info.depth_stencil_attachment.clear_stencil = 0;
			rp_info.depth_stencil_attachment.store_op = ATTACHMENT_STORE_OP_STORE;

			swapchain_render_passes_[i] = backend_->create_render_pass(rp_info);
		}
	}

	pool_ = backend_->create_command_pool({ queue_ });

	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		per_frame_common_resources_[i].command = backend_->create_command_context(pool_);
		per_frame_common_resources_[i].start_semaphore = backend_->create_semaphore();
		per_frame_common_resources_[i].finish_semaphore = backend_->create_semaphore();
		per_frame_common_resources_[i].fence = backend_->create_fence(true);
	}

	// Initialize GPU Profiler (backend-agnostic factory)
	{
		gpu_profiler_ = backend_->create_gpu_profiler();

		// Set profiler on all command contexts
		if (gpu_profiler_) {
			for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
				per_frame_common_resources_[i].command->set_gpu_profiler(gpu_profiler_.get());
			}
		}
	}

	// Depth buffer visualization
	RHITextureInfo viz_info = {};
	viz_info.format = COLOR_FORMAT;
	viz_info.extent.width = WINDOW_EXTENT.width;
	viz_info.extent.height = WINDOW_EXTENT.height;
	viz_info.extent.depth = 1;
	viz_info.mip_levels = 1;
	viz_info.array_layers = 1;
	viz_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
	viz_info.type = RESOURCE_TYPE_RENDER_TARGET | RESOURCE_TYPE_TEXTURE;

	depth_visualize_texture_ = backend_->create_texture(viz_info);

	if (depth_visualize_texture_) {
		RHITextureViewInfo viz_view_info = {};
		viz_view_info.texture = depth_visualize_texture_;
		viz_view_info.format = COLOR_FORMAT;
		viz_view_info.view_type = VIEW_TYPE_2D;
		viz_view_info.subresource.aspect = TEXTURE_ASPECT_COLOR;
		viz_view_info.subresource.level_count = 1;
		viz_view_info.subresource.layer_count = 1;

		depth_visualize_texture_view_ = backend_->create_texture_view(viz_view_info);

		// Initialize depth visualize pass
		depth_visualize_pass_ = std::make_shared<render::DepthVisualizePass>();
		depth_visualize_pass_->init();

		if (depth_visualize_pass_->is_initialized()) {
			depth_visualize_initialized_ = true;
			INFO(LogRenderSystem, "Depth buffer visualization initialized");
		} else {
			WARN(LogRenderSystem, "Failed to initialize depth visualize pass");
		}
	} else {
		WARN(LogRenderSystem, "Failed to create depth visualize texture");
	}
}

void RenderSystem::create_fallback_resources() {
	if (!backend_) {
		return;
	}

	fallback_resources_.init(backend_);

	INFO(LogRenderSystem, "Fallback resources created");
}

void RenderSystem::init_passes() {
	// Initialize depth prepass
	depth_prepass_ = std::make_shared<render::DepthPrePass>();
	depth_prepass_->init();

	if (depth_prepass_->get_type() == render::PassType::Depth) {
		INFO(LogRenderSystem, "DepthPrePass initialized successfully");
	}

	// Initialize skybox pass
	skybox_pass_ = std::make_shared<render::SkyboxPass>();
	skybox_pass_->init();

	if (skybox_pass_->is_ready()) {
		INFO(LogRenderSystem, "SkyboxPass initialized successfully");
	} else {
		WARN(LogRenderSystem, "SkyboxPass initialization failed or incomplete");
	}

	// Initialize editor UI pass
	editor_ui_pass_ = std::make_shared<render::EditorUIPass>();
	editor_ui_pass_->init();

	if (editor_ui_pass_->is_ready()) {
		INFO(LogRenderSystem, "EditorUIPass initialized successfully");
	}

	// Initialize depth prepass resources
}



void RenderSystem::build_and_execute_rdg(uint32_t frame_index, const RenderPacket &packet) {
	// Create command list from command context
	CommandListInfo cmd_info;
	cmd_info.pool = pool_;
	cmd_info.context = per_frame_common_resources_[frame_index].command;
	cmd_info.bypass = true;
	auto command_list = std::make_shared<RHICommandList>(cmd_info);

	// Create RDG builder
	RDGBuilder rdg_builder(command_list);

	// Get current back buffer
	uint32_t current_buffer_index = swapchain_->get_current_frame_index();
	RHITextureRef back_buffer = swapchain_->get_texture(current_buffer_index);

	if (!back_buffer) {
		ERR(LogRenderSystem, "No back buffer available for RDG");
		return;
	}

	Extent2D extent = swapchain_->get_extent();

	// Import back buffer as RDG texture
	RDGTextureHandle color_target = rdg_builder.create_texture("BackBuffer")
											.import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
											.finish();

	// Import depth texture
	RDGTextureHandle depth_target = rdg_builder.create_texture("Depth")
											.import(depth_texture_, RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT)
											.finish();

	// Collect draw batches
	std::vector<render::DrawBatch> batches;
	mesh_manager_->collect_draw_batches(batches);

	// Get camera data early (needed for both batches and skybox)
	CameraComponent *camera = packet.active_camera;
	if (!camera && mesh_manager_) {
		camera = mesh_manager_->get_active_camera();
	}

	if (!camera) {
		WARN(LogRenderSystem, "No active camera for RDG rendering");
		rdg_builder.execute();
		return;
	}

	if (batches.empty()) {
		// No geometry to render, just clear the screen
		INFO(LogRenderSystem, "ClearPass executed (no geometry to render)");
		rdg_builder.create_render_pass("ClearPass")
				.color(0, color_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE,
						Color4{ 1.0f, 0.0f, 0.0f, 1.0f })
				.execute([extent](RDGPassContext context) {
					context.command->set_viewport({ 0, 0 }, { extent.width, extent.height });
					context.command->set_scissor({ 0, 0 }, { extent.width, extent.height });
				})
				.finish();
		// Note: Continue to skybox pass even with empty batches
	}

	// Get light data
	Vec3 light_dir = Vec3(0.0f, -1.0f, 0.0f);
	Vec3 light_color = Vec3(1.0f, 1.0f, 1.0f);
	float light_intensity = 1.0f;

	if (packet.active_scene) {
		for (auto &entity : packet.active_scene->entities_) {
			if (!entity) {
				continue;
			}
			auto *light = entity->get_component<DirectionalLightComponent>();
			if (light && light->enable()) {
				auto *transform = entity->get_component<TransformComponent>();
				if (transform) {
					light_dir = -transform->transform.front();
				}
				light_color = light->get_color();
				light_intensity = light->get_intensity();
				break;
			}
		}
	}

	// Execute depth prepass first (before any forward/render passes)
	if (enable_depth_prepass_ && depth_prepass_) {
		PROFILE_SCOPE("RenderSystem_DepthPrepass");

		depth_prepass_->set_per_frame_data(
				camera->get_view_matrix(),
				camera->get_projection_matrix());

		depth_prepass_->build(rdg_builder, depth_target, batches);
	}

	// Call custom RDG build function if set (for testing)
	// Custom RDG (e.g., deferred pass) should execute before forward pass
	if (custom_rdg_build_func_) {
		PROFILE_SCOPE("RenderSystem_CustomRDG");
		custom_rdg_build_func_(rdg_builder, packet);
	}

	// Build mesh passes using the mesh manager
	if (enable_pbr_pass_ || enable_npr_pass_) {
		PROFILE_SCOPE("RenderSystem_MeshPasses");
		mesh_manager_->build_rdg(rdg_builder, color_target, depth_target, enable_pbr_pass_, enable_npr_pass_);
	}

	// Build skybox pass (renders after opaque objects)
	if (enable_skybox_pass_ && skybox_pass_ && skybox_pass_->is_ready() && packet.active_scene) {
		PROFILE_SCOPE("RenderSystem_SkyboxPass");
		
		// Collect skybox components from scene
		std::vector<SkyboxComponent*> skyboxes = 
			packet.active_scene->get_components<SkyboxComponent>();
		
		if (!skyboxes.empty()) {
			skybox_pass_->build(rdg_builder, color_target, depth_target,
				camera->get_view_matrix(),
				camera->get_projection_matrix(),
				skyboxes);
		}
	}

	// Build editor UI pass (renders on top of everything)
	if (show_ui_ && editor_ui_pass_ && editor_ui_pass_->is_ready()) {
		// Set the UI draw function for this frame (will be called during build)
		editor_ui_pass_->set_ui_draw_function([this, &packet]() {
			draw_scene_hierarchy(packet.active_scene);
			draw_inspector_panel();
			
			if (show_buffer_debug_) {
				draw_buffer_debug();
			}
			
			draw_rdg_visualizer();
			
			ImGui::Begin("Renderer Debug", &show_ui_);
			
			if (ImGui::Checkbox("Wireframe", &wireframe_mode_)) {
				if (mesh_manager_) {
					mesh_manager_->set_wireframe(wireframe_mode_);
				}
			}
			
			ImGui::Checkbox("Show Buffer Debug", &show_buffer_debug_);
			ImGui::Checkbox("Show RDG Visualizer", &show_rdg_visualizer_);
			
			ImGui::Separator();
			ImGui::Text("Render Passes:");
			ImGui::Checkbox("Depth Prepass", &enable_depth_prepass_);
			ImGui::Checkbox("PBR Pass", &enable_pbr_pass_);
			ImGui::Checkbox("NPR Pass", &enable_npr_pass_);
			ImGui::Checkbox("Skybox Pass", &enable_skybox_pass_);
			ImGui::Checkbox("Depth Visualize", &enable_depth_visualize_);
			
			if (gizmo_manager_) {
				gizmo_manager_->draw_controls();
			}
			
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
					1000.0f / ImGui::GetIO().Framerate,
					ImGui::GetIO().Framerate);
			
			ImGui::Separator();
			ImGui::Separator();
			if (ImGui::Button("Toggle Profiler")) {
				ProfilerWidget::toggle_visibility();
			}
			ImGui::SameLine();
			if (ImGui::Button("Toggle GPU Profiler")) {
				GPUProfilerWidget::toggle_visibility();
			}
			
			ImGui::End();

			// Custom game UI callbacks
			{
				std::lock_guard<std::mutex> lock(custom_ui_callbacks_mutex_);
				for (const auto& [name, callback] : custom_ui_callbacks_) {
					callback();
				}
			}
			
			if (ProfilerWidget::is_visible()) {
				ProfilerWidget::draw_window();
			}
			
			if (GPUProfilerWidget::is_visible() && gpu_profiler_) {
				GPUProfilerWidget::draw_window(*gpu_profiler_);
			}
			
			// Draw gizmo in a transparent viewport window for proper ImGuizmo input handling.
			// ImGuizmo's IsHoveringWindow() requires the draw list to belong to a real
			// ImGui window that matches g.HoveredWindow; using GetForegroundDrawList()
			// breaks this check and prevents drag interaction.
			if (gizmo_manager_ && selected_entity_ && packet.active_camera) {
				ImGuiIO &io = ImGui::GetIO();
				
				// Calculate viewport area (exclude hierarchy and inspector panels)
				float hierarchy_width = 250.0f;
				float inspector_width = 300.0f;
				ImVec2 viewport_pos(hierarchy_width, 0);
				ImVec2 viewport_size(io.DisplaySize.x - hierarchy_width - inspector_width, io.DisplaySize.y);
				
				// Create a transparent overlay window so ImGuizmo can detect hover/click/drag
				ImGui::SetNextWindowPos(viewport_pos);
				ImGui::SetNextWindowSize(viewport_size);
				ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
				ImGui::Begin("##GizmoViewport", nullptr,
						ImGuiWindowFlags_NoDecoration |
						ImGuiWindowFlags_NoBackground |
						ImGuiWindowFlags_NoSavedSettings |
						ImGuiWindowFlags_NoFocusOnAppearing |
						ImGuiWindowFlags_NoBringToFrontOnFocus |
						ImGuiWindowFlags_NoNav |
						ImGuiWindowFlags_NoMove |
						ImGuiWindowFlags_NoResize);
				
				// Use the window's own draw list (nullptr) so ImGuizmo's
				// IsHoveringWindow() correctly matches g.HoveredWindow
				gizmo_manager_->draw_gizmo(packet.active_camera, selected_entity_,
						viewport_pos, viewport_size, nullptr);
				
				ImGui::End();
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);
				
				// Draw light gizmo at entity position
				draw_light_gizmo(packet.active_camera, selected_entity_,
						Extent2D{ static_cast<uint32_t>(io.DisplaySize.x),
								static_cast<uint32_t>(io.DisplaySize.y) });
			}
		});
		
		editor_ui_pass_->build(rdg_builder);
	}

	// Capture RDG info for visualization
	capture_rdg_info(rdg_builder);

	// Execute the RDG
	rdg_builder.execute();
}

void RenderSystem::capture_rdg_info(RDGBuilder &builder) {
	std::lock_guard<std::mutex> lock(rdg_info_mutex_);

	last_rdg_nodes_.clear();
	last_rdg_edges_.clear();
	rdg_graph_layout_dirty_ = true;

	// Helper map to track which resources we've added
	std::unordered_map<uint32_t, size_t> node_index_map;

	const auto &passes = builder.get_passes();

	// First pass: collect all pass nodes
	for (const auto &pass : passes) {
		if (!pass) {
			continue;
		}

		RDGNodeInfo node;
		node.name = pass->name();
		node.id = pass->ID();
		node.is_pass = true;
		node.x = node.y = 0; // Will be calculated during layout

		// Determine pass type
		switch (pass->node_type()) {
			case RDG_PASS_NODE_TYPE_RENDER:
				node.type = "Render";
				break;
			case RDG_PASS_NODE_TYPE_COMPUTE:
				node.type = "Compute";
				break;
			case RDG_PASS_NODE_TYPE_RAY_TRACING:
				node.type = "RayTracing";
				break;
			case RDG_PASS_NODE_TYPE_PRESENT:
				node.type = "Present";
				break;
			case RDG_PASS_NODE_TYPE_COPY:
				node.type = "Copy";
				break;
			default:
				node.type = "Unknown";
				break;
		}

		node_index_map[node.id] = last_rdg_nodes_.size();
		last_rdg_nodes_.push_back(std::move(node));
	}

	// Second pass: collect resources and edges
	for (const auto &pass : passes) {
		if (!pass) {
			continue;
		}

		size_t pass_idx = node_index_map[pass->ID()];

		// Collect texture inputs/outputs and create edges
		pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
			if (!texture) {
				return;
			}

			uint32_t tex_id = texture->ID();
			std::string res_name = texture->name();

			// Add resource node if not exists
			if (node_index_map.find(tex_id) == node_index_map.end()) {
				RDGNodeInfo res_node;
				res_node.name = res_name;
				res_node.id = tex_id;
				res_node.is_pass = false;
				res_node.type = "Texture";
				res_node.x = res_node.y = 0;
				node_index_map[tex_id] = last_rdg_nodes_.size();
				last_rdg_nodes_.push_back(std::move(res_node));
			}

			size_t tex_idx = node_index_map[tex_id];

			// Check if this is a read-only depth attachment
			bool is_readonly_depth = edge->as_depth_stencil && edge->read_only_depth;
			
			if (edge->as_color || (edge->as_depth_stencil && !is_readonly_depth) || edge->as_output_read || edge->as_output_read_write) {
				// Pass writes to resource: Pass -> Resource
				last_rdg_nodes_[pass_idx].outputs.push_back(res_name);
				last_rdg_nodes_[tex_idx].inputs.push_back(pass->name());
				last_rdg_edges_.push_back({ pass->ID(), tex_id, "" });
			} else {
				// Pass reads from resource: Resource -> Pass
				last_rdg_nodes_[pass_idx].inputs.push_back(res_name);
				last_rdg_nodes_[tex_idx].outputs.push_back(pass->name());
				last_rdg_edges_.push_back({ tex_id, pass->ID(), "" });
			}
		});

		// Collect buffer inputs/outputs
		pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
			if (!buffer) {
				return;
			}

			uint32_t buf_id = buffer->ID();
			std::string res_name = buffer->name();

			// Add resource node if not exists
			if (node_index_map.find(buf_id) == node_index_map.end()) {
				RDGNodeInfo res_node;
				res_node.name = res_name;
				res_node.id = buf_id;
				res_node.is_pass = false;
				res_node.type = "Buffer";
				res_node.x = res_node.y = 0;
				node_index_map[buf_id] = last_rdg_nodes_.size();
				last_rdg_nodes_.push_back(std::move(res_node));
			}

			size_t buf_idx = node_index_map[buf_id];

			if (edge->as_output_read || edge->as_output_read_write) {
				// Pass writes to buffer
				last_rdg_nodes_[pass_idx].outputs.push_back(res_name + " (Buf)");
				last_rdg_nodes_[buf_idx].inputs.push_back(pass->name());
				last_rdg_edges_.push_back({ pass->ID(), buf_id, "buf" });
			} else {
				// Pass reads from buffer
				last_rdg_nodes_[pass_idx].inputs.push_back(res_name + " (Buf)");
				last_rdg_nodes_[buf_idx].outputs.push_back(pass->name());
				last_rdg_edges_.push_back({ buf_id, pass->ID(), "buf" });
			}
		});
	}
}

bool RenderSystem::tick(const RenderPacket &packet) {
	PROFILE_FUNCTION();

	// Note: Window close check is now handled by Window::process_messages() in EngineContext

	// Use frame_index from packet for multi-threaded safety
	uint32_t frame_index = packet.frame_index;

	// Update active camera in mesh manager if provided
	if (packet.active_camera) {
		mesh_manager_->set_active_camera(packet.active_camera);
	}

	// Tick managers (collect render data)
	{
		PROFILE_SCOPE("RenderSystem_Managers");
		mesh_manager_->tick();
		light_manager_->tick(frame_index);
		update_global_setting();
	}

	// Execute rendering using frame_index from packet for thread safety
	auto &resource = per_frame_common_resources_[frame_index];
	resource.fence->wait();

	RHITextureRef swapchain_texture = swapchain_->get_new_frame(nullptr, resource.start_semaphore);
	RHICommandContextRef command = resource.command;

	Extent2D extent = swapchain_->get_extent();

	// Start ImGui new frame BEFORE building RDG (UI building happens in EditorUIPass)
	if (backend_ && show_ui_) {
		backend_->imgui_new_frame();
		ImGuizmo::BeginFrame();
	}

	command->begin_command();
	{
		PROFILE_SCOPE("RenderSystem_SceneRender");

		// Collect GPU profiler results from previous frames
		if (gpu_profiler_) {
			gpu_profiler_->collect_results();
		}

		// Begin GPU timestamp frame
		command->gpu_timestamp_begin_frame();

		// Use RDG-based rendering
		build_and_execute_rdg(frame_index, packet);

		// End GPU timestamp frame
		command->gpu_timestamp_end_frame();

		// Depth visualization
		if (enable_depth_visualize_ && depth_visualize_initialized_ && depth_visualize_pass_ && depth_visualize_texture_view_) {
			PROFILE_SCOPE("RenderSystem_DepthVisualize");
			CameraComponent *camera = packet.active_camera;
			if (!camera && mesh_manager_) {
				camera = mesh_manager_->get_active_camera();
			}
			if (camera) {
				Extent2D viz_extent = {
					depth_visualize_texture_->get_info().extent.width,
					depth_visualize_texture_->get_info().extent.height
				};
				depth_visualize_pass_->draw(
						command,
						depth_texture_,
						depth_visualize_texture_view_,
						viz_extent,
						camera->get_near(),
						camera->get_far());
			}
		}

		command->end_render_pass();
	}
	command->end_command();

	command->execute(resource.fence, resource.start_semaphore, resource.finish_semaphore);

	swapchain_->present(resource.finish_semaphore);

	if (backend_) {
		backend_->tick();
	}

	return true;
}

bool RenderSystem::add_custom_ui_callback(const std::string& name, std::function<void()> func) {
	std::lock_guard<std::mutex> lock(custom_ui_callbacks_mutex_);
	auto [it, inserted] = custom_ui_callbacks_.emplace(name, std::move(func));
	return inserted;
}

bool RenderSystem::remove_custom_ui_callback(const std::string& name) {
	std::lock_guard<std::mutex> lock(custom_ui_callbacks_mutex_);
	return custom_ui_callbacks_.erase(name) > 0;
}

void RenderSystem::clear_custom_ui_callbacks() {
	std::lock_guard<std::mutex> lock(custom_ui_callbacks_mutex_);
	custom_ui_callbacks_.clear();
}

void RenderSystem::cleanup_for_test() {
	// WARN(LogRenderSystem, "cleanup_for_test() called, backend_={}", (void*)backend_.get());
	
	// Clear custom UI callbacks
	clear_custom_ui_callbacks();
	
	// Check if backend is valid (device not destroyed)
	if (!backend_ || !backend_->is_valid()) {
		// WARN(LogRenderSystem, "Backend is null or invalid, skipping GPU cleanup");
		// Still cleanup non-GPU state
		if (mesh_manager_) {
			mesh_manager_->cleanup_for_test();
		}
		selected_entity_ = nullptr;
		return;
	}
	
	// Wait for GPU to complete all pending work
	for (auto &resource : per_frame_common_resources_) {
		if (resource.fence) {
			resource.fence->wait();
		}
	}
	
	// Cleanup mesh manager state (keeps passes initialized)
	if (mesh_manager_) {
		mesh_manager_->cleanup_for_test();
	}
	
	// Reset selected entity
	selected_entity_ = nullptr;
	
	// Wait for any pending GPU operations
	// Backend validity already checked above, but double-check for safety
	// Flush any pending GPU work
	auto temp_pool = backend_->create_command_pool({queue_});
	auto temp_cmd = backend_->create_command_context(temp_pool);
	temp_cmd->begin_command();
	temp_cmd->end_command();
	auto temp_fence = backend_->create_fence(false);
	if (temp_fence) {
		temp_cmd->execute(temp_fence, nullptr, nullptr);
		temp_fence->wait();
	}
}

void RenderSystem::destroy() {
	WARN(LogRenderSystem, "RenderSystem::destroy() called! backend_={}", (void*)backend_.get());
	
	// Destroy GPU profiler before backend
	if (gpu_profiler_) {
		gpu_profiler_->destroy();
		gpu_profiler_.reset();
	}

	// Shutdown ImGui
	if (backend_) {
		backend_->imgui_shutdown();
	}

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
	
	// Clear passes
	editor_ui_pass_.reset();
	skybox_pass_.reset();
	depth_prepass_.reset();

	// Clear per-frame resources
	for (auto &resource : per_frame_common_resources_) {
		resource.command.reset();
		resource.start_semaphore.reset();
		resource.finish_semaphore.reset();
		resource.fence.reset();
	}

	for (auto &view : swapchain_buffer_views_) {
		view.reset();
	}

	depth_texture_view_.reset();
	depth_texture_.reset();
	pool_.reset();
	swapchain_.reset();
	queue_.reset();
	surface_.reset();

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
	EngineContext::render_resource()->set_render_global_setting(global_setting_);
	*/
}

void RenderSystem::render_ui_begin() {
	// UI building is done directly in tick() after imgui_new_frame()
}

void RenderSystem::render_ui(RHICommandContextRef command) {
	(void)command;
}

void RenderSystem::draw_scene_hierarchy(Scene *scene) {
	ImGuiIO &io = ImGui::GetIO();
	float hierarchy_width = 250.0f;

	// Left side, fill top to bottom
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(hierarchy_width, io.DisplaySize.y));

	ImGui::Begin("Scene Hierarchy", nullptr,
			ImGuiWindowFlags_NoMove |
					ImGuiWindowFlags_NoResize);

	if (scene) {
		for (const auto &entity : scene->entities_) {
			if (entity) {
				draw_entity_node(entity.get());
			}
		}
	}

	ImGui::End();
}

void RenderSystem::draw_entity_node(Entity *entity) {
	if (!entity) {
		return;
	}

	// Get entity display info
	std::string icon = get_entity_icon(entity);
	std::string name = get_entity_name(entity);
	std::string label = icon + " " + name + "##" + std::to_string(reinterpret_cast<uintptr_t>(entity));

	// Check if this entity is selected
	bool is_selected = (selected_entity_ == entity);

	// Check if entity has submeshes (for MeshRenderer) or children
	auto* mesh_renderer = entity->get_component<MeshRendererComponent>();
	bool has_submeshes = (mesh_renderer != nullptr);
	bool has_children = entity->has_children();
	bool has_sub_content = has_submeshes || has_children;

	// Tree node flags
	// Note: Don't use OpenOnDoubleClick here - we handle double-click manually for camera move
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (is_selected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!has_sub_content) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	bool node_open = ImGui::TreeNodeEx(label.c_str(), flags);

	// Handle selection click
	if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		// Exclude double-click to avoid conflict with move camera feature
		if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			selected_entity_ = entity;
			INFO(LogRenderSystem, "Selected entity: {}", name);
		}
	}
	
	// Handle double-click: move camera to view the entity
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		move_camera_to_view_entity(entity);
	}

	if (node_open) {
		// Draw child entities recursively
		for (auto& child : entity->get_children()) {
			draw_entity_node(child.get());
		}
		// Draw submeshes if this is a mesh entity
		if (has_submeshes) {
			draw_mesh_submeshes(mesh_renderer);
		}
		ImGui::TreePop();
	}
}

void RenderSystem::draw_mesh_submeshes(MeshRendererComponent* mesh_renderer) {
	if (!mesh_renderer) {
		return;
	}

	auto model = mesh_renderer->get_model();
	if (!model) {
		ImGui::TreeNodeEx("  (No model)", ImGuiTreeNodeFlags_Leaf);
		return;
	}

	uint32_t submesh_count = model->get_submesh_count();
	for (uint32_t i = 0; i < submesh_count; ++i) {
		std::string submesh_label = "  [Submesh " + std::to_string(i) + "]";
		
		// Get material info if available
		auto material = mesh_renderer->get_material(i);
		if (material) {
			submesh_label += " - ";
			switch (material->get_material_type()) {
				case MaterialType::PBR: submesh_label += "PBR"; break;
				case MaterialType::NPR: submesh_label += "NPR"; break;
				case MaterialType::Skybox: submesh_label += "Skybox"; break;
				default: submesh_label += "Base"; break;
			}
		}
		
		ImGui::TreeNodeEx(submesh_label.c_str(), ImGuiTreeNodeFlags_Leaf);
		ImGui::TreePop();
	}
}

void RenderSystem::move_camera_to_view_entity(Entity* target_entity) {
	if (!target_entity) {
		return;
	}
	
	// Get target entity's transform
	auto* target_transform = target_entity->get_component<TransformComponent>();
	if (!target_transform) {
		WARN(LogRenderSystem, "Cannot move camera: target entity has no TransformComponent");
		return;
	}
	
	// Get active camera
	auto* world = EngineContext::world();
	if (!world) {
		return;
	}
	
	auto* scene = world->get_active_scene();
	if (!scene) {
		return;
	}
	
	CameraComponent* camera = scene->get_camera();
	if (!camera) {
		WARN(LogRenderSystem, "Cannot move camera: no active camera");
		return;
	}
	
	// Get camera's owner entity and its transform component
	Entity* camera_entity = camera->get_owner();
	if (!camera_entity) {
		WARN(LogRenderSystem, "Cannot move camera: camera has no owner entity");
		return;
	}
	
	auto* camera_transform = camera_entity->get_component<TransformComponent>();
	if (!camera_transform) {
		WARN(LogRenderSystem, "Cannot move camera: camera entity has no TransformComponent");
		return;
	}
	
	// ★ 计算目标中心点：使用包围盒中心
	Vec3 target_center = target_transform->get_world_position();
	Vec3 camera_pos = target_center;
	
	// 尝试从 MeshRendererComponent 获取包围盒
	auto* mesh_renderer = target_entity->get_component<MeshRendererComponent>();
	if (mesh_renderer && mesh_renderer->get_model()) {
		BoundingBox bbox = mesh_renderer->get_model()->get_bounding_box();
		// 包围盒中心（本地空间）
		Vec3 local_center = (bbox.min + bbox.max) * 0.5f;
		const auto& t = target_transform->transform;
		
		// 计算目标中心（世界空间）
		target_center = t.get_position() 
			+ t.right() * local_center.x 
			+ t.up() * local_center.y 
			+ t.front() * local_center.z;
		
		// 计算正面（z+方向）面中心：xy为包围盒中心，z为max.z
		Vec3 local_front_face_center = Vec3(local_center.x, local_center.y, bbox.max.z);
		
		// 相机位置：正面面中心 + z+方向2m
		// world_pos = position + right * local_x + up * local_y + front * (local_z + 2.0f)
		camera_pos = t.get_position()
			+ t.right() * local_front_face_center.x
			+ t.up() * local_front_face_center.y
			+ t.front() * (local_front_face_center.z + 2.0f);
		
		INFO(LogRenderSystem, "Using bounding box front face center: target=({:.2f}, {:.2f}, {:.2f}), camera=({:.2f}, {:.2f}, {:.2f})", 
			 target_center.x, target_center.y, target_center.z,
			 camera_pos.x, camera_pos.y, camera_pos.z);
	} else {
		// 没有 MeshRenderer，使用 transform position 并假设中心在 Y 轴上方 1 米处
		target_center.y += 1.0f;
		// 相机放在z+方向2m处
		camera_pos = target_center + target_transform->transform.front() * 2.0f;
	}
	
	// Calculate rotation to look at target center
	// Direction from camera to target
	Vec3 look_dir = (target_center - camera_pos).normalized();
	
	// Calculate euler angles (pitch, yaw, roll) from look direction
	// Based on coordinate system: X=right, Y=up, Z=front
	// pitch: rotation around X (right) axis - up/down
	// yaw: rotation around Y (up) axis - left/right
	// roll: rotation around Z (front-facing screen axis) - usually 0
	
	float pitch = 0.0f;
	float yaw = 0.0f;
	
	// Calculate yaw from front direction projected onto XZ plane
	// atan2(front.x, front.z) gives yaw when +Z is forward
	yaw = std::atan2(look_dir.x, look_dir.z);
	
	// Calculate pitch from front direction
	// asin(front.y) gives pitch (clamped to avoid gimbal lock)
	float horizontal_dist = std::sqrt(look_dir.x * look_dir.x + look_dir.z * look_dir.z);
	pitch = std::atan2(look_dir.y, horizontal_dist);
	
	// Convert to degrees
	pitch = Math::to_angle(pitch);
	yaw = Math::to_angle(yaw);
	
	// Set camera position and rotation
	camera_transform->transform.set_position(camera_pos);
	camera_transform->transform.set_rotation(Vec3(pitch, yaw, 0.0f));
	
	INFO(LogRenderSystem, "Camera moved to view entity: pos=({:.2f}, {:.2f}, {:.2f}), pitch={:.1f}, yaw={:.1f}",
		 camera_pos.x, camera_pos.y, camera_pos.z, pitch, yaw);
}

// Draw Inspector panel for selected entity
void RenderSystem::draw_inspector_panel() {
	ImGuiIO &io = ImGui::GetIO();
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
		auto &inspector = ReflectInspector::get();

		for (const auto &comp_ptr : selected_entity_->get_components()) {
			if (!comp_ptr) {
				continue;
			}

			Component *comp = comp_ptr.get();
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
	if (!show_ui_ || !show_buffer_debug_) {
		return;
	}

	// Always set position and size to ensure window is visible at center-top
	float window_width = (float)WINDOW_EXTENT.width * 0.5f;
	float window_height = (float)WINDOW_EXTENT.height * 0.25f;
	float pos_x = ((float)WINDOW_EXTENT.width - window_width) * 0.5f;
	float pos_y = 0.0f;

	ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height), ImGuiCond_Always);

	// Force window to be open
	bool open = true;
	ImGui::Begin("Buffer Debug", &open);

	// Depth Buffer
	ImGui::Text("Depth Buffer:");
	if (depth_texture_) {
		const auto &info = depth_texture_->get_info();
		ImGui::Text("  Texture: valid");
		ImGui::Text("  Type flags: 0x%X (DEPTH=%s, TEXTURE=%s)",
				info.type,
				(info.type & RESOURCE_TYPE_DEPTH_STENCIL) ? "Y" : "N",
				(info.type & RESOURCE_TYPE_TEXTURE) ? "Y" : "N");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "  Texture: NULL");
	}

	// Show depth buffer visualization (converted to color)
	if (depth_visualize_initialized_ && depth_visualize_texture_view_) {
		void *tex_id = depth_visualize_texture_view_->raw_handle();
		ImGui::Text("  Visualized Depth:");
		if (tex_id) {
			float display_width = 280.0f;
			float display_height = display_width * ((float)WINDOW_EXTENT.height / (float)WINDOW_EXTENT.width);
			ImGui::Image(tex_id, ImVec2(display_width, display_height));
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "  Error: SRV is null");
		}
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  Depth visualize not initialized");

		// Fallback: try to show raw depth view if available
		if (depth_texture_view_) {
			void *tex_id = depth_texture_view_->raw_handle();
			ImGui::Text("  Raw depth view (may not display correctly):");
			if (tex_id) {
				float display_width = 280.0f;
				float display_height = display_width * ((float)WINDOW_EXTENT.height / (float)WINDOW_EXTENT.width);
				ImGui::Image(tex_id, ImVec2(display_width, display_height));
			}
		}
	}

	ImGui::Separator();

	// Back Buffer info
	ImGui::Text("Back Buffer:");
	if (swapchain_) {
		auto tex = swapchain_->get_texture(swapchain_->get_current_frame_index());
		ImGui::Text("  Current frame: %d", swapchain_->get_current_frame_index());
		ImGui::Text("  Texture valid: %s", tex ? "Yes" : "No");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "  Swapchain not available");
	}

	ImGui::End();
}

// Draw RDG Visualizer panel
void RenderSystem::draw_rdg_visualizer() {
	if (!show_rdg_visualizer_) {
		return;
	}

	ImGui::Begin("RDG Visualizer", &show_rdg_visualizer_);

	std::lock_guard<std::mutex> lock(rdg_info_mutex_);

	if (last_rdg_nodes_.empty()) {
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No RDG data captured yet.");
		ImGui::Text("RDG rendering will capture data on next frame.");
		ImGui::End();
		return;
	}

	// Pass type colors
	auto get_type_color = [](const std::string &type) -> ImVec4 {
		if (type == "Render") {
			return ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green
		}
		if (type == "Compute") {
			return ImVec4(0.2f, 0.4f, 0.9f, 1.0f); // Blue
		}
		if (type == "Copy") {
			return ImVec4(0.9f, 0.6f, 0.2f, 1.0f); // Orange
		}
		if (type == "Present") {
			return ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // Red
		}
		if (type == "RayTracing") {
			return ImVec4(0.8f, 0.2f, 0.8f, 1.0f); // Purple
		}
		if (type == "Texture") {
			return ImVec4(0.9f, 0.9f, 0.3f, 1.0f); // Yellow
		}
		if (type == "Buffer") {
			return ImVec4(0.5f, 0.8f, 0.9f, 1.0f); // Cyan
		}
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	};

	// Node background colors
	auto get_node_bg_color = [](const RDGNodeInfo &node) -> ImU32 {
		if (!node.is_pass) {
			return IM_COL32(60, 60, 40, 255); // Resource: Dark yellow-ish
		}
		if (node.type == "Render") {
			return IM_COL32(40, 80, 40, 255);
		}
		if (node.type == "Compute") {
			return IM_COL32(40, 60, 100, 255);
		}
		if (node.type == "Copy") {
			return IM_COL32(100, 80, 40, 255);
		}
		if (node.type == "Present") {
			return IM_COL32(100, 40, 40, 255);
		}
		if (node.type == "RayTracing") {
			return IM_COL32(80, 40, 100, 255);
		}
		return IM_COL32(60, 60, 60, 255);
	};

	// Graph View
		ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
		ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
		if (canvas_sz.x < 50.0f) {
			canvas_sz.x = 50.0f;
		}
		if (canvas_sz.y < 50.0f) {
			canvas_sz.y = 50.0f;
		}
		ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

		// Draw graph canvas
		ImGuiIO &io = ImGui::GetIO();
		ImDrawList *draw_list = ImGui::GetWindowDrawList();

		// Background
		draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(20, 20, 25, 255));
		draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(80, 80, 80, 255));

		// Simple layout: Passes in a row at top, resources below
		if (rdg_graph_layout_dirty_) {
			float pass_x = 50;
			float pass_y = 50;
			float res_x = 50;
			float res_y = 200;

			for (auto &node : last_rdg_nodes_) {
				if (node.is_pass) {
					node.x = pass_x;
					node.y = pass_y;
					pass_x += 180;
				} else {
					node.x = res_x;
					node.y = res_y;
					res_x += 150;
					if (res_x > 800) {
						res_x = 50;
						res_y += 80;
					}
				}
			}
			rdg_graph_layout_dirty_ = false;
		}

		// Draw edges first (behind nodes)
		for (const auto &edge : last_rdg_edges_) {
			auto from_it = std::find_if(last_rdg_nodes_.begin(), last_rdg_nodes_.end(),
					[&](const auto &n) { return n.id == edge.from_id; });
			auto to_it = std::find_if(last_rdg_nodes_.begin(), last_rdg_nodes_.end(),
					[&](const auto &n) { return n.id == edge.to_id; });

			if (from_it != last_rdg_nodes_.end() && to_it != last_rdg_nodes_.end()) {
				ImVec2 p1 = ImVec2(canvas_p0.x + from_it->x + 60, canvas_p0.y + from_it->y + 25);
				ImVec2 p2 = ImVec2(canvas_p0.x + to_it->x + 60, canvas_p0.y + to_it->y);

				// Bezier curve
				ImVec2 cp1 = ImVec2(p1.x, p1.y + 30);
				ImVec2 cp2 = ImVec2(p2.x, p2.y - 30);

				// Determine color based on edge direction
				ImU32 edge_color = IM_COL32(150, 150, 150, 150);
				if (from_it->is_pass && !to_it->is_pass) {
					edge_color = IM_COL32(255, 100, 100, 180);  // Pass -> Resource (Write): Red
				} else if (!from_it->is_pass && to_it->is_pass) {
					edge_color = IM_COL32(100, 200, 255, 180);  // Resource -> Pass (Read): Blue
				}

				draw_list->AddBezierCubic(p1, cp1, cp2, p2, edge_color, 2.0f);

				ImVec2 dir = ImVec2(p2.x - cp2.x, p2.y - cp2.y);
				float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
				if (len > 0) {
					dir.x /= len;
					dir.y /= len;
					ImVec2 normal = ImVec2(-dir.y, dir.x);
					ImVec2 arrow_p1 = ImVec2(p2.x - 8 * dir.x - 4 * normal.x, p2.y - 8 * dir.y - 4 * normal.y);
					ImVec2 arrow_p2 = ImVec2(p2.x - 8 * dir.x + 4 * normal.x, p2.y - 8 * dir.y + 4 * normal.y);
					draw_list->AddTriangleFilled(p2, arrow_p1, arrow_p2, edge_color);
				}
			}
		}

		// Draw nodes
		for (const auto &node : last_rdg_nodes_) {
			ImVec2 node_pos = ImVec2(canvas_p0.x + node.x, canvas_p0.y + node.y);
			ImVec2 node_size = node.is_pass ? ImVec2(120, 50) : ImVec2(100, 40);

			// Node body
			ImU32 bg_color = get_node_bg_color(node);
			draw_list->AddRectFilled(node_pos, ImVec2(node_pos.x + node_size.x, node_pos.y + node_size.y),
					bg_color, 4.0f);
			draw_list->AddRect(node_pos, ImVec2(node_pos.x + node_size.x, node_pos.y + node_size.y),
					IM_COL32(150, 150, 150, 255), 4.0f);

			// Node text
			ImVec4 text_color = get_type_color(node.type);
			draw_list->AddText(ImVec2(node_pos.x + 5, node_pos.y + 5),
					IM_COL32(255, 255, 255, 255), node.name.c_str());

			// Type label
			std::string type_label = node.is_pass ? node.type : "Res";
			draw_list->AddText(ImVec2(node_pos.x + 5, node_pos.y + 25),
					IM_COL32((int)(text_color.x * 255), (int)(text_color.y * 255),
							(int)(text_color.z * 255), 255),
					type_label.c_str());
		}

		// Controls
		ImGui::SetCursorScreenPos(canvas_p0);
		ImGui::InvisibleButton("canvas", canvas_sz);

		// Info text
		ImGui::SetCursorScreenPos(ImVec2(canvas_p0.x + 10, canvas_p0.y + 10));
		ImGui::Text("Nodes: %zu | Edges: %zu", last_rdg_nodes_.size(), last_rdg_edges_.size());

		// Legend
		ImGui::SetCursorScreenPos(ImVec2(canvas_p0.x + 10, canvas_p0.y + canvas_sz.y - 30));
		ImGui::Text("Pass: ");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Render"), "Render");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Compute"), "Compute");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Copy"), "Copy");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Present"), "Present");
		ImGui::SameLine();
		ImGui::Text("| Resource: ");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Texture"), "Texture");
		ImGui::SameLine();
		ImGui::TextColored(get_type_color("Buffer"), "Buffer");

	ImGui::End();
}

// Draw light gizmo visualization
void RenderSystem::draw_light_gizmo(CameraComponent *camera, Entity *entity, const Extent2D &extent) {
	if (!camera || !entity) {
		return;
	}

	auto *transform = entity->get_component<TransformComponent>();
	if (!transform) {
		return;
	}

	Vec3 position = transform->get_world_position();

	// Project 3D position to screen space
	Mat4 view = camera->get_view_matrix();
	Mat4 proj = camera->get_projection_matrix();
	Mat4 view_proj = view * proj;

	Vec4 pos_clip = view_proj * Vec4(position.x, position.y, position.z, 1.0f);
	if (pos_clip.w <= 0) {
		return; // Behind camera
	}

	Vec3 pos_ndc = pos_clip.xyz() / pos_clip.w;
	ImVec2 screen_pos;
	screen_pos.x = (pos_ndc.x * 0.5f + 0.5f) * extent.width;
	screen_pos.y = (1.0f - (pos_ndc.y * 0.5f + 0.5f)) * extent.height; // Flip Y for screen coords

	ImDrawList *draw_list = ImGui::GetForegroundDrawList();

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
	if (auto *point_light = entity->get_component<PointLightComponent>()) {
		Vec3 color_vec = point_light->get_color();
		ImU32 color = IM_COL32(
				static_cast<int>(color_vec.x * 255),
				static_cast<int>(color_vec.y * 255),
				static_cast<int>(color_vec.z * 255),
				255);
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

void DefaultRenderResource::init(RHIBackendRef backend_) {
	{
		RHITextureInfo info = {};
		info.extent = { 1, 1, 1 };
		info.format = FORMAT_R8G8B8A8_UNORM;
		info.type = RESOURCE_TYPE_TEXTURE;
		info.memory_usage = MEMORY_USAGE_GPU_ONLY;
		info.mip_levels = 1;
		info.array_layers = 1;

		fallback_white_texture_ = backend_->create_texture(info);

		uint32_t white = 0xFFFFFFFF;

		RHIBufferInfo buf_info = {};
		buf_info.size = sizeof(uint32_t);
		buf_info.type = RESOURCE_TYPE_BUFFER; // Use VertexBuffer as generic staging
		buf_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
		buf_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
		auto staging = backend_->create_buffer(buf_info);

		if (staging) {
			void *data = staging->map();
			if (data) {
				memcpy(data, &white, sizeof(uint32_t));
				staging->unmap();
			}

			auto cmd = backend_->get_immediate_command();
			if (cmd) {
				TextureSubresourceLayers layers = {};
				layers.aspect = TEXTURE_ASPECT_COLOR;
				layers.layer_count = 1;
				layers.base_array_layer = 0;
				layers.mip_level = 0;

				cmd->copy_buffer_to_texture(staging, 0, fallback_white_texture_, layers);
			}
			staging->destroy();
		}
	}

	// Create 1x1 Black Texture (0, 0, 0, 255)
	{
		RHITextureInfo info = {};
		info.extent = { 1, 1, 1 };
		info.format = FORMAT_R8G8B8A8_UNORM;
		info.type = RESOURCE_TYPE_TEXTURE;
		info.memory_usage = MEMORY_USAGE_GPU_ONLY;
		info.mip_levels = 1;
		info.array_layers = 1;

		fallback_black_texture_ = backend_->create_texture(info);

		uint32_t black = 0xFF000000; // ARGB: A=255, R=0, G=0, B=0

		RHIBufferInfo buf_info = {};
		buf_info.size = sizeof(uint32_t);
		buf_info.type = RESOURCE_TYPE_BUFFER;
		buf_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
		buf_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
		auto staging = backend_->create_buffer(buf_info);

		if (staging) {
			void* data = staging->map();
			if (data) {
				memcpy(data, &black, sizeof(uint32_t));
				staging->unmap();
			}

			auto cmd = backend_->get_immediate_command();
			if (cmd) {
				TextureSubresourceLayers layers = {};
				layers.aspect = TEXTURE_ASPECT_COLOR;
				layers.layer_count = 1;
				layers.base_array_layer = 0;
				layers.mip_level = 0;

				cmd->copy_buffer_to_texture(staging, 0, fallback_black_texture_, layers);
			}
			staging->destroy();
		}
	}

	// Create 1x1 Normal Texture (128, 128, 255) -> Flat Normal (0, 0, 1)
	{
		RHITextureInfo info = {};
		info.extent = { 1, 1, 1 };
		info.format = FORMAT_R8G8B8A8_UNORM;
		info.type = RESOURCE_TYPE_TEXTURE;
		info.memory_usage = MEMORY_USAGE_GPU_ONLY;
		info.mip_levels = 1;
		info.array_layers = 1;

		fallback_normal_texture_ = backend_->create_texture(info);

		// RGBA: R=128, G=128, B=255, A=255 -> 0xFFFF8080 (Little Endian uint32)
		uint32_t normal = 0xFFFF8080;

		RHIBufferInfo buf_info = {};
		buf_info.size = sizeof(uint32_t);
		buf_info.type = RESOURCE_TYPE_BUFFER;
		buf_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
		buf_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
		auto staging = backend_->create_buffer(buf_info);

		if (staging) {
			void *data = staging->map();
			if (data) {
				memcpy(data, &normal, sizeof(uint32_t));
				staging->unmap();
			}

			auto cmd = backend_->get_immediate_command();
			if (cmd) {
				TextureSubresourceLayers layers = {};
				layers.aspect = TEXTURE_ASPECT_COLOR;
				layers.layer_count = 1;
				layers.base_array_layer = 0;
				layers.mip_level = 0;

				cmd->copy_buffer_to_texture(staging, 0, fallback_normal_texture_, layers);
			}
			staging->destroy();
		}
	}
}
