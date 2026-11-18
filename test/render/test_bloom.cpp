#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/function/render/graph/rdg_pool.h"
#include "engine/function/render/render_pass/bloom_pass.h"
#include "engine/main/engine_context.h"
#include "engine/core/utils/path_utils.h"

#include <algorithm>
#include <array>
#include <thread>

namespace {

RHIShaderRef shader(const char* source, ShaderFrequency frequency, const char* profile) {
    auto backend = EngineContext::rhi();
    return backend->create_shader({.frequency = frequency, .code = backend->compile_shader(source, "main", profile)});
}

struct InputPipeline {
    RHIRootSignatureRef root;
    RHIGraphicsPipelineRef pipeline;
};

InputPipeline input_pipeline() {
    const auto vertex = shader(R"(
        float4 main(uint id : SV_VertexID) : SV_Position {
            float2 uv = float2((id << 1) & 2, id & 2);
            return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
        })", SHADER_FREQUENCY_VERTEX, "vs_5_0");
    const auto fragment = shader(R"(
        cbuffer Params : register(b0) { uint2 extent; float background; float peak; };
        float4 main(float4 position : SV_Position) : SV_Target {
            int2 offset = int2(position.xy) - int2(extent / 2);
            float color = all(abs(offset) <= 2) ? peak : background;
            return float4(color, color, color, 1);
        })", SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
    auto backend = EngineContext::rhi();
    RHIRootSignatureInfo layout;
    layout.add_push_constant({.size = 16, .frequency = SHADER_FREQUENCY_FRAGMENT});
    auto root = backend->create_root_signature(layout);
    RHIGraphicsPipelineInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.root_signature = root;
    info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    info.depth_stencil_state.enable_depth_test = false;
    info.depth_stencil_state.enable_depth_write = false;
    info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
    return {root, backend->create_graphics_pipeline(info)};
}

void draw_input(RDGBuilder& graph, RDGTextureHandle target, Extent2D extent, const InputPipeline& pipeline) {
    graph.create_render_pass("BloomTestInput")
        .color(0, target, ATTACHMENT_LOAD_OP_DONT_CARE, ATTACHMENT_STORE_OP_STORE)
        .execute([extent, pipeline = pipeline.pipeline](RDGPassContext context) {
            struct Parameters {
                uint32_t width;
                uint32_t height;
                float background = 0.125f;
                float peak = 0.9f;
            } parameters{extent.width, extent.height};
            context.command->set_graphics_pipeline(pipeline);
            context.command->set_viewport({0, 0}, {extent.width, extent.height});
            context.command->set_scissor({0, 0}, {extent.width, extent.height});
            context.command->push_constants(&parameters, sizeof(parameters), SHADER_FREQUENCY_FRAGMENT);
            context.command->draw(3, 1, 0, 0);
        })
        .finish();
}

} // namespace

