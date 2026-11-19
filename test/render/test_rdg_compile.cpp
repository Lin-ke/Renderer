#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/graph/rdg_pool.h"

namespace {
RDGTextureHandle texture(RDGBuilder& graph, const std::string& name) {
    return graph.create_texture(name).extent({13,7,1}).format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
}
RHICommandListRef commands(bool bypass = true) {
    auto backend = EngineContext::rhi();
    auto pool = backend->create_command_pool({backend->get_queue({QUEUE_TYPE_GRAPHICS,0})});
    return pool->create_command_list(bypass);
}
}

TEST_CASE("Compiled RDG binds an external texture before recording", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    {
        auto backend = EngineContext::rhi();
        const RHITextureInfo info{
            .format = FORMAT_R8G8B8A8_UNORM,
            .extent = {13, 7, 1},
            .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET
        };
        auto target = backend->create_texture(info);
        REQUIRE(target);

        RDGBuilder graph;
        auto external = graph.create_texture("External").import(target->get_info(), RESOURCE_STATE_UNDEFINED).finish();
        unsigned recorded_slot = UINT32_MAX;
        graph.create_render_pass("Clear")
            .color(0, external, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0, 1, 0, 1})
            .execute([&](RDGPassContext context) { recorded_slot = context.frame_slot; })
            .finish();
        REQUIRE(graph.compile());
        auto command = commands();
        REQUIRE(graph.bind_external_texture(external, target));
        command->begin_command();
        REQUIRE(graph.execute_compiled(command, 1));
        command->end_command();
        command->execute();
        REQUIRE(recorded_slot == 1);

        std::vector<uint8_t> pixels(13 * 7 * 4);
        auto readback = backend->create_command_context(nullptr);
        REQUIRE(readback->read_texture(target, pixels.data(), static_cast<uint32_t>(pixels.size())));
        CHECK(pixels[0] == 0);
        CHECK(pixels[1] == 255);
        CHECK(pixels[2] == 0);
    }
    test_utils::TestContext::reset();
}

TEST_CASE("RDG reordered copy chain preserves GPU output and last use", "[rdg][rdg_compile][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET}); REQUIRE(output);
    for (bool bypass : {true,false}) {
        auto command = commands(bypass); command->begin_command();
        RDGBuilder graph;
        auto src = texture(graph,"Source"), intermediate = texture(graph,"Intermediate"), dead = texture(graph,"Dead");
        auto dst = graph.create_texture("External").import(output,RESOURCE_STATE_UNDEFINED).finish();
        graph.create_copy_pass("FirstCopy").from(src).to(intermediate);
        graph.create_copy_pass("FinalCopy").from(intermediate).to(dst);
        graph.create_render_pass("ClearProducer").color(0,src,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,{0.25f,0.5f,0.75f,1});
        graph.create_render_pass("DeadClear").color(0,dead,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
        REQUIRE(graph.execute(command)); command->end_command(); command->execute();
        std::vector<uint8_t> pixels(13*7*4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(output,pixels.data(),uint32_t(pixels.size())));
        CHECK(test_utils::image_matches(pixels, 13, [](auto, auto) { return std::array<uint8_t,4>{64,128,191,255}; }));
        REQUIRE_FALSE(backend->check_debug_messages("RDG reordered copies"));
    }
    test_utils::TestContext::reset();
}

TEST_CASE("RDG exported resources survive execution without pool reuse", "[rdg][rdg_compile][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    RHITextureRef outputs[2];
    for (int i = 0; i < 2; ++i) {
        auto command = commands(); command->begin_command();
        RDGBuilder graph;
        auto color = texture(graph,"Export");
        graph.create_render_pass("WriteExport").color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,
            i == 0 ? Color4{1,0,0,1} : Color4{0,1,0,1});
        REQUIRE(graph.extract_texture(color,outputs[i]));
        REQUIRE(graph.execute(command)); command->end_command(); command->execute();
        REQUIRE(outputs[i]);
    }
    REQUIRE(outputs[0].get() != outputs[1].get());
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> pixels(13*7*4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(outputs[i],pixels.data(),uint32_t(pixels.size())));
        CHECK(test_utils::image_matches(pixels, 13, [i](auto, auto) { return std::array<uint8_t,4>{uint8_t(i == 0 ? 255 : 0),uint8_t(i == 0 ? 0 : 255),0,255}; }));
    }
    REQUIRE_FALSE(backend->check_debug_messages("RDG exports"));
    test_utils::TestContext::reset();
}

TEST_CASE("RDG frame preparation rejects undeclared GPU access", "[rdg][rdg_compile][dx12]") {
    test_utils::TestContext::reset();
    auto buffer = EngineContext::rhi()->create_buffer({.size = 16, .type = RESOURCE_TYPE_VERTEX_BUFFER});
    REQUIRE(buffer);
    auto* renderer = EngineContext::render_system();
    auto packet = renderer->make_render_packet(nullptr, nullptr, 0);
    packet.camera = RenderCamera{};
    packet.depth_prepass = packet.pbr = packet.npr = packet.gpu_driven = packet.bloom.enabled = false;
    packet.ui.reset();
    renderer->set_custom_rdg_build_func([buffer](RDGBuilder& graph, const RenderPacket&) {
        graph.create_render_pass("UndeclaredVertexInput")
            .color(0, graph.get_texture("BackBuffer"), ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE)
            .execute([buffer](RDGPassContext context) {
                context.command->bind_vertex_buffer(buffer);
            }).finish();
    });
    auto frame = renderer->build_frame(packet);
    renderer->clear_custom_rdg_build_func();
    REQUIRE(frame);
    CHECK_FALSE(renderer->execute_frame(std::move(*frame)));
    test_utils::TestContext::reset();
}
