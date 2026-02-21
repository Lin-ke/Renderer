#include "profiler_widget.h"
#include "imgui_flamegraph.h"
#include "profiler.h"
#include <algorithm>
#include <array>
#include <sstream>

bool ProfilerWidget::show_window_ = false;

// Global period for time synchronization across all threads
static std::array<TimePoint, 2> g_period = {};

static void profiler_value_getter(float* start_timestamp, float* end_timestamp, 
                                   ImU8* level, const char** caption, 
                                   const void* data, int idx) {
    auto scopes = static_cast<const TimeScopes*>(data);
    const auto& scope_list = scopes->get_scopes();
    
    if (idx < 0 || idx >= static_cast<int>(scope_list.size())) {
        return;
    }
    
    const auto& scope = scope_list[idx];
    const auto& first_scope = scope_list[0];

    if (start_timestamp) {
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(
            scope->get_begin_time() - g_period[0]);
        *start_timestamp = static_cast<float>(time.count()) / 1000.0f; // Convert to ms
    }
    if (end_timestamp) {
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(
            scope->get_end_time() - g_period[0]);
        *end_timestamp = static_cast<float>(time.count()) / 1000.0f; // Convert to ms
    }
    if (level) {
        *level = static_cast<ImU8>(scope->get_depth());
    }
    if (caption) {
        *caption = scope->name.c_str();
    }
}

void ProfilerWidget::draw_flame_graph(const std::map<std::thread::id, std::shared_ptr<TimeScopes>>& time_scopes) {
    if (time_scopes.empty()) {
        ImGui::Text("No profiling data available");
        return;
    }
    
    g_period = {};
    bool init = false;
    
    // Calculate the global time period across all threads
    for (const auto& [thread_id, scopes] : time_scopes) {
        if (scopes && !scopes->get_scopes().empty()) {
            const auto& begin = scopes->get_begin_time();
            const auto& end = scopes->get_end_time();
            
            g_period[0] = init ? std::min(begin, g_period[0]) : begin;
            g_period[1] = init ? std::max(end, g_period[1]) : end;
            init = true;
        }
    }
    
    if (!init) {
        ImGui::Text("No profiling data available");
        return;
    }
    
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(g_period[1] - g_period[0]);
    float scale_max = static_cast<float>(time.count()) / 1000.0f; // Convert to ms

    // Draw flame graph for each thread
    for (const auto& [thread_id, scopes] : time_scopes) {
        if (!scopes || scopes->get_scopes().empty()) {
            continue;
        }
        
        // Format thread name
        std::stringstream ss;
        ss << thread_id;
        std::string thread_name = "[" + ss.str() + "] Thread";
        
        // Draw the flame graph for this thread
        ImGuiFlameGraph::PlotFlame(
            "", 
            profiler_value_getter, 
            scopes.get(), 
            static_cast<int>(scopes->size()),
            0,
            thread_name.c_str(), 
            0.0f, 
            scale_max, 
            ImVec2(0, 0));
        
        ImGui::Spacing();
    }
    
    // Show total frame time
    ImGui::Separator();
    ImGui::Text("Total Frame Time: %.4f ms", scale_max);
}

void ProfilerWidget::draw_window(bool* open) {
    if (!open && !show_window_) {
        return;
    }
    
    bool window_open = open ? *open : show_window_;
    if (!window_open) {
        return;
    }
    
    if (ImGui::Begin("Profiler", open ? open : &show_window_)) {
        // Controls
        if (ImGui::Button("Clear")) {
            Profiler::get().clear();
        }
        ImGui::SameLine();
        
        bool enabled = Profiler::get().is_enabled();
        if (ImGui::Checkbox("Enabled", &enabled)) {
            Profiler::get().set_enabled(enabled);
        }
        
        ImGui::Separator();
        
        // Flame graph
        const auto& all_scopes = Profiler::get().get_all_scopes();
        draw_flame_graph(all_scopes);
    }
    ImGui::End();
}
