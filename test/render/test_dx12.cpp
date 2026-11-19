#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/core/window/window.h"
#include "engine/core/utils/path_utils.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include <imgui.h>
#include <array>
#include <algorithm>
#include <barrier>
#include <ranges>
#include <thread>
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

TEST_CASE("DX12 upload buffer can publish a partial mapped write", "[rhi][dx12][buffer_range]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    REQUIRE(backend->get_backend_info().type == BACKEND_DX12);

    const std::array<uint32_t, 3> initial{11, 22, 33};
    auto upload = backend->create_buffer({.size = sizeof(initial), .memory_usage = MEMORY_USAGE_CPU_TO_GPU});
    auto readback = backend->create_buffer({.size = sizeof(initial), .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(upload);
    REQUIRE(readback);

    auto* mapped = static_cast<uint32_t*>(upload->map());
    REQUIRE(mapped);
    memcpy(mapped, initial.data(), sizeof(initial));
    upload->unmap();

    mapped = static_cast<uint32_t*>(upload->map());
    REQUIRE(mapped);
    mapped[1] = 42;
    upload->unmap_written_range(sizeof(uint32_t), sizeof(uint32_t));

    auto command = backend->get_immediate_command();
    command->copy_buffer(upload, 0, readback, 0, sizeof(initial));
    command->flush();
    const auto* result = static_cast<const uint32_t*>(readback->map());
    REQUIRE(result);
    CHECK(result[0] == 11);
    CHECK(result[1] == 42);
    CHECK(result[2] == 33);
    readback->unmap();
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
            RDGBuilder graph;
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
            REQUIRE(graph.execute(commands)); commands->end_command(); commands->execute();
            std::vector<uint8_t> pixels(13*7*4);
            REQUIRE(backend->create_command_context(nullptr)->read_texture(output,pixels.data(),uint32_t(pixels.size())));
            CHECK(test_utils::image_matches(pixels, 13, [](auto, auto) { return std::array<uint8_t,4>{64,128,191,255}; }));
            REQUIRE_FALSE(backend->check_debug_messages("RDG compute-render-copy"));
        }
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Texture upload is ordered before the first parallel RDG frame", "[render][rdg][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    auto* renderer = EngineContext::render_system();
    backend->check_debug_messages();

    auto uploaded = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_UNORM, Extent3D{1, 1, 1}, 1, 1);
    REQUIRE(uploaded->texture_);
    std::array<uint8_t, 4> texel{37, 149, 223, 255};
    uploaded->set_data(texel.data(), uint32_t(texel.size()));

    auto vertex = make_shader(fullscreen_vertex, SHADER_FREQUENCY_VERTEX, "vs_5_0");
    auto fragment = make_shader(R"(
        Texture2D<float4> source : register(t0);
        float4 main() : SV_Target { return source.Load(int3(0, 0, 0)); }
    )", SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
    REQUIRE(vertex);
    REQUIRE(fragment);
    RHIRootSignatureInfo layout;
    layout.add_entry_from_reflect(fragment);
    auto root = backend->create_root_signature(layout);
    REQUIRE(root);
    auto pipeline = make_pipeline(vertex, fragment, root);
    REQUIRE(pipeline);

    auto packet = renderer->make_render_packet(nullptr, nullptr, 0);
    packet.camera = RenderCamera{};
    packet.depth_prepass = packet.pbr = packet.npr = packet.gpu_driven = packet.bloom.enabled = false;
    packet.ui.reset();
    renderer->set_custom_rdg_build_func([&](RDGBuilder& graph, const RenderPacket&) {
        auto source = graph.create_texture("AssetTexture").import(uploaded->texture_, RESOURCE_STATE_SHADER_RESOURCE).finish();
        auto intermediate = graph.create_texture("Sampled").extent({WINDOW_EXTENT.width, WINDOW_EXTENT.height, 1})
            .format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
        graph.create_render_pass("SampleAsset").root_signature(root).read(0, 0, 0, source)
            .color(0, intermediate, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE)
            .execute([pipeline](RDGPassContext context) {
                context.command->set_graphics_pipeline(pipeline);
                context.command->bind_descriptor_set(context.descriptors[0]);
                context.command->draw(3);
            }).finish();
        graph.create_copy_pass("CopySample").from(intermediate).to(graph.get_texture("BackBuffer")).finish();
    });
    auto frame = renderer->build_frame(packet);
    renderer->clear_custom_rdg_build_func();
    REQUIRE(frame);
    REQUIRE(renderer->execute_frame(std::move(*frame)));
    std::vector<uint8_t> pixels;
    Extent2D extent;
    REQUIRE(renderer->capture_frame(pixels, extent));
    CHECK(test_utils::image_matches(pixels, extent.width, [&](auto, auto) { return texel; }));
    CHECK_FALSE(backend->check_debug_messages("upload before parallel RDG sampling"));
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
        CHECK(test_utils::image_matches(pixels, 13, [](auto x, auto) {
            return std::array<uint8_t,4>{uint8_t(x < 5 ? 255 : 0),uint8_t(x < 5 ? 0 : 255),0,255};
        }));
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
    CHECK(test_utils::image_matches(std::span<const uint8_t>(pixels, 24), 3, [](auto, auto y) {
        return std::array<uint8_t,4>{uint8_t(y == 0 ? 255 : 0),uint8_t(y == 0 ? 0 : 255),0,255};
    }));
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
    CHECK(test_utils::image_matches(pixels, 13, [](auto x, auto) {
        return std::array<uint8_t,4>{uint8_t(x < 5 ? 255 : 0),uint8_t(x < 5 ? 0 : 255),0,255};
    }));
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
        render_system->set_custom_rdg_build_func([frame](RDGBuilder& graph,const RenderPacket&) {
            auto output = graph.get_texture("BackBuffer");
            graph.create_render_pass("CaptureClear").color(0,output,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,
                frame == 0 ? Color4{0,1,0,1} : Color4{0,0,1,1}).finish();
        });
        auto packet = render_system->make_render_packet(scene.get(), camera, frame);
        REQUIRE(render_system->tick(packet));
        std::vector<uint8_t> pixels; Extent2D extent;
        REQUIRE(render_system->capture_frame(pixels,extent)); REQUIRE(extent == WINDOW_EXTENT);
        REQUIRE(pixels.size() == size_t(extent.width) * extent.height * 4);
        const uint8_t green = frame == 0 ? 255 : 0;
        const uint8_t blue = frame == 0 ? 0 : 255;
        size_t offset = 0;
        while (offset < pixels.size() && pixels[offset] == 0 &&
               pixels[offset + 1] == green && pixels[offset + 2] == blue) {
            offset += 4;
        }
        CAPTURE(frame, offset / 4);
        REQUIRE(offset == pixels.size());
        auto path = (utils::get_engine_path() / "build/test-results" / ("dx12-capture-" + std::to_string(frame) + ".png")).string();
        REQUIRE(test_utils::save_screenshot_png(path,extent.width,extent.height,pixels));
    }
    REQUIRE_FALSE(EngineContext::rhi()->check_debug_messages("hidden frame capture"));
    test_utils::TestContext::reset();
}