TEST_CASE("Bloom compiles on the render thread and records owned frame data on the device thread", "[render][bloom][dx12]") {
    test_utils::TestContext::reset();
    {
        auto backend = EngineContext::rhi();
        backend->check_debug_messages();
        auto bloom = std::make_unique<render::BloomPass>();
        bloom->init();
        REQUIRE(bloom->is_ready());
        const auto input = input_pipeline();
        REQUIRE(input.root);
        REQUIRE(input.pipeline);

        struct Frame {
            Extent2D extent;
            render::BloomSettings settings;
            std::unique_ptr<RDGBuilder> graph;
            RDGTextureHandle output{UINT32_MAX};
            RHITextureInfo output_info{};
            std::vector<uint8_t> pixels;
            bool compiled = false;
            bool recorded = false;
            bool read_back = false;
            EngineContext::ThreadRole recording_thread = EngineContext::Unknown;
        };
        std::array<Frame, 4> frames;
        frames[0].extent = {65, 33};
        frames[0].settings = {true, 0.35f, 1.0f};
        frames[1].extent = {65, 33};
        frames[1].settings = {true, 2.0f, 1.0f};
        frames[2].extent = {65, 33};
        frames[2].settings = {true, 0.0f, 0.0f};
        frames[3].extent = {1, 1};
        frames[3].settings = {true, 0.1f, 0.5f};
        std::jthread render_thread([&] {
            EngineContext::set_thread_role(EngineContext::Renderer);
            for (auto& frame : frames) {
                frame.graph = std::make_unique<RDGBuilder>();
                auto& graph = *frame.graph;
                const auto extent = frame.extent;
                auto source = graph.create_texture("SceneColor").extent({extent.width, extent.height, 1})
                    .format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
                frame.output_info = {.format = FORMAT_R8G8B8A8_UNORM, .extent = {extent.width, extent.height, 1},
                    .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET};
                frame.output = graph.create_texture("FinalColor").import(frame.output_info, RESOURCE_STATE_UNDEFINED).finish();
                draw_input(graph, source, extent, input);
                if (!bloom->build(graph, source, frame.output, extent, frame.settings)) return;
                graph.create_compute_pass("RecordThread").never_cull().execute([&frame](RDGPassContext) {
                    frame.recording_thread = EngineContext::get_thread_role();
                }).finish();
                frame.compiled = graph.compile();
                // Recorded callbacks must keep the parameters used while building this frame.
                frame.settings.threshold = 100.0f;
                frame.settings.intensity = 0.0f;
            }
        });
        render_thread.join();
        REQUIRE(std::all_of(frames.begin(), frames.end(), [](const auto& frame) { return frame.compiled; }));
        bloom.reset();

        std::jthread device_thread([&] {
            EngineContext::set_thread_role(EngineContext::Device);
            for (uint32_t i = 0; i < frames.size(); ++i) {
                auto& frame = frames[i];
                auto target = backend->create_texture(frame.output_info);
                auto pool = backend->create_command_pool({backend->get_queue({QUEUE_TYPE_GRAPHICS, 0})});
                auto command = pool->create_command_list(true);
                if (!target || !command || !frame.graph->bind_external_texture(frame.output, target)) return;
                command->begin_command();
                frame.recorded = frame.graph->execute_compiled(command, i % FRAMES_IN_FLIGHT);
                command->end_command();
                command->execute();
                frame.pixels.resize(size_t(frame.extent.width) * frame.extent.height * 4);
                auto readback = backend->create_command_context(nullptr);
                frame.read_back = readback->read_texture(target, frame.pixels.data(), static_cast<uint32_t>(frame.pixels.size()));
            }
        });
        device_thread.join();
        for (const auto& frame : frames) {
            REQUIRE(frame.recorded);
            REQUIRE(frame.read_back);
            CHECK(frame.recording_thread == EngineContext::Device);
        }
        auto pixel = [&](size_t frame, uint32_t x, uint32_t y) {
            return frames[frame].pixels[(y * frames[frame].extent.width + x) * 4];
        };
        CHECK(pixel(0, 39, 16) > pixel(1, 39, 16) + 5);
        CHECK(pixel(0, 32, 23) > pixel(1, 32, 23) + 5);
        CHECK(std::abs(int(pixel(0, 0, 0)) - 32) <= 1);
        CHECK(std::abs(int(pixel(1, 32, 16)) - 230) <= 1);
        CHECK(std::abs(int(pixel(2, 32, 16)) - 230) <= 1);
        CHECK(pixel(3, 0, 0) > 230);
        int max_difference = 0;
        for (size_t i = 0; i < frames[1].pixels.size(); ++i) {
            max_difference = std::max(max_difference, std::abs(int(frames[1].pixels[i]) - int(frames[2].pixels[i])));
        }
        CHECK(max_difference <= 1);
        const auto results = utils::get_engine_path() / "build/test-results";
        REQUIRE(test_utils::save_screenshot_png((results / "bloom-enabled.png").string(), 65, 33, frames[0].pixels));
        REQUIRE(test_utils::save_screenshot_png((results / "bloom-disabled.png").string(), 65, 33, frames[1].pixels));
        CHECK_FALSE(backend->check_debug_messages("bloom on render and device threads"));
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Bloom runs through the renderer with late back buffer binding", "[render][bloom][dx12]") {
    test_utils::TestContext::reset();
    auto* renderer = EngineContext::render_system();
    const auto input = input_pipeline();
    REQUIRE(input.pipeline);
    renderer->bloom_settings_ = {true, 0.35f, 1.0f};
    auto first = renderer->make_render_packet(nullptr, nullptr, 0);
    renderer->bloom_settings_.enabled = false;
    // A view enables the custom scene pass without accessing ECS during graph construction.
    first.camera = RenderCamera{Mat4::Identity(), Mat4::Identity(), Vec3::Zero(), 0.1f, 100.0f};
    first.depth_prepass = false;
    first.pbr = false;
    first.npr = false;
    renderer->set_custom_rdg_build_func([input](RDGBuilder& graph, const RenderPacket&) {
        draw_input(graph, graph.get_texture("SceneColor"), WINDOW_EXTENT, input);
    });
    auto frame = renderer->build_frame(first);
    renderer->clear_custom_rdg_build_func();
    REQUIRE(frame);
    REQUIRE(renderer->execute_frame(std::move(*frame)));
    std::vector<uint8_t> pixels;
    Extent2D extent;
    REQUIRE(renderer->capture_frame(pixels, extent));
    const auto halo = ((extent.height / 2) * extent.width + extent.width / 2 + 7) * 4;
    CHECK(pixels[halo] > 37);
    CHECK_FALSE(EngineContext::rhi()->check_debug_messages("bloom renderer integration"));
    test_utils::TestContext::reset();
}
