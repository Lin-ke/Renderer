#include "profiler_widget.h"
#include "cpu_profiler.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>

// ============================================================================
// Static state
// ============================================================================
bool  ProfilerWidget::show_window_  = false;
float ProfilerWidget::zoom_         = 1.0f;
float ProfilerWidget::pan_x_        = 0.0f;
int   ProfilerWidget::selected_bar_ = -1;

// ============================================================================
// Colour helpers
// ============================================================================
static ImU32 depth_color(uint32_t depth, bool hovered) {
    // Cycle through visually distinct hues per depth
    static const ImVec4 palette[] = {
        {0.30f, 0.75f, 0.40f, 0.85f},  // green
        {0.40f, 0.70f, 0.85f, 0.85f},  // teal
        {0.55f, 0.65f, 0.35f, 0.85f},  // olive
        {0.70f, 0.80f, 0.30f, 0.85f},  // lime
        {0.35f, 0.55f, 0.70f, 0.85f},  // steel blue
        {0.60f, 0.75f, 0.50f, 0.85f},  // sage
    };
    constexpr int N = sizeof(palette) / sizeof(palette[0]);
    ImVec4 c = palette[depth % N];
    if (hovered) { c.x += 0.15f; c.y += 0.15f; c.z += 0.15f; }
    return ImGui::ColorConvertFloat4ToU32(c);
}

static ImU32 depth_outline(uint32_t depth, bool hovered) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(depth_color(depth, hovered));
    c.x *= 0.6f; c.y *= 0.6f; c.z *= 0.6f; c.w = 1.0f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// ============================================================================
// draw_window
// ============================================================================
void ProfilerWidget::draw_window(bool* open) {
    if (!open && !show_window_) return;
    bool& wnd = open ? *open : show_window_;
    if (!wnd) return;

    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("CPU Profiler", &wnd)) {
        draw_header();
        ImGui::Separator();
        draw_frame_history();
        ImGui::Separator();

        const auto& frame = CpuProfiler::instance().get_display_frame();
        if (!frame.threads.empty()) {
            draw_flame_chart(frame);
        } else {
            ImGui::TextDisabled("No profiling data");
        }
    }
    ImGui::End();
}

