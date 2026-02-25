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
    void build(RDGBuilder& builder) override;

    std::string_view get_name() const override { return "EditorUIPass"; }
    PassType get_type() const override { return PassType::EditorUI; }

    bool is_ready() const { return initialized_; }

    /**
     * @brief Set the function that will draw all ImGui UI elements
     * @param draw_func Function that contains all ImGui:: calls
     */
    void set_ui_draw_function(std::function<void()> draw_func) { ui_draw_func_ = std::move(draw_func); }

private:
    bool initialized_ = false;
    std::function<void()> ui_draw_func_;
};

} // namespace render
