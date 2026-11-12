#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("RDG sorts forward resource references and culls dead branches", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto a = texture(graph,"A"), b = texture(graph,"B"), unused = texture(graph,"Unused");
    graph.create_compute_pass("Consumer").read(0,0,0,b).never_cull();
    graph.create_compute_pass("Middle").read(0,0,0,a).output_read(b);
    graph.create_compute_pass("UnusedConsumer").read(0,0,0,a).output_read(unused);
    graph.create_compute_pass("Producer").output_read(a);
    auto allocated = RDGTexturePool::get()->allocated_size();
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"Producer","Middle","Consumer"});
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"UnusedConsumer"});
    REQUIRE(RDGTexturePool::get()->allocated_size() == allocated);
    REQUIRE(graph.get_execution_passes()[0]->before(graph.get_execution_passes()[2]));
    test_utils::TestContext::reset();
}

TEST_CASE("RDG tracks overwritten versions and attachment load dependencies", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    for (auto load : {ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_LOAD_OP_LOAD}) {
        RDGBuilder graph;
        auto color = texture(graph,"Color");
        graph.create_render_pass("Old").color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
        graph.create_render_pass("New").color(0,color,load,ATTACHMENT_STORE_OP_STORE);
        graph.create_compute_pass("Read").read(0,0,0,color).never_cull();
        REQUIRE(graph.compile());
        if (load == ATTACHMENT_LOAD_OP_CLEAR) {
            REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"New","Read"});
            REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"Old"});
        } else {
            REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"Old","New","Read"});
            REQUIRE(graph.get_compile_info().culled_passes.empty());
        }
    }
    test_utils::TestContext::reset();
}

TEST_CASE("RDG culling preserves write ordering across removed versions", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto x = texture(graph,"X"), a = texture(graph,"A"), z = texture(graph,"Z");
    graph.create_render_pass("FirstWriter").read(0,0,0,z)
        .color(0,x,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE)
        .color(1,a,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
    graph.create_render_pass("Overwritten").color(0,x,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
    graph.create_render_pass("FinalWriter").color(0,x,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
    graph.create_compute_pass("ReadX").read(0,0,0,x).never_cull();
    graph.create_compute_pass("ReadA").read(0,0,0,a).never_cull();
    graph.create_compute_pass("ForwardProducer").output_read(z);
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"ForwardProducer","FirstWriter","FinalWriter","ReadX","ReadA"});
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"Overwritten"});
    test_utils::TestContext::reset();
}

TEST_CASE("RDG imported aliases preserve initial reads before later writes", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    auto native = EngineContext::rhi()->create_buffer({.size = 64, .stride = 4, .type = RESOURCE_TYPE_RW_BUFFER});
    REQUIRE(native);
    RDGBuilder graph;
    auto a = graph.create_buffer("AliasA").import(native,RESOURCE_STATE_COMMON).finish();
    auto b = graph.create_buffer("AliasB").import(native,RESOURCE_STATE_COMMON).finish();
    auto read = graph.create_compute_pass("ReadInitial").read(0,0,0,a,0,64,RESOURCE_TYPE_BUFFER).never_cull().finish();
    graph.create_compute_pass("WriteImported").read_write(0,0,0,b);
    auto prerequisite = graph.create_compute_pass("LatePrerequisite").finish();
    REQUIRE(graph.add_dependency(read,prerequisite));
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"LatePrerequisite","ReadInitial","WriteImported"});
    REQUIRE(graph.get_compile_info().culled_passes.empty());
    test_utils::TestContext::reset();
}