// ============================================================================
// draw_header – FPS / frame time / avg FPS / pause / threshold / level
// ============================================================================
void ProfilerWidget::draw_header() {
    auto& prof = CpuProfiler::instance();

    float fps   = prof.get_current_fps();
    float ft    = prof.get_current_frame_time();
    float avg   = prof.get_average_fps();

    // FPS colour: green > 60, yellow > 30, red otherwise
    ImVec4 fps_col = (fps >= 60.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                   : (fps >= 30.0f) ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                                    : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    ImVec4 ft_col  = (ft <= 16.67f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                   : (ft <= 33.33f) ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                                    : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

    ImGui::Text("FPS:");  ImGui::SameLine();
    ImGui::TextColored(fps_col, "%.1f", fps);
    ImGui::SameLine(0, 20);
    ImGui::Text("Frame time:");  ImGui::SameLine();
    ImGui::TextColored(ft_col, "%.3f ms", ft);
    ImGui::SameLine(0, 20);
    ImGui::Text("Average FPS:");  ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%.1f", avg);

    // Pause toggle
    ImGui::SameLine(0, 30);
    bool paused = prof.is_paused();
    if (ImGui::Checkbox("Pause captures", &paused)) {
        prof.set_paused(paused);
        if (!paused) {
            prof.select_latest_frame();
            selected_bar_ = -1;
        }
    }

    // Threshold + Level
    float thr = prof.get_threshold_ms();
    uint32_t lvl = prof.get_threshold_level();
    ImGui::PushItemWidth(180);
    if (ImGui::SliderFloat("Threshold", &thr, 0.0f, 100.0f, "%.3f ms")) {
        prof.set_threshold(thr, lvl);
    }
    ImGui::SameLine();
    int lvl_i = static_cast<int>(lvl);
    if (ImGui::SliderInt("Level", &lvl_i, 0, 10)) {
        prof.set_threshold(thr, static_cast<uint32_t>(lvl_i));
    }
    ImGui::PopItemWidth();

    // Reset zoom
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Reset zoom and pan")) {
        zoom_ = 1.0f;
        pan_x_ = 0.0f;
    }
}

// ============================================================================
// draw_frame_history – yellow bar chart of last N frame times
// ============================================================================
void ProfilerWidget::draw_frame_history() {
    auto& prof = CpuProfiler::instance();
    const auto& times = prof.get_frame_times();
    size_t head  = prof.get_frame_history_head();
    size_t count = prof.get_frame_history_count();
    if (count == 0) return;

    // Build a linear array oldest->newest
    std::vector<float> linear(count);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (head + CpuProfiler::FRAME_HISTORY_MAX - count + i) % CpuProfiler::FRAME_HISTORY_MAX;
        linear[i] = times[idx];
    }

    float max_time = *std::max_element(linear.begin(), linear.end());
    if (max_time < 1.0f) max_time = 1.0f;

    float avail_w = ImGui::GetContentRegionAvail().x;
    float bar_h   = 50.0f;

    ImVec2 cursor  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(cursor, ImVec2(cursor.x + avail_w, cursor.y + bar_h),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));

    float bar_w = avail_w / static_cast<float>(CpuProfiler::FRAME_HISTORY_MAX);
    if (bar_w < 1.0f) bar_w = 1.0f;

    ImVec2 mouse = ImGui::GetMousePos();
    int hovered_bar = -1;

    for (size_t i = 0; i < count; ++i) {
        float x0 = cursor.x + static_cast<float>(i) * bar_w;
        float frac = linear[i] / max_time;
        float h = frac * (bar_h - 2.0f);
        float y0 = cursor.y + bar_h - h - 1.0f;
        float y1 = cursor.y + bar_h - 1.0f;

        bool bar_hov = (mouse.x >= x0 && mouse.x < x0 + bar_w &&
                        mouse.y >= cursor.y && mouse.y < cursor.y + bar_h);
        if (bar_hov) hovered_bar = static_cast<int>(i);

        ImU32 col;
        if (static_cast<int>(i) == selected_bar_) {
            col = IM_COL32(255, 100, 100, 255);
        } else if (bar_hov) {
            col = IM_COL32(255, 255, 150, 255);
        } else {
            col = IM_COL32(230, 200, 50, 255);
        }

        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bar_w - 1.0f, y1), col);
    }

    // Tooltip on hover
    if (hovered_bar >= 0) {
        ImGui::SetTooltip("Frame %d: %.3f ms (%.1f FPS)",
            hovered_bar, linear[hovered_bar],
            linear[hovered_bar] > 0.0f ? 1000.0f / linear[hovered_bar] : 0.0f);

        // Click to select
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Map bar index to history deque index
            size_t hist_size = prof.get_history_size();
            if (hist_size > 0 && static_cast<size_t>(hovered_bar) < hist_size) {
                // The bar chart and history deque should be in the same order
                // But history may be shorter than count if paused
                size_t deque_idx = static_cast<size_t>(hovered_bar);
                // Clamp
                if (deque_idx >= hist_size) deque_idx = hist_size - 1;
                prof.set_paused(true);
                prof.select_history_frame(deque_idx);
                selected_bar_ = hovered_bar;
            }
        }
    }

    ImGui::Dummy(ImVec2(avail_w, bar_h));
}

// ============================================================================
// draw_flame_chart – per-thread flame graphs
// ============================================================================
void ProfilerWidget::draw_flame_chart(const CpuProfileFrame& frame) {
    ImGui::BeginChild("FlameChartScroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    float avail_w = ImGui::GetContentRegionAvail().x;
    float block_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;

    // Zoom / pan via mouse
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float old_zoom = zoom_;
            zoom_ *= (wheel > 0) ? 1.15f : (1.0f / 1.15f);
            zoom_ = (zoom_ < 0.1f) ? 0.1f : (zoom_ > 100.0f) ? 100.0f : zoom_;

            // Zoom towards mouse
            float mouse_frac = (ImGui::GetMousePos().x - ImGui::GetCursorScreenPos().x + pan_x_) / (avail_w * old_zoom);
            pan_x_ = mouse_frac * avail_w * zoom_ - (ImGui::GetMousePos().x - ImGui::GetCursorScreenPos().x);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan_x_ -= ImGui::GetIO().MouseDelta.x;
        }
    }
    pan_x_ = (std::max)(pan_x_, 0.0f);

    float total_w = avail_w * zoom_;

    for (const auto& td : frame.threads) {
        if (td.scopes.empty()) continue;

        // Thread header
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.35f, 1.0f));
        std::string header_label = td.name + "  (0x" + [&]{
            std::ostringstream o; o << std::hex << td.thread_id; return o.str();
        }() + ")";
        if (ImGui::CollapsingHeader(header_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            draw_thread_flame(td, frame, total_w, block_h);
        }
        ImGui::PopStyleColor();
    }

    // Total frame time
    ImGui::Separator();
    ImGui::Text("Frame #%u  |  Total: %.3f ms", frame.frame_number, frame.duration_ms);

    ImGui::EndChild();
}

