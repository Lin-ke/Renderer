#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/core/utils/profile_session.h"
#include "engine/core/utils/profiler.h"
#include "engine/core/utils/profile_probe.h"
#include "engine/core/utils/path_utils.h"
#include "engine/core/os/thread_pool.h"
#include "engine/main/engine_context.h"
#include <cereal/external/rapidjson/document.h>
#include <fstream>
#include <iterator>

TEST_CASE("A profile report captures loading, real GPU work, memory and its render graph", "[render][profile]") {
    test_utils::TestContext::reset();
    auto& profile = ProfileSession::instance();
    REQUIRE(profile.begin("FunctionalCapture"));
    auto operation = profile.begin_operation("LoadTestScene");
    {
        ProfileContextScope operation_scope(operation.context);
        auto task = EngineContext::thread_pool()->enqueue([] {
            PROFILE_SCOPE("LoadWorker");
            std::pmr::vector<uint32_t> data(memory::resource(memory::Category::Asset));
            data.resize(1024, 42);
            ProfileSession::instance().sample_memory("WorkerAllocated", {});
            return data.back();
        });
        REQUIRE(task.get() == 42);
        auto* renderer = EngineContext::render_system();
        auto backend = EngineContext::rhi();
        auto make_shader = [&](const char* source, ShaderFrequency frequency, const char* target) {
            return backend->create_shader({.frequency = frequency, .code = backend->compile_shader(source, "main", target)});
        };
        const auto vertex = make_shader(R"(
            float4 main(uint id : SV_VertexID) : SV_Position {
                float2 uv = float2((id << 1) & 2, id & 2);
                return float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
            })", SHADER_FREQUENCY_VERTEX, "vs_5_0");
        const auto fragment = make_shader("float4 main() : SV_Target { return float4(0,0,0,1); }",
            SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
        RHIGraphicsPipelineInfo info{};
        info.vertex_shader = vertex; info.fragment_shader = fragment;
        info.root_signature = backend->create_root_signature({});
        info.rasterizer_state.cull_mode = CULL_MODE_NONE;
        info.depth_stencil_state.enable_depth_test = false;
        info.depth_stencil_state.enable_depth_write = false;
        info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
        const auto pipeline = backend->create_graphics_pipeline(info);
        REQUIRE(pipeline);
        renderer->set_custom_rdg_build_func([pipeline](RDGBuilder& graph, const RenderPacket&) {
            graph.create_render_pass("ProfileDraw")
                .color(0, graph.get_texture("BackBuffer"), ATTACHMENT_LOAD_OP_DONT_CARE, ATTACHMENT_STORE_OP_STORE)
                .execute([pipeline](RDGPassContext context) {
                    context.command->set_graphics_pipeline(pipeline);
                    context.command->set_viewport({0,0}, {WINDOW_EXTENT.width, WINDOW_EXTENT.height});
                    context.command->set_scissor({0,0}, {WINDOW_EXTENT.width, WINDOW_EXTENT.height});
                    context.command->draw(3,1,0,0);
                }).finish();
        });
        auto packet = renderer->make_render_packet(nullptr, nullptr, 30);
        packet.camera = RenderCamera{Mat4::Identity(), Mat4::Identity(), Vec3::Zero(), 0.1f, 100};
        packet.depth_prepass = packet.pbr = packet.npr = false;
        ProfileProbe::set_enabled(true);
        REQUIRE(renderer->tick(packet));
        ProfileProbe::set_enabled(false);
        renderer->clear_custom_rdg_build_func();
        std::vector<uint8_t> pixels;
        Extent2D extent;
        REQUIRE(renderer->capture_frame(pixels, extent));
        CHECK(test_utils::image_matches(pixels, extent.width, [](auto, auto) { return std::array<uint8_t,4>{0,0,0,255}; }));
        EngineContext::run_device_task([&] {
            profile.sample_memory("FrameComplete", EngineContext::rhi()->get_memory_snapshot());
        });
    }
    profile.end_operation(operation);
    profile.end();
    const auto path = utils::get_engine_path() / "build/test-results/functional-profile.json";
    REQUIRE(profile.write(path));
    std::ifstream file(path);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    CEREAL_RAPIDJSON_NAMESPACE::Document report;
    report.Parse(text.c_str());
    REQUIRE_FALSE(report.HasParseError());
    const auto& events = report["traceEvents"];
    bool correlated_worker = false;
    for (const auto& event : events.GetArray()) {
        if (std::string_view(event["name"].GetString()) == "LoadWorker")
            correlated_worker = event["args"]["operation_id"].GetUint64() == operation.context.operation_id;
    }
    CHECK(correlated_worker);
    REQUIRE(report["graphs"].Size() == 1);
    CHECK(report["graphs"][0]["frame_id"].GetUint64() == 30);
    REQUIRE(report["gpu_frames"].Size() == 1);
    CHECK(report["gpu_frames"][0]["frame_id"].GetUint64() == 30);
    CHECK(report["gpu_frames"][0]["gpu_ms"].GetDouble() > 0);
    const auto& passes = report["gpu_frames"][0]["passes"];
    const CEREAL_RAPIDJSON_NAMESPACE::Value* statistics = nullptr;
    for (const auto& pass : passes.GetArray()) {
        if (std::string_view(pass["name"].GetString()) == "ProfileDraw")
            statistics = &pass["pipeline_statistics"];
    }
    REQUIRE(statistics);
    REQUIRE((*statistics)["available"].GetBool());
    CHECK((*statistics)["vertex_invocations"].GetUint64() == 3);
    CHECK((*statistics)["pixel_invocations"].GetUint64() > 0);
    CHECK_FALSE(EngineContext::rhi()->check_debug_messages("profile pipeline statistics"));
    CHECK(report["memory"]["samples"][0]["tracked"]["current_bytes"].GetUint64() >= 4096);
    CHECK(report["memory"]["samples"][1]["gpu"]["allocated_bytes"].GetUint64() > 0);
    CHECK(report["dropped_cpu_events"].GetUint64() == 0);
    test_utils::TestContext::reset();
}
