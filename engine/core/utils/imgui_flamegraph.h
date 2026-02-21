#pragma once

// ImGui Flame Graph Widget
// Based on https://github.com/bwrsandman/imgui-flame-graph
// MIT License - Copyright(c) 2019 Sandy Carter

#include <imgui.h>
#include <climits>

namespace ImGuiFlameGraph {

// Plot a flame graph
// @param label - widget label
// @param values_getter - callback to get values for each entry
//   - start: start time in ms (relative to period start)
//   - end: end time in ms (relative to period start)
//   - level: depth level (0 = root)
//   - caption: display name
// @param data - user data passed to values_getter
// @param values_count - number of entries
// @param values_offset - offset for scrolling (0 for default)
// @param overlay_text - text overlay on graph
// @param scale_min - minimum scale (use FLT_MAX for auto)
// @param scale_max - maximum scale (use FLT_MAX for auto)
// @param graph_size - widget size (use ImVec2(0,0) for auto)
IMGUI_API void PlotFlame(
    const char* label,
    void (*values_getter)(float* start, float* end, ImU8* level, const char** caption, 
                          const void* data, int idx),
    const void* data,
    int values_count,
    int values_offset = 0,
    const char* overlay_text = nullptr,
    float scale_min = FLT_MAX,
    float scale_max = FLT_MAX,
    ImVec2 graph_size = ImVec2(0, 0));

} // namespace ImGuiFlameGraph
