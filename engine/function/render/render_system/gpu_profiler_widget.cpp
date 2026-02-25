#include "gpu_profiler_widget.h"
#include "gpu_profiler.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

bool GPUProfilerWidget::show_window_ = false;

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

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

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

    // Find min/max range for display
    float min_ms = 0.0f;
    float max_ms = total_ms;

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
    const float scale_ms = 10.0f; // Scale: 10ms = full width (adjustable)
    float effective_scale = std::max(total_ms, scale_ms);

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
        float w = (results[i].time_ms / effective_scale) * bar_width;
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

    // Draw time scale ticks
    for (int ms_tick = 1; ms_tick <= (int)effective_scale; ++ms_tick) {
        float tick_x = bar_start.x + (ms_tick / effective_scale) * bar_width;
        draw_list->AddLine(
            ImVec2(tick_x, bar_start.y + bar_height - 4),
            ImVec2(tick_x, bar_start.y + bar_height),
            IM_COL32(200, 200, 200, 120));
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height + 2));

    // --- Scale axis labels ---
    {
        char buf[32];
        for (int ms_tick = 0; ms_tick <= (int)effective_scale; ms_tick += std::max(1, (int)(effective_scale / 10))) {
            float tick_x = (ms_tick / effective_scale) * bar_width;
            snprintf(buf, sizeof(buf), "%d", ms_tick);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tick_x);
            ImGui::TextDisabled("%s", buf);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        float center_x = bar_width * 0.45f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x);
        ImGui::TextDisabled("ms");
    }

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
