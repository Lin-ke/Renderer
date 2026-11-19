#include "engine/function/render/render_pass/editor_ui_pass.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/core/window/window.h"
#include "engine/function/render/rhi/rhi_command_list.h"

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

void EditorUIPass::build(RDGBuilder& builder, RDGTextureHandle color, Extent2D extent,
                         std::shared_ptr<ImDrawData> draw_data, RDGTextureHandle preview) {
    if (!initialized_ || !is_enabled() || !draw_data || !draw_data->CmdListsCount) return;

    builder.create_render_pass("EditorUI_Pass")
        .color(0, color, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
        .sample(preview, {TEXTURE_ASPECT_NONE, 0, 1, 0, 1})
        .execute([draw_data = std::move(draw_data), extent](RDGPassContext context) {
            std::scoped_lock lock(Window::imgui_mutex());
            context.command->set_viewport({0, 0}, {extent.width, extent.height});
            context.command->set_scissor({0, 0}, {extent.width, extent.height});
            context.command->imgui_render_draw_data(draw_data);
        })
        .finish();
}

} // namespace render
