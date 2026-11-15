#include "gpu_profiler_widget.h"
#include "gpu_profiler.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

bool GPUProfilerWidget::show_window_ = true;

// Predefined pass colors (visually distinct, muted palette)
static const ImVec4 kPassColors[] = {
    {0.22f, 0.47f, 0.96f, 1.0f}, // Blue
    {0.93f, 0.51f, 0.18f, 1.0f}, // Orange
    {0.30f, 0.75f, 0.35f, 1.0f}, // Green
    {0.84f, 0.24f, 0.24f, 1.0f}, // Red
    {0.58f, 0.36f, 0.82f, 1.0f}, // Purple
    {0.64f, 0.52f, 0.30f, 1.0f}, // Brown
    {0.85f, 0.60f, 0.85f, 1.0f}, // Pink
    {0.50f, 0.50f, 0.50f, 1.0f}, // Gray
    {0.10f, 0.75f, 0.75f, 1.0f}, // Cyan
    {0.96f, 0.82f, 0.18f, 1.0f}, // Yellow
    {0.40f, 0.65f, 0.30f, 1.0f}, // Dark green
    {0.75f, 0.40f, 0.55f, 1.0f}, // Mauve
};
static constexpr int kColorCount = sizeof(kPassColors) / sizeof(kPassColors[0]);

void GPUProfilerWidget::draw_window(GPUProfiler& profiler, bool* open) {
    if (!show_window_) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    // Set window size to half screen height
    float window_height = ImGui::GetIO().DisplaySize.y * 0.5f;
    ImGui::SetNextWindowSize(ImVec2(400, window_height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("GPU Profiler", open ? open : &show_window_, flags)) {
        ImGui::End();
        return;
    }

    const auto& results = profiler.get_smoothed_results();
    float total_ms = profiler.get_smoothed_total_ms();

    if (results.empty()) {
        ImGui::TextDisabled("No GPU timing data available");
        ImGui::End();
        return;
    }


    // Header: total GPU frame time
    ImGui::Text("GPU frame time: %.3f ms", total_ms);

    // Also show raw (unsmoothed) range
    {
        const auto& raw = profiler.get_results();
        float raw_total = profiler.get_total_frame_time_ms();
        ImGui::SameLine();
        ImGui::TextDisabled("(raw: %.3f ms)", raw_total);
    }

    ImGui::Separator();

    // --- Stacked horizontal bar chart ---
    const float bar_height = 24.0f;
    const float bar_width = ImGui::GetContentRegionAvail().x;
    constexpr float min_scale_ms = 10.0f;
    constexpr int tick_count = 10;
    const float scale_ms = std::isfinite(total_ms) ? std::max(total_ms, min_scale_ms) : min_scale_ms;

    // Draw two rows of bars like the reference image
    ImVec2 bar_start = ImGui::GetCursorScreenPos();

    // Draw bar background
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        bar_start,
        ImVec2(bar_start.x + bar_width, bar_start.y + bar_height),
        IM_COL32(20, 22, 35, 255));

    // Draw each pass as a colored segment
    float x_offset = 0.0f;
    for (size_t i = 0; i < results.size(); ++i) {
        const float time_ms = results[i].time_ms;
        if (!std::isfinite(time_ms) || time_ms < 0.0f) continue;
        float w = std::min(time_ms / scale_ms, 1.0f) * bar_width;
        if (w < 1.0f) w = 1.0f;

        ImVec4 col = kPassColors[i % kColorCount];
        ImU32 col32 = ImGui::ColorConvertFloat4ToU32(col);

        ImVec2 p0(bar_start.x + x_offset, bar_start.y);
        ImVec2 p1(bar_start.x + x_offset + w, bar_start.y + bar_height);
        draw_list->AddRectFilled(p0, p1, col32);

        // Draw border between segments
        draw_list->AddRect(p0, p1, IM_COL32(0, 0, 0, 100));

        x_offset += w;
    }

    const float label_y = bar_start.y + bar_height + 4.0f;
    for (int tick = 0; tick <= tick_count; ++tick) {
        const float fraction = static_cast<float>(tick) / tick_count;
        const float tick_x = bar_start.x + fraction * bar_width;
        draw_list->AddLine(
            ImVec2(tick_x, bar_start.y + bar_height - 4),
            ImVec2(tick_x, bar_start.y + bar_height),
            IM_COL32(200, 200, 200, 120));
        if (tick % 2 == 0) {
            char label[32];
            snprintf(label, sizeof(label), "%.3g", fraction * scale_ms);
            const float width = ImGui::CalcTextSize(label).x;
            const float left = std::clamp(tick_x - width * 0.5f, bar_start.x,
                                          bar_start.x + std::max(0.0f, bar_width - width));
            draw_list->AddText(ImVec2(left, label_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
        }
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height + 4.0f + ImGui::GetTextLineHeight()));
    ImGui::Separator();

    // --- Legend (color boxes + names + times) ---
    ImGui::BeginGroup();
    for (size_t i = 0; i < results.size(); ++i) {
        ImVec4 col = kPassColors[i % kColorCount];
        ImGui::ColorButton(("##color" + std::to_string(i)).c_str(), col,
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           ImVec2(12, 12));
        ImGui::SameLine();
        ImGui::Text("%-20s  %.3f ms", results[i].name.c_str(), results[i].time_ms);
    }
    ImGui::EndGroup();

    ImGui::End();
}
