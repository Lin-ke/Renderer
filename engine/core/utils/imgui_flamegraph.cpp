#include "imgui_flamegraph.h"
#include "imgui_internal.h"

namespace ImGuiFlameGraph {


static inline ImVec2 ImVec2Add(const ImVec2& a, const ImVec2& b) {
    return ImVec2(a.x + b.x, a.y + b.y);
}

static inline ImVec2 ImVec2Minus(const ImVec2& a, const ImVec2& b) {
    return ImVec2(a.x - b.x, a.y - b.y);
}

static inline ImVec2 ImVec2Times(const ImVec2& a, const ImVec2& b) {
    return ImVec2(a.x * b.x, a.y * b.y);
}

void PlotFlame(
    const char* label,
    void (*values_getter)(float* start, float* end, ImU8* level, const char** caption,
                          const void* data, int idx),
    const void* data,
    int values_count,
    int values_offset,
    const char* overlay_text,
    float scale_min,
    float scale_max,
    ImVec2 graph_size)
{
    ImGui::PushItemWidth(-1);   

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    
    ImU8 max_depth = 0;
    for (int i = values_offset; i < values_count; ++i) {
        ImU8 depth;
        values_getter(nullptr, nullptr, &depth, nullptr, data, i);
        max_depth = ImMax(max_depth, depth);
    }

    const auto block_height = ImGui::GetTextLineHeight() + (style.FramePadding.y * 2);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    
    if (graph_size.x == 0.0f)
        graph_size.x = ImGui::CalcItemWidth();
    if (graph_size.y == 0.0f)
        graph_size.y = label_size.y + (style.FramePadding.y * 3) + block_height * (max_depth + 1);

    const ImRect frame_bb(window->DC.CursorPos, 
                          ImVec2Add(window->DC.CursorPos, graph_size));
    const ImRect inner_bb(ImVec2Add(frame_bb.Min, style.FramePadding),
                          ImVec2Minus(frame_bb.Max, style.FramePadding));
    const ImRect total_bb(frame_bb.Min,
                          ImVec2Add(frame_bb.Max, 
                                   ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0)));
    
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, 0, &frame_bb))
        return;

    
    if (scale_min == FLT_MAX || scale_max == FLT_MAX) {
        float v_min = FLT_MAX;
        float v_max = -FLT_MAX;
        for (int i = values_offset; i < values_count; i++) {
            float v_start, v_end;
            values_getter(&v_start, &v_end, nullptr, nullptr, data, i);
            if (v_start == v_start)
                v_min = ImMin(v_min, v_start);
            if (v_end == v_end)
                v_max = ImMax(v_max, v_end);
        }
        if (scale_min == FLT_MAX)
            scale_min = v_min;
        if (scale_max == FLT_MAX)
            scale_max = v_max;
    }

    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, 
                       ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    bool any_hovered = false;
    if (values_count - values_offset >= 1) {
        const ImU32 col_base = ImGui::GetColorU32(ImGuiCol_PlotHistogram) & 0x77FFFFFF;
        const ImU32 col_hovered = ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered) & 0x77FFFFFF;
        const ImU32 col_outline_base = ImGui::GetColorU32(ImGuiCol_PlotHistogram) & 0x7FFFFFFF;
        const ImU32 col_outline_hovered = ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered) & 0x7FFFFFFF;

        for (int i = values_offset; i < values_count; ++i) {
            float stage_start, stage_end;
            ImU8 depth;
            const char* caption;

            values_getter(&stage_start, &stage_end, &depth, &caption, data, i);

            auto duration = scale_max - scale_min;
            if (duration == 0) {
                return;
            }

            auto start = stage_start - scale_min;
            auto end = stage_end - scale_min;

            auto start_x = static_cast<float>(start / static_cast<double>(duration));
            auto end_x = static_cast<float>(end / static_cast<double>(duration));

            float width = inner_bb.Max.x - inner_bb.Min.x;
            float height = block_height * (max_depth - depth + 1) - style.FramePadding.y;

            auto pos0 = ImVec2Add(inner_bb.Min, ImVec2(start_x * width, height));
            auto pos1 = ImVec2Add(inner_bb.Min, ImVec2(end_x * width, height + block_height));

            bool v_hovered = false;
            if (ImGui::IsMouseHoveringRect(pos0, pos1)) {
                ImGui::SetTooltip("%s: %.4f ms", caption, stage_end - stage_start);
                v_hovered = true;
                any_hovered = v_hovered;
            }

            window->DrawList->AddRectFilled(pos0, pos1, v_hovered ? col_hovered : col_base);
            window->DrawList->AddRect(pos0, pos1, v_hovered ? col_outline_hovered : col_outline_base);
            
            auto text_size = ImGui::CalcTextSize(caption);
            auto box_size = ImVec2Minus(pos1, pos0);
            if (text_size.x < box_size.x) {
                auto text_offset = ImVec2Times(ImVec2(0.5f, 0.5f), ImVec2Minus(box_size, text_size));
                ImGui::RenderText(ImVec2Add(pos0, text_offset), caption);
            }
        }

        
        if (overlay_text) {
            ImGui::RenderTextClipped(
                ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y),
                frame_bb.Max, overlay_text, nullptr, nullptr, ImVec2(0.5f, 0.0f));
        }

        if (label_size.x > 0.0f) {
            ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
        }
    }

    if (!any_hovered && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Total: %.4f ms", scale_max - scale_min);
    }

    ImGui::PopItemWidth();
}

} // namespace ImGuiFlameGraph
