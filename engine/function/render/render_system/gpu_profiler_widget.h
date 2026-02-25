#pragma once

class GPUProfiler;

/**
 * @brief ImGui widget for displaying GPU pass timing as a stacked bar chart.
 *
 * Renders a compact overlay similar to commercial GPU profilers:
 * - Horizontal stacked bar showing per-pass GPU time
 * - Color-coded legend
 * - CPU draw cost annotation
 */
class GPUProfilerWidget {
public:
    static void draw_window(GPUProfiler& profiler, bool* open = nullptr);
    static void toggle_visibility() { show_window_ = !show_window_; }
    static bool is_visible() { return show_window_; }

private:
    static bool show_window_;
};
