#include <catch2/catch_test_macros.hpp>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/core/window/window.h"
#include "engine/core/utils/path_utils.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include <cstring>

namespace {
RHIShaderRef make_shader(const char* source, ShaderFrequency frequency, const char* profile) {
    auto backend = EngineContext::rhi();
    return backend->create_shader({.frequency = frequency, .code = backend->compile_shader(source, "main", profile)});
}
const char* fullscreen_vertex = R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
        float2 uv = float2((id << 1) & 2, id & 2);
        return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    })";
RHIGraphicsPipelineRef make_pipeline(RHIShaderRef vertex, RHIShaderRef fragment, RHIRootSignatureRef root) {
    RHIGraphicsPipelineInfo info = {};
    info.vertex_shader = vertex; info.fragment_shader = fragment; info.root_signature = root;
    info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    info.depth_stencil_state.enable_depth_test = false; info.depth_stencil_state.enable_depth_write = false;
    info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
    return EngineContext::rhi()->create_graphics_pipeline(info);
}
RHICommandListRef make_commands(bool bypass) {
    auto backend = EngineContext::rhi();
    auto pool = backend->create_command_pool({backend->get_queue({QUEUE_TYPE_GRAPHICS, 0})});
    return pool->create_command_list(bypass);
}
}

TEST_CASE("Root layouts merge stages and preserve register namespaces", "[rhi][dx12]") {
    test_utils::TestContext::reset();
    RHIRootSignatureInfo info;
    info.add_entry({.binding = 0, .frequency = SHADER_FREQUENCY_VERTEX, .type = RESOURCE_TYPE_UNIFORM_BUFFER});
    info.add_entry({.binding = 0, .frequency = SHADER_FREQUENCY_FRAGMENT, .type = RESOURCE_TYPE_UNIFORM_BUFFER});
    info.add_entry({.binding = 0, .size = 2, .type = RESOURCE_TYPE_TEXTURE});
    info.add_entry({.binding = 0, .type = RESOURCE_TYPE_SAMPLER});
    REQUIRE(info.is_valid());
    REQUIRE(info.get_entries().size() == 3);
    REQUIRE(info.find_entry(0, 0, RESOURCE_TYPE_UNIFORM_BUFFER)->frequency == (SHADER_FREQUENCY_VERTEX | SHADER_FREQUENCY_FRAGMENT));
    info.add_entry({.binding = 1, .type = RESOURCE_TYPE_BUFFER});
    REQUIRE_FALSE(info.is_valid()); // t1 overlaps the t0[2] array even with a different SRV type.
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 RDG compute render copy and pooled descriptor reuse", "[rhi][dx12][rdg]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    REQUIRE(backend->get_backend_info().type == BACKEND_DX12);
    backend->check_debug_messages("initialization");
    auto compute = make_shader(R"(
        RWTexture2D<float4> output_tex : register(u0);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
            uint w,h; output_tex.GetDimensions(w,h);
            if (id.x < w && id.y < h) output_tex[id.xy] = float4(0.25,0.5,0.75,1);
        })", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    REQUIRE(compute);
    RHIRootSignatureInfo compute_layout; compute_layout.add_entry_from_reflect(compute);
    REQUIRE(compute_layout.get_entries().size() == 1);
    auto compute_root = backend->create_root_signature(compute_layout); REQUIRE(compute_root);
    auto compute_pipeline = backend->create_compute_pipeline({compute, compute_root}); REQUIRE(compute_pipeline);
    auto vertex = make_shader(fullscreen_vertex, SHADER_FREQUENCY_VERTEX, "vs_5_0");
    auto fragment = make_shader(R"(
        Texture2D<float4> input_tex : register(t0);
        float4 main(float4 position : SV_Position) : SV_Target { return input_tex.Load(int3(position.xy,0)); }
        )", SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
    REQUIRE(vertex); REQUIRE(fragment);
    RHIRootSignatureInfo render_layout; render_layout.add_entry_from_reflect(fragment);
    auto render_root = backend->create_root_signature(render_layout); REQUIRE(render_root);
    auto pipeline = make_pipeline(vertex, fragment, render_root); REQUIRE(pipeline);
    auto output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET}); REQUIRE(output);
    for (bool bypass : {true, false}) {
        for (int frame = 0; frame < 3; ++frame) {
            auto commands = make_commands(bypass); commands->begin_command();
            RDGBuilder graph(commands);
            auto source = graph.create_texture("ComputeOutput").extent({13,7,1}).format(FORMAT_R8G8B8A8_UNORM).allow_read_write().finish();
            auto color = graph.create_texture("Color").extent({13,7,1}).format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
            auto copied = graph.create_texture("CopyOutput").import(output, RESOURCE_STATE_UNDEFINED).finish();
            graph.create_compute_pass("Write")
                .root_signature(compute_root).read_write(0,0,0,source)
                .execute([&](RDGPassContext ctx) {
                    ctx.command->set_compute_pipeline(compute_pipeline);
                    ctx.command->bind_descriptor_set(ctx.descriptors[0],0);
                    ctx.command->dispatch(2,1,1);
                }).finish();
            graph.create_render_pass("Sample")
                .root_signature(render_root).read(0,0,0,source)
                .color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE)
                .execute([&](RDGPassContext ctx) {
                    ctx.command->set_graphics_pipeline(pipeline);
                    ctx.command->bind_descriptor_set(ctx.descriptors[0],0);
                    ctx.command->draw(3,1,0,0);
                }).finish();
            graph.create_copy_pass("Copy").from(color).to(copied).finish();
            graph.execute(); commands->end_command(); commands->execute();
            std::vector<uint8_t> pixels(13*7*4);
            REQUIRE(backend->create_command_context(nullptr)->read_texture(output,pixels.data(),uint32_t(pixels.size())));
            for (size_t i = 0; i < pixels.size(); i += 4) {
                REQUIRE(pixels[i] == 64); REQUIRE(pixels[i+1] == 128); REQUIRE(pixels[i+2] == 191); REQUIRE(pixels[i+3] == 255);
            }
            REQUIRE_FALSE(backend->check_debug_messages("RDG compute-render-copy"));
        }
    }
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 constant buffer and descriptor snapshots survive deferred draws", "[rhi][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto vertex = make_shader(fullscreen_vertex,SHADER_FREQUENCY_VERTEX,"vs_5_0");
    auto fragment = make_shader("cbuffer Params : register(b0) { float4 color; }; float4 main() : SV_Target { return color; }",SHADER_FREQUENCY_FRAGMENT,"ps_5_0");
    REQUIRE(vertex); REQUIRE(fragment);
    RHIRootSignatureInfo layout; layout.add_entry_from_reflect(fragment);
    auto root = backend->create_root_signature(layout); REQUIRE(root);
    auto pipeline = make_pipeline(vertex,fragment,root); REQUIRE(pipeline);
    auto set = root->create_descriptor_set(0); REQUIRE(set);
    auto buffer = backend->create_buffer({.size = 16, .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_UNIFORM_BUFFER}); REQUIRE(buffer);
    auto target = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .type = RESOURCE_TYPE_RENDER_TARGET | RESOURCE_TYPE_TEXTURE}); REQUIRE(target);
    auto view = backend->create_texture_view({.texture = target}); REQUIRE(view);
    RHIRenderPassInfo pass_info = {}; pass_info.extent = {13,7}; pass_info.color_attachments[0].texture_view = view;
    pass_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
    auto pass = backend->create_render_pass(pass_info); REQUIRE(pass);
    for (bool bypass : {true,false}) {
        auto commands = make_commands(bypass); commands->begin_command(); commands->begin_render_pass(pass); commands->set_graphics_pipeline(pipeline);
        for (int i = 0; i < 2; ++i) {
            float color[4] = {i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, 0.f, 1.f};
            auto* mapped = buffer->map(); REQUIRE(mapped); memcpy(mapped,color,sizeof(color)); buffer->unmap();
            set->write_buffer(0,buffer);
            commands->bind_descriptor_set(set,0);
            commands->set_scissor({i == 0 ? 0u : 5u,0},{i == 0 ? 5u : 13u,7});
            commands->draw(3,1,0,0);
        }
        set->clear(); // The command stream owns its binding snapshots.
        commands->end_render_pass(); commands->end_command(); commands->execute();
        std::vector<uint8_t> pixels(13*7*4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(target,pixels.data(),uint32_t(pixels.size())));
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 13; ++x) {
            auto offset = (y*13+x)*4;
            REQUIRE(pixels[offset] == (x < 5 ? 255 : 0)); REQUIRE(pixels[offset+1] == (x < 5 ? 0 : 255));
        }
        REQUIRE_FALSE(backend->check_debug_messages("descriptor snapshots"));
    }
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 packed texture uploads preserve odd row pitches and mip layers", "[rhi][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto texture = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .array_layers = 2,
        .mip_levels = 3, .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RW_TEXTURE}); REQUIRE(texture);
    auto upload = backend->create_buffer({.size = 13*7*4*2, .memory_usage = MEMORY_USAGE_CPU_TO_GPU}); REQUIRE(upload);
    auto* cpu = static_cast<uint8_t*>(upload->map()); REQUIRE(cpu);
    for (uint32_t i = 0; i < 13*7*2; ++i) { cpu[i*4] = i < 13*7 ? 255 : 0; cpu[i*4+1] = i < 13*7 ? 0 : 255; cpu[i*4+2] = 0; cpu[i*4+3] = 255; }
    upload->unmap();
    auto immediate = backend->get_immediate_command();
    immediate->copy_buffer_to_texture(upload,0,texture,{TEXTURE_ASPECT_COLOR,0,0,2});
    immediate->generate_mips(texture); immediate->flush();
    auto readback = backend->create_buffer({.size = 3*1*4*2, .memory_usage = MEMORY_USAGE_GPU_TO_CPU}); REQUIRE(readback);
    immediate->copy_texture_to_buffer(texture,{TEXTURE_ASPECT_COLOR,2,0,2},readback,0); immediate->flush();
    auto* pixels = static_cast<uint8_t*>(readback->map()); REQUIRE(pixels);
    for (int i = 0; i < 6; ++i) { REQUIRE(pixels[i*4] == (i < 3 ? 255 : 0)); REQUIRE(pixels[i*4+1] == (i < 3 ? 0 : 255)); }
    readback->unmap();
    REQUIRE_FALSE(backend->check_debug_messages("upload-mips-readback"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 descriptor arrays use independent register spaces", "[rhi][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto shader = make_shader(R"(
        Texture2D<float4> colors[2] : register(t0, space1);
        RWTexture2D<float4> result : register(u0);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
            if (id.x < 13 && id.y < 7) result[id.xy] = id.x < 5 ? colors[0].Load(int3(0,0,0)) : colors[1].Load(int3(0,0,0));
        })",SHADER_FREQUENCY_COMPUTE,"cs_5_1");
    REQUIRE(shader);
    RHIRootSignatureInfo layout; layout.add_entry_from_reflect(shader);
    REQUIRE(layout.find_entry(1,0,RESOURCE_TYPE_TEXTURE));
    REQUIRE(layout.find_entry(1,0,RESOURCE_TYPE_TEXTURE)->size == 2);
    auto root = backend->create_root_signature(layout); REQUIRE(root);
    auto pipeline = backend->create_compute_pipeline({shader,root}); REQUIRE(pipeline);
    auto output_set = root->create_descriptor_set(0); auto input_set = root->create_descriptor_set(1);
    auto target = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {13,7,1}, .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RW_TEXTURE}); REQUIRE(target);
    output_set->write_texture(0,target,RESOURCE_TYPE_RW_TEXTURE);
    for (uint32_t i = 0; i < 2; ++i) {
        auto color = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {1,1,1}}); REQUIRE(color);
        auto upload = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_CPU_TO_GPU}); REQUIRE(upload);
        uint32_t pixel = i == 0 ? 0xff0000ff : 0xff00ff00;
        auto* cpu = upload->map(); REQUIRE(cpu); memcpy(cpu,&pixel,4); upload->unmap();
        backend->get_immediate_command()->copy_buffer_to_texture(upload,0,color,{TEXTURE_ASPECT_COLOR,0,0,1});
        input_set->write_texture(0,color,RESOURCE_TYPE_TEXTURE,0,i);
    }
    backend->get_immediate_command()->flush();
    auto commands = make_commands(false); commands->begin_command(); commands->set_compute_pipeline(pipeline);
    commands->bind_descriptor_set(output_set,0); commands->bind_descriptor_set(input_set,1); commands->dispatch(2,1,1);
    commands->end_command(); commands->execute();
    std::vector<uint8_t> pixels(13*7*4);
    REQUIRE(backend->create_command_context(nullptr)->read_texture(target,pixels.data(),uint32_t(pixels.size())));
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 13; ++x) {
        REQUIRE(pixels[(y*13+x)*4] == (x < 5 ? 255 : 0)); REQUIRE(pixels[(y*13+x)*4+1] == (x < 5 ? 0 : 255));
    }
    REQUIRE_FALSE(backend->check_debug_messages("descriptor spaces and arrays"));
    test_utils::TestContext::reset();
}

