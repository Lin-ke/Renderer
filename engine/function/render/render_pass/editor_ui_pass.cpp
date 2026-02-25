#include "engine/function/render/render_pass/editor_ui_pass.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/main/engine_context.h"

#include <imgui.h>

DEFINE_LOG_TAG(LogEditorUIPass, "EditorUIPass");

namespace render {

EditorUIPass::EditorUIPass() = default;

EditorUIPass::~EditorUIPass() = default;

void EditorUIPass::init() {
    INFO(LogEditorUIPass, "Initializing EditorUIPass...");
    initialized_ = true;
    INFO(LogEditorUIPass, "EditorUIPass initialized successfully");
}

void EditorUIPass::build(RDGBuilder& builder) {
    if (!initialized_ || !is_enabled()) {
        return;
    }

    if (!ui_draw_func_) {
        // No UI draw function set, skip this pass
        return;
    }

    // Build ImGui UI - this must happen after ImGui::NewFrame() and before ImGui::Render()
    ui_draw_func_();
    
    // Generate ImGui draw data
    ImGui::Render();

    auto render_system = EngineContext::render_system();
    if (!render_system) {
        return;
    }

    auto swapchain = render_system->get_swapchain();
    if (!swapchain) {
        return;
    }

    // Get current back buffer
    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) {
        return;
    }

    Extent2D extent = swapchain->get_extent();

    // Import back buffer as color target (load existing content, store result)
    RDGTextureHandle color_target = builder.create_texture("EditorUI_Color")
        .import(back_buffer, RESOURCE_STATE_COLOR_ATTACHMENT)
        .finish();

    // Get depth texture for depth attachment (to handle ImGui windows overlapping correctly)
    auto depth_texture = render_system->get_depth_texture();
    RDGTextureHandle depth_target(UINT32_MAX);
    bool has_depth = false;
    if (depth_texture) {
        depth_target = builder.create_texture("EditorUI_Depth")
            .import(depth_texture, RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT)
            .finish();
        has_depth = true;
    }

    // Capture ui_draw_func_ by copy to ensure it stays valid
    auto draw_func = ui_draw_func_;

    // Create render pass for ImGui rendering
    auto rp_builder = builder.create_render_pass("EditorUI_Pass")
        .color(0, color_target, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE);
    
    // Add depth attachment if available (helps with some ImGui rendering scenarios)
    if (has_depth) {
        rp_builder.depth_stencil(depth_target, ATTACHMENT_LOAD_OP_LOAD, 
                                 ATTACHMENT_STORE_OP_DONT_CARE, 1.0f, 0);
    }

    rp_builder.execute([draw_func, extent](RDGPassContext context) {
        RHICommandListRef cmd = context.command;
        if (!cmd) {
            return;
        }

        // Set viewport and scissor to full screen
        cmd->set_viewport({0, 0}, {extent.width, extent.height});
        cmd->set_scissor({0, 0}, {extent.width, extent.height});

        // Render ImGui draw data
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data && draw_data->CmdListsCount > 0) {
            cmd->imgui_render_draw_data();
        }
    })
    .finish();
}

} // namespace render