TEST_CASE("RDG resolves texture subresources without false dependencies", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto tex = graph.create_texture("Mips").extent({8,8,1}).format(FORMAT_R8G8B8A8_UNORM).mip_levels(2).array_layers(2).allow_read_write().finish();
    TextureSubresourceRange base = {TEXTURE_ASPECT_COLOR,0,1,0,1};
    TextureSubresourceRange mip = {TEXTURE_ASPECT_COLOR,1,1,1,1};
    graph.create_compute_pass("Filter").read(0,0,0,tex,VIEW_TYPE_2D_ARRAY,base).output_read(tex,mip).never_cull();
    graph.create_compute_pass("UnusedLayer").output_read(tex,{TEXTURE_ASPECT_COLOR,0,1,1,1});
    graph.create_compute_pass("BaseProducer").output_read(tex,base);
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"BaseProducer","Filter"});
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"UnusedLayer"});
    test_utils::TestContext::reset();
}

TEST_CASE("RDG buffer ranges retain only contributing versions", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto buffer = graph.create_buffer("Data").size(64).allow_read_write().finish();
    graph.create_compute_pass("FullWrite").output_read(buffer,0,64);
    graph.create_compute_pass("PrefixWrite").output_read(buffer,0,16);
    graph.create_compute_pass("ReadTail").read(0,0,0,buffer,16,48,RESOURCE_TYPE_BUFFER).never_cull();
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"FullWrite","ReadTail"});
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"PrefixWrite"});
    graph.create_compute_pass("ReadPrefix").read(0,0,0,buffer,0,16,RESOURCE_TYPE_BUFFER).never_cull();
    REQUIRE(graph.compile());
    REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"FullWrite","PrefixWrite","ReadTail","ReadPrefix"});
    test_utils::TestContext::reset();
}

TEST_CASE("RDG explicit dependencies and side effects execute in stable order", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    auto command = commands();
    RDGBuilder graph(command);
    std::vector<std::string> calls;
    auto consumer = graph.create_compute_pass("Consumer").never_cull().execute([&](RDGPassContext) { calls.push_back("Consumer"); }).finish();
    graph.create_compute_pass("DeadCallback").execute([&](RDGPassContext) { calls.push_back("Dead"); });
    auto producer = graph.create_compute_pass("Producer").execute([&](RDGPassContext) { calls.push_back("Producer"); }).finish();
    graph.create_compute_pass("Independent").never_cull().execute([&](RDGPassContext) { calls.push_back("Independent"); });
    REQUIRE(graph.add_dependency(consumer,producer));
    REQUIRE(graph.compile()); REQUIRE(calls.empty());
    command->begin_command(); REQUIRE(graph.execute()); command->end_command(); command->execute();
    REQUIRE(calls == std::vector<std::string>{"Producer","Consumer","Independent"});
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"DeadCallback"});
    test_utils::TestContext::reset();
}

TEST_CASE("RDG rejects live cycles before callbacks or GPU allocation", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto a = texture(graph,"A"), b = texture(graph,"B");
    int calls = 0;
    graph.create_compute_pass("CycleA").read(0,0,0,b).output_read(a).execute([&](RDGPassContext) { ++calls; });
    graph.create_compute_pass("CycleB").read(0,0,0,a).output_read(b).execute([&](RDGPassContext) { ++calls; });
    REQUIRE(graph.compile()); // An unreachable cycle is culled.
    REQUIRE(graph.get_execution_passes().empty());
    RHITextureRef output;
    REQUIRE(graph.extract_texture(a,output));
    auto allocated = RDGTexturePool::get()->allocated_size();
    REQUIRE_FALSE(graph.execute());
    REQUIRE(graph.get_compile_info().error.find("CycleA") != std::string::npos);
    REQUIRE(graph.get_compile_info().error.find("CycleB") != std::string::npos);
    REQUIRE(graph.get_execution_passes().empty()); REQUIRE(calls == 0); REQUIRE_FALSE(output);
    REQUIRE(RDGTexturePool::get()->allocated_size() == allocated);
    test_utils::TestContext::reset();
}

