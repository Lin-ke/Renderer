#pragma once

#include "cpu_profiler.h"

/**
 * @brief ImGui widget for CPU profiler – rprof-style visualization
 *
 * Features:
 *   - Frame inspector header (FPS / frame-time / average FPS)
 *   - Pause captures toggle
 *   - Threshold & Level sliders
 *   - Frame history bar chart (click to select a frame)
 *   - Per-thread flame chart with zoom / pan
 *   - Hover tooltip: name, time (ms), file, line
 */
class ProfilerWidget {
public:
    /// Draw the full profiler window (call each ImGui frame)
    static void draw_window(bool* open = nullptr);

    static void toggle_visibility() { show_window_ = !show_window_; }
    static bool is_visible()        { return show_window_; }

private:
    // --- sub-draw helpers ---
    static void draw_header();
    static void draw_frame_history();
    static void draw_flame_chart(const CpuProfileFrame& frame);
    static void draw_thread_flame(const CpuProfileThreadData& td, const CpuProfileFrame& frame,
                                  float total_width, float block_height);

    // --- state ---
    static bool  show_window_;
    static float zoom_;           // horizontal zoom
    static float pan_x_;          // horizontal pan (pixels)
    static int   selected_bar_;   // index into history ring (-1 = latest)
};