// ============================================================================
// draw_thread_flame – flame graph for one thread
// ============================================================================
void ProfilerWidget::draw_thread_flame(
    const CpuProfileThreadData& td,
    const CpuProfileFrame& frame,
    float total_width,
    float block_height)
{
    if (td.scopes.empty()) return;

    // Determine max depth
    uint32_t max_depth = 0;
    for (const auto& s : td.scopes) {
        max_depth = (std::max)(max_depth, s.depth);
    }

    float flame_h = block_height * (max_depth + 1);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x -= pan_x_;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    ImVec2 bg_min = ImVec2(ImGui::GetCursorScreenPos().x, origin.y);
    ImVec2 bg_max = ImVec2(bg_min.x + ImGui::GetContentRegionAvail().x, origin.y + flame_h);
    dl->AddRectFilled(bg_min, bg_max, IM_COL32(30, 30, 40, 200));

    uint64_t frame_duration = frame.end_ticks - frame.start_ticks;
    if (frame_duration == 0) {
        ImGui::Dummy(ImVec2(0, flame_h));
        return;
    }

    ImVec2 mouse_pos = ImGui::GetMousePos();

    for (const auto& scope : td.scopes) {
        double rel_start = static_cast<double>(scope.start_ticks - frame.start_ticks) / static_cast<double>(frame_duration);
        double rel_end   = static_cast<double>(scope.end_ticks   - frame.start_ticks) / static_cast<double>(frame_duration);

        float x0 = origin.x + static_cast<float>(rel_start * total_width);
        float x1 = origin.x + static_cast<float>(rel_end   * total_width);
        float y0 = origin.y + static_cast<float>(scope.depth) * block_height;
        float y1 = y0 + block_height;

        // Clip
        float vis_left  = bg_min.x;
        float vis_right = bg_max.x;
        if (x1 < vis_left || x0 > vis_right) continue;

        float cx0 = (std::max)(x0, vis_left);
        float cx1 = (std::min)(x1, vis_right);
        if (cx1 - cx0 < 1.0f) cx1 = cx0 + 1.0f;

        bool hovered = (mouse_pos.x >= cx0 && mouse_pos.x <= cx1 &&
                        mouse_pos.y >= y0  && mouse_pos.y <= y1);

        ImU32 fill = depth_color(scope.depth, hovered);
        ImU32 line_col = depth_outline(scope.depth, hovered);

        dl->AddRectFilled(ImVec2(cx0, y0), ImVec2(cx1, y1), fill);
        dl->AddRect(ImVec2(cx0, y0), ImVec2(cx1, y1), line_col);

        // Text inside block
        float box_w = cx1 - cx0;
        if (scope.name) {
            const char* label = scope.name;
            ImVec2 text_size = ImGui::CalcTextSize(label);
            if (text_size.x + 4.0f < box_w) {
                float tx = cx0 + (box_w - text_size.x) * 0.5f;
                float ty = y0 + (block_height - text_size.y) * 0.5f;
                dl->AddText(ImVec2(tx, ty), IM_COL32(0, 0, 0, 255), label);
            }
        }

        // Tooltip
        if (hovered) {
            float dur_ms = scope.duration_ms(frame.cpu_frequency);
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f), "%s", scope.name ? scope.name : "(unknown)");
            ImGui::Separator();
            ImGui::Text("Time:  %.3f ms", dur_ms);
            if (scope.file) {
                ImGui::Text("File:  %s", scope.file);
            }
            if (scope.line > 0) {
                ImGui::Text("Line:  %u", scope.line);
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::Dummy(ImVec2(0, flame_h));
}
