#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "engine/function/render/render_system/gpu_profiler.h"
#include "engine/function/render/render_system/gpu_profiler_widget.h"
#include "engine/main/engine_context.h"
#include "test/test_utils.h"

TEST_CASE("GPU profiler draws a bounded chart for large timings", "[gpu_profiler]") {
    test_utils::TestContext::reset();

    class TestProfiler final : public GPUProfiler {
    public:
        TestProfiler() {
            smoothed_total_ms_ = 1'000'000.0f;
            smoothed_results_.push_back({"Render pass", smoothed_total_ms_});
        }

        void destroy() override {}
        void begin_frame() override {}
        void end_frame() override {}
        void begin_scope(const std::string&) override {}
        void end_scope() override {}
        void collect_results() override {}
    } profiler;

    auto backend = EngineContext::rhi();
    REQUIRE(backend);
    REQUIRE(ImGui::GetCurrentContext());

    // A newly created ImGui window may skip rendering for its first frame.
    for (int frame = 0; frame < 2; ++frame) {
        backend->imgui_new_frame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(450, 300));
        GPUProfilerWidget::draw_window(profiler);
        ImGui::Render();
    }

    REQUIRE(ImGui::GetDrawData()->TotalVtxCount > 0);
    REQUIRE(ImGui::GetDrawData()->TotalVtxCount < 10'000);
    test_utils::TestContext::reset();
}