TEST_CASE("Hidden rendering captures the completed swapchain frame", "[rhi][dx12][capture]") {
    test_utils::TestContext::reset();
    auto* window = EngineContext::window(); REQUIRE(window);
    REQUIRE_FALSE(window->is_visible()); REQUIRE_FALSE(IsWindowVisible(window->get_hwnd()));
    auto scene = std::make_shared<Scene>();
    auto* entity = scene->create_entity(); entity->add_component<TransformComponent>();
    auto* camera = entity->add_component<CameraComponent>();
    auto* render_system = EngineContext::render_system();
    for (uint32_t frame = 0; frame < 2; ++frame) {
        render_system->set_custom_rdg_build_func([frame,render_system](RDGBuilder& graph,const RenderPacket&) {
            auto swapchain = render_system->get_swapchain();
            auto output = graph.create_texture("CaptureTarget").import(swapchain->get_texture(swapchain->get_current_frame_index()),RESOURCE_STATE_PRESENT).finish();
            graph.create_render_pass("CaptureClear").color(0,output,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,
                frame == 0 ? Color4{0,1,0,1} : Color4{0,0,1,1}).finish();
        });
        RenderPacket packet; packet.frame_index = frame; packet.active_camera = camera; packet.active_scene = scene.get();
        REQUIRE(render_system->tick(packet));
        std::vector<uint8_t> pixels; Extent2D extent;
        REQUIRE(render_system->capture_frame(pixels,extent)); REQUIRE(extent == WINDOW_EXTENT);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            REQUIRE(pixels[i] == 0); REQUIRE(pixels[i+1] == (frame == 0 ? 255 : 0)); REQUIRE(pixels[i+2] == (frame == 0 ? 0 : 255));
        }
        auto path = (utils::get_engine_path() / "build/test-results" / ("dx12-capture-" + std::to_string(frame) + ".png")).string();
        REQUIRE(test_utils::save_screenshot_png(path,extent.width,extent.height,pixels));
    }
    REQUIRE_FALSE(EngineContext::rhi()->check_debug_messages("hidden frame capture"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 RDG storage buffers root constants and consecutive UAV writes", "[rhi][dx12][rdg]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi(); backend->check_debug_messages();
    auto shader = make_shader(R"(
        StructuredBuffer<uint> values : register(t0);
        RWStructuredBuffer<uint> result : register(u0);
        cbuffer Params : register(b1) { uint add; uint accumulate; };
        [numthreads(16,1,1)] void main(uint3 id : SV_DispatchThreadID) {
            if (accumulate) result[id.x] += add;
            else result[id.x] = values[id.x] * 2 + add;
        })", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    REQUIRE(shader);
    RHIRootSignatureInfo layout;
    layout.add_entry({.type = RESOURCE_TYPE_BUFFER}); layout.add_entry({.type = RESOURCE_TYPE_RW_BUFFER});
    layout.add_push_constant({.size = 8, .frequency = SHADER_FREQUENCY_COMPUTE, .binding = 1});
    auto root = backend->create_root_signature(layout); REQUIRE(root);
    auto pipeline = backend->create_compute_pipeline({shader,root}); REQUIRE(pipeline);
    auto input = backend->create_buffer({.size = 64, .stride = 4, .memory_usage = MEMORY_USAGE_CPU_TO_GPU}); REQUIRE(input);
    auto output = backend->create_buffer({.size = 64, .stride = 4, .type = RESOURCE_TYPE_RW_BUFFER}); REQUIRE(output);
    auto readback = backend->create_buffer({.size = 64, .memory_usage = MEMORY_USAGE_GPU_TO_CPU}); REQUIRE(readback);
    auto* data = static_cast<uint32_t*>(input->map()); REQUIRE(data);
    for (uint32_t i = 0; i < 16; ++i) data[i] = i;
    input->unmap();
    auto commands = make_commands(false); commands->begin_command();
    RDGBuilder graph(commands);
    auto src = graph.create_buffer("Values").import(input,RESOURCE_STATE_COMMON).finish();
    auto dst = graph.create_buffer("Result").import(output,RESOURCE_STATE_COMMON).finish();
    for (uint32_t pass = 0; pass < 2; ++pass) {
        graph.create_compute_pass("BufferPass"+std::to_string(pass)).root_signature(root)
            .read(0,0,0,src,0,64,RESOURCE_TYPE_BUFFER).read_write(0,0,0,dst,0,64)
            .execute([pipeline,pass](RDGPassContext ctx) {
                ctx.command->set_compute_pipeline(pipeline); ctx.command->bind_descriptor_set(ctx.descriptors[0],0);
                uint32_t constants[] = {pass == 0 ? 5u : 9u,pass};
                ctx.command->push_constants(constants,sizeof(constants),SHADER_FREQUENCY_COMPUTE);
                ctx.command->dispatch(1,1,1);
            }).finish();
    }
    graph.execute(); commands->copy_buffer(output,0,readback,0,64); commands->end_command(); commands->execute();
    auto* result = static_cast<uint32_t*>(readback->map()); REQUIRE(result);
    for (uint32_t i = 0; i < 16; ++i) REQUIRE(result[i] == i*2+14);
    readback->unmap();
    REQUIRE_FALSE(backend->check_debug_messages("RDG storage buffers and constants"));
    test_utils::TestContext::reset();
}