TEST_CASE("Render packet owns the editor draw data for its frame", "[rhi][dx12][render_packet]") {
    test_utils::TestContext::reset();
    auto* renderer = EngineContext::render_system();
    renderer->set_show_ui(true);
    ImU32 color = IM_COL32(255, 0, 0, 255);
    renderer->add_custom_ui_callback("test_overlay", [&] {
        ImGui::GetForegroundDrawList()->AddRectFilled({8, 8}, {32, 32}, color);
    });
    auto first = renderer->make_render_packet(nullptr, nullptr, 0);
    color = IM_COL32(0, 255, 0, 255);
    auto second = renderer->make_render_packet(nullptr, nullptr, 1);
    renderer->clear_custom_ui_callbacks();
    renderer->set_show_ui(false);

    REQUIRE(first.ui);
    REQUIRE(second.ui);
    std::vector<uint8_t> pixels;
    Extent2D extent;
    REQUIRE(renderer->tick(first));
    REQUIRE(renderer->capture_frame(pixels, extent));
    auto offset = (16 * extent.width + 16) * 4;
    CHECK(pixels[offset] == 255);
    CHECK(pixels[offset + 1] == 0);
    REQUIRE(renderer->tick(second));
    REQUIRE(renderer->capture_frame(pixels, extent));
    offset = (16 * extent.width + 16) * 4;
    CHECK(pixels[offset] == 0);
    CHECK(pixels[offset + 1] == 255);
    REQUIRE_FALSE(EngineContext::rhi()->check_debug_messages("queued editor draw data"));
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
    RDGBuilder graph;
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
    REQUIRE(graph.execute(commands)); commands->copy_buffer(output,0,readback,0,64); commands->end_command(); commands->execute();
    auto* result = static_cast<uint32_t*>(readback->map()); REQUIRE(result);
    CHECK(std::ranges::all_of(std::views::iota(0u, 16u), [&](auto i) { return result[i] == i*2+14; }));
    readback->unmap();
    REQUIRE_FALSE(backend->check_debug_messages("RDG storage buffers and constants"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 static geometry retains its staging data until submission", "[rhi][dx12][static_geometry]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    backend->check_debug_messages();

    std::array<uint32_t, 3> indices{1, 2, 3};
    auto buffer = backend->create_static_buffer({.size = sizeof(indices), .stride = sizeof(uint32_t),
        .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_INDEX_BUFFER}, indices.data());
    REQUIRE(buffer);
    CHECK(buffer->get_info().memory_usage == MEMORY_USAGE_GPU_ONLY);
    indices.fill(0);

    auto readback = backend->create_buffer({.size = sizeof(indices), .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(readback);
    auto command = backend->get_immediate_command();
    command->copy_buffer(buffer, 0, readback, 0, sizeof(indices));
    command->flush();
    auto* copied = static_cast<const uint32_t*>(readback->map());
    REQUIRE(copied);
    CHECK(copied[0] == 1);
    CHECK(copied[1] == 2);
    CHECK(copied[2] == 3);
    readback->unmap();
    CHECK_FALSE(backend->check_debug_messages("static geometry upload"));

    test_utils::TestContext::reset();
}

TEST_CASE("DX12 per-frame command contexts own independent command lists", "[rhi][dx12]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    auto* render_system = EngineContext::render_system();
    REQUIRE(backend);
    REQUIRE(render_system);

    auto& frame0_cmd = render_system->per_frame_common_resources_[0].commands.front();
    auto& frame1_cmd = render_system->per_frame_common_resources_[1].commands.front();
    REQUIRE(frame0_cmd);
    REQUIRE(frame1_cmd);
    REQUIRE(frame0_cmd->raw_handle() != nullptr);
    REQUIRE(frame1_cmd->raw_handle() != nullptr);
    CHECK(frame0_cmd->raw_handle() != frame1_cmd->raw_handle());

    auto immediate = backend->get_immediate_command();
    REQUIRE(immediate);
    std::array<uint32_t, 4> values{10, 20, 30, 40};
    auto static_buf = backend->create_static_buffer({.size = sizeof(values), .stride = sizeof(uint32_t),
        .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_VERTEX_BUFFER}, values.data());
    REQUIRE(static_buf);
    REQUIRE(immediate->raw_handle() != nullptr);
    CHECK(immediate->raw_handle() != frame0_cmd->raw_handle());
    CHECK(immediate->raw_handle() != frame1_cmd->raw_handle());

    auto readback = backend->create_buffer({.size = sizeof(values), .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(readback);
    frame0_cmd->begin_command();
    frame0_cmd->copy_buffer(static_buf, 0, readback, 0, sizeof(values));
    frame0_cmd->end_command();
    frame0_cmd->execute(nullptr, nullptr, nullptr);

    const auto* mapped = static_cast<const uint32_t*>(readback->map());
    REQUIRE(mapped);
    CHECK(mapped[0] == 10);
    CHECK(mapped[1] == 20);
    CHECK(mapped[2] == 30);
    CHECK(mapped[3] == 40);
    readback->unmap();
    CHECK_FALSE(backend->check_debug_messages("per-frame command contexts"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 asynchronous submissions preserve constants, views and readbacks across frame reuse", "[rhi][dx12][async]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    backend->check_debug_messages();
    auto shader = make_shader(R"(
        cbuffer Params : register(b0) { uint value; };
        Texture2D<float4> input_tex : register(t0);
        RWTexture2D<float4> output_tex : register(u0);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
            output_tex[id.xy] = input_tex.Load(int3(0,0,0)) + float4(float(value)/255.0,0,0,0);
        })", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    REQUIRE(shader);
    RHIRootSignatureInfo layout; layout.add_entry_from_reflect(shader);
    auto root = backend->create_root_signature(layout);
    REQUIRE(root);
    auto pipeline = backend->create_compute_pipeline({.compute_shader = shader, .root_signature = root});
    REQUIRE(pipeline);
    auto constants = backend->create_buffer({.size = 16, .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_UNIFORM_BUFFER});
    REQUIRE(constants);
    auto input = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {1,1,1}, .type = RESOURCE_TYPE_TEXTURE});
    auto upload = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_CPU_TO_GPU});
    REQUIRE(input); REQUIRE(upload);
    const std::array<uint8_t,4> texel{10,0,0,255};
    auto* source = upload->map(); REQUIRE(source);
    memcpy(source, texel.data(), texel.size()); upload->unmap();
    backend->get_immediate_command()->copy_buffer_to_texture(upload, 0, input, {TEXTURE_ASPECT_COLOR,0,0,1});
    auto set = root->create_descriptor_set(0);
    set->write_buffer(0, constants);
    std::array<RHICommandListRef,3> commands{make_commands(true), make_commands(false), make_commands(true)};
    std::array<RHIFenceRef,3> fences{backend->create_fence(true), backend->create_fence(true), backend->create_fence(true)};
    std::array<RHIBufferRef,18> readbacks;
    auto latest = backend->create_buffer({.size = 64*64*4, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(latest);
    for (uint32_t frame = 0; frame < readbacks.size(); ++frame) {
        auto& command = commands[frame % commands.size()];
        command->begin_command();
        auto output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {64,64,1},
            .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RW_TEXTURE});
        readbacks[frame] = backend->create_buffer({.size = 64*64*4, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
        REQUIRE(output); REQUIRE(readbacks[frame]);
        auto* data = static_cast<uint32_t*>(constants->map()); REQUIRE(data);
        data[0] = frame + 1; constants->unmap();
        set->write_texture(0, input).write_texture(0, output, RESOURCE_TYPE_RW_TEXTURE);
        command->set_compute_pipeline(pipeline);
        command->bind_descriptor_set(set,0);
        command->dispatch(8,8,1);
        command->copy_texture_to_buffer(output, {TEXTURE_ASPECT_COLOR,0,0,1}, readbacks[frame],0);
        command->copy_texture_to_buffer(output, {TEXTURE_ASPECT_COLOR,0,0,1}, latest,0);
        command->end_command();
        command->execute(fences[frame % fences.size()]);
        for (int tick = 0; tick < 7; ++tick) backend->tick();
    }
    for (const auto& fence : fences) fence->wait();
    const auto* latest_pixels = static_cast<const uint8_t*>(latest->map()); REQUIRE(latest_pixels);
    CHECK(test_utils::image_matches(std::span<const uint8_t>(latest_pixels,64*64*4),64,
        [](auto, auto) { return std::array<uint8_t,4>{28,0,0,255}; }));
    latest->unmap();
    bool correct = true;
    for (uint32_t frame = 0; frame < readbacks.size(); ++frame) {
        const auto* pixels = static_cast<const uint8_t*>(readbacks[frame]->map()); REQUIRE(pixels);
        correct &= test_utils::image_matches(std::span<const uint8_t>(pixels,64*64*4),64,
            [frame](auto, auto) { return std::array<uint8_t,4>{uint8_t(11+frame),0,0,255}; });
        readbacks[frame]->unmap();
    }
    CHECK(correct);
    CHECK_FALSE(backend->check_debug_messages("asynchronous frame resources"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 completed texture readbacks are visible while another context records", "[rhi][dx12][async]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    auto input = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {1,1,1}, .type = RESOURCE_TYPE_TEXTURE});
    auto upload = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_CPU_TO_GPU});
    auto readback = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
    REQUIRE(input); REQUIRE(upload); REQUIRE(readback);
    const std::array<uint8_t,4> expected{31,63,127,255};
    auto* source = upload->map(); REQUIRE(source);
    memcpy(source, expected.data(), expected.size()); upload->unmap();
    backend->get_immediate_command()->copy_buffer_to_texture(upload,0,input,{TEXTURE_ASPECT_COLOR,0,0,1});
    auto first = make_commands(true), second = make_commands(true);
    first->begin_command(); second->begin_command();
    first->copy_texture_to_buffer(input,{TEXTURE_ASPECT_COLOR,0,0,1},readback,0);
    auto fence = backend->create_fence(true); REQUIRE(fence);
    first->end_command(); first->execute(fence);
    fence->wait();
    const auto* pixels = static_cast<const uint8_t*>(readback->map()); REQUIRE(pixels);
    CHECK(std::equal(expected.begin(),expected.end(),pixels));
    readback->unmap();
    second->end_command(); second->execute();
    CHECK_FALSE(backend->check_debug_messages("completed copy with a recording context"));
    test_utils::TestContext::reset();
}

TEST_CASE("DX12 command contexts support multithreaded parallel recording", "[rhi][dx12][multithread]") {
    test_utils::TestContext::reset();
    auto backend = EngineContext::rhi();
    backend->check_debug_messages();

    auto shader = make_shader(R"(
        cbuffer ThreadParams : register(b0) { uint thread_color; };
        Texture2D<float4> base_tex : register(t0);
        RWTexture2D<float4> out_tex : register(u0);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) {
            if (id.x < 16 && id.y < 16) {
                float4 base = base_tex.Load(int3(0, 0, 0));
                out_tex[id.xy] = base + float4(float(thread_color) / 255.0, float(id.x) / 255.0, float(id.y) / 255.0, 0.0);
            }
        })", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    REQUIRE(shader);
    RHIRootSignatureInfo layout;
    layout.add_entry_from_reflect(shader);
    auto root = backend->create_root_signature(layout);
    REQUIRE(root);
    auto pipeline = backend->create_compute_pipeline({.compute_shader = shader, .root_signature = root});
    REQUIRE(pipeline);

    auto base_tex = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {1, 1, 1}, .type = RESOURCE_TYPE_TEXTURE});
    auto upload = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_CPU_TO_GPU});
    REQUIRE(base_tex);
    REQUIRE(upload);
    const std::array<uint8_t, 4> base_pixel{20, 10, 5, 255};
    void* upload_ptr = upload->map();
    REQUIRE(upload_ptr);
    std::memcpy(upload_ptr, base_pixel.data(), base_pixel.size());
    upload->unmap();
    auto immediate = backend->get_immediate_command();
    immediate->copy_buffer_to_texture(upload, 0, base_tex, {TEXTURE_ASPECT_COLOR, 0, 0, 1});
    immediate->texture_barrier({base_tex, RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_SHADER_RESOURCE,
        {TEXTURE_ASPECT_COLOR, 0, 1, 0, 1}});
    immediate->flush();

    constexpr uint32_t thread_count = 4;
    struct WorkerResources {
        RHICommandListRef commands;
        RHITextureRef output;
        RHIBufferRef constants;
        RHIDescriptorSetRef set;
    };
    std::array<WorkerResources, thread_count> workers;
    for (uint32_t i = 0; i < thread_count; ++i) {
        workers[i].commands = make_commands(true);
        workers[i].output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {16, 16, 1},
            .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RW_TEXTURE});
        workers[i].constants = backend->create_buffer({.size = 16, .memory_usage = MEMORY_USAGE_CPU_TO_GPU,
            .type = RESOURCE_TYPE_UNIFORM_BUFFER});
        workers[i].set = root->create_descriptor_set(0);
        REQUIRE(workers[i].commands);
        REQUIRE(workers[i].output);
        REQUIRE(workers[i].constants);
        REQUIRE(workers[i].set);

        auto* cbuf = static_cast<uint32_t*>(workers[i].constants->map());
        REQUIRE(cbuf);
        cbuf[0] = (i + 1) * 30;
        workers[i].constants->unmap();
        workers[i].set->write_buffer(0, workers[i].constants)
            .write_texture(0, base_tex)
            .write_texture(0, workers[i].output, RESOURCE_TYPE_RW_TEXTURE);
    }

    std::barrier sync_start(thread_count);
    {
        std::array<std::jthread, thread_count> threads;
        for (uint32_t i = 0; i < thread_count; ++i) {
            threads[i] = std::jthread([&, i] {
                sync_start.arrive_and_wait();
                auto& w = workers[i];
                w.commands->begin_command();
                w.commands->set_compute_pipeline(pipeline);
                w.commands->bind_descriptor_set(w.set, 0);
                w.commands->dispatch(2, 2, 1);
                w.commands->end_command();
            });
        }
    }

    for (uint32_t i = 0; i < thread_count; ++i) {
        workers[i].commands->execute();
    }

    for (uint32_t i = 0; i < thread_count; ++i) {
        std::vector<uint8_t> pixels(16 * 16 * 4);
        REQUIRE(backend->create_command_context(nullptr)->read_texture(workers[i].output, pixels.data(), uint32_t(pixels.size())));
        const uint8_t expected_r = uint8_t(20 + (i + 1) * 30);
        CHECK(test_utils::image_matches(pixels, 16, [expected_r](auto x, auto y) {
            return std::array<uint8_t, 4>{expected_r, uint8_t(10 + x), uint8_t(5 + y), 255};
        }));
    }
    CHECK_FALSE(backend->check_debug_messages("multithreaded parallel recording"));
    test_utils::TestContext::reset();
}