TEST_CASE("RDG rejects undefined live inputs while culling unused work", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto missing = texture(graph,"Missing");
    auto reader = graph.create_compute_pass("Reader").read(0,0,0,missing);
    REQUIRE(graph.compile()); REQUIRE(graph.get_execution_passes().empty());
    reader.never_cull();
    REQUIRE_FALSE(graph.compile());
    REQUIRE(graph.get_compile_info().error.find("Missing") != std::string::npos);
    test_utils::TestContext::reset();
}

TEST_CASE("RDG removes unused work before any GPU resources are allocated", "[rdg][rdg_compile]") {
    test_utils::TestContext::reset();
    RDGBuilder graph;
    auto color = texture(graph,"UnusedColor");
    auto buffer = graph.create_buffer("UnusedBuffer").size(64).allow_read_write().finish();
    int calls = 0;
    graph.create_render_pass("UnusedDraw").color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE)
        .execute([&](RDGPassContext) { ++calls; });
    graph.create_compute_pass("UnusedCompute").output_read(buffer).execute([&](RDGPassContext) { ++calls; });
    auto texture_count = RDGTexturePool::get()->allocated_size();
    auto buffer_count = RDGBufferPool::get()->allocated_size();
    REQUIRE(graph.execute()); // No command list is needed for an empty compiled graph.
    REQUIRE(calls == 0);
    REQUIRE(graph.get_compile_info().execution_order.empty());
    REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"UnusedDraw","UnusedCompute"});
    REQUIRE(RDGTexturePool::get()->allocated_size() == texture_count);
    REQUIRE(RDGBufferPool::get()->allocated_size() == buffer_count);
    test_utils::TestContext::reset();
}

TEST_CASE("RDG reordered copy chain preserves GPU output and last use", "[rdg][rdg_compile][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET}); REQUIRE(output);
    for (bool bypass : {true,false}) {
        auto command = commands(bypass); command->begin_command();
        RDGBuilder graph(command);
        auto src = texture(graph,"Source"), intermediate = texture(graph,"Intermediate"), dead = texture(graph,"Dead");
        auto dst = graph.create_texture("External").import(output,RESOURCE_STATE_UNDEFINED).finish();
        graph.create_copy_pass("FirstCopy").from(src).to(intermediate);
        graph.create_copy_pass("FinalCopy").from(intermediate).to(dst);
        graph.create_render_pass("ClearProducer").color(0,src,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,{0.25f,0.5f,0.75f,1});
        graph.create_render_pass("DeadClear").color(0,dead,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE);
        REQUIRE(graph.execute()); command->end_command(); command->execute();
        REQUIRE(graph.get_compile_info().execution_order == std::vector<std::string>{"ClearProducer","FirstCopy","FinalCopy"});
        REQUIRE(graph.get_compile_info().culled_passes == std::vector<std::string>{"DeadClear"});
        std::vector<uint8_t> pixels(13*7*4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(output,pixels.data(),uint32_t(pixels.size())));
        for (size_t i = 0; i < pixels.size(); i += 4) {
            REQUIRE(pixels[i] == 64); REQUIRE(pixels[i+1] == 128); REQUIRE(pixels[i+2] == 191); REQUIRE(pixels[i+3] == 255);
        }
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
        RDGBuilder graph(command);
        auto color = texture(graph,"Export");
        graph.create_render_pass("WriteExport").color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,
            i == 0 ? Color4{1,0,0,1} : Color4{0,1,0,1});
        REQUIRE(graph.extract_texture(color,outputs[i]));
        REQUIRE(graph.execute()); command->end_command(); command->execute();
        REQUIRE(outputs[i]);
    }
    REQUIRE(outputs[0].get() != outputs[1].get());
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> pixels(13*7*4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(outputs[i],pixels.data(),uint32_t(pixels.size())));
        for (size_t p = 0; p < pixels.size(); p += 4) { REQUIRE(pixels[p] == (i == 0 ? 255 : 0)); REQUIRE(pixels[p+1] == (i == 0 ? 0 : 255)); }
    }
    REQUIRE_FALSE(backend->check_debug_messages("RDG exports"));
    test_utils::TestContext::reset();
}
