#pragma once

#include "time_scope.h"
#include <map>
#include <memory>
#include <thread>

/**
 * @brief ImGui widget for displaying profiler flame graphs
 */
class ProfilerWidget {
public:
    /**
     * @brief Draw the profiler flame graph UI
     * @param time_scopes - map of thread id to TimeScopes
     */
    static void draw_flame_graph(const std::map<std::thread::id, std::shared_ptr<TimeScopes>>& time_scopes);
    
    /**
     * @brief Draw the profiler window (call from your ImGui render code)
     */
    static void draw_window(bool* open = nullptr);
    
    /**
     * @brief Toggle profiler window visibility
     */
    static void toggle_visibility() { show_window_ = !show_window_; }
    
    /**
     * @brief Check if profiler window is visible
     */
    static bool is_visible() { return show_window_; }

private:
    static bool show_window_;
};
