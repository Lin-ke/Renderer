#pragma once

#include "engine/function/render/render_pass/render_pass.h"

namespace render {

/**
 * @brief Editor UI Pass - Renders ImGui interface
 * 
 * This pass handles all ImGui rendering, including:
 * - Scene hierarchy panel
 * - Inspector panel
 * - Debug visualizers (buffer, RDG graph)
 * - Gizmo rendering
 * - Profiler widget
 */
class EditorUIPass : public RenderPass {
public:
    EditorUIPass();
    ~EditorUIPass() override;

    void init() override;
    void build(RDGBuilder& builder, RDGTextureHandle color, Extent2D extent,
               std::shared_ptr<ImDrawData> draw_data, RDGTextureHandle preview);

    std::string_view get_name() const override { return "EditorUIPass"; }
    PassType get_type() const override { return PassType::EditorUI; }

    bool is_ready() const { return initialized_; }

private:
    bool initialized_ = false;
};

} // namespace render
