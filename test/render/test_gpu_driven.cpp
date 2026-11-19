#include "test/test_assertions.h"
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_pass/gpu_driven_pass.h"
#include "engine/function/render/render_pass/draw_material.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/core/utils/path_utils.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <thread>

namespace {

render::DrawBatch rectangle(bool padded, float x, const Vec4& color, uint32_t object_id) {
    auto backend = EngineContext::rhi();
    const float half_x = padded ? 0.25f : 0.1875f;
    const float half_y = padded ? 0.125f : 0.1875f;
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;
    if (padded) { positions.emplace_back(8.0f,8.0f,8.0f); indices = {0,0,0}; }
    positions.insert(positions.end(), {Vec3(-half_x,-half_y,0), Vec3(half_x,-half_y,0),
        Vec3(half_x,half_y,0), Vec3(-half_x,half_y,0)});
    const uint32_t base = padded ? 1 : 0;
    for (auto index : {0u,1u,2u,0u,2u,3u}) indices.push_back(base + index);
    render::DrawBatch batch;
    batch.vertex_buffer = backend->create_static_buffer({.size = positions.size() * sizeof(Vec3), .stride = sizeof(Vec3),
        .type = RESOURCE_TYPE_VERTEX_BUFFER | RESOURCE_TYPE_BUFFER}, positions.data());
    batch.index_buffer = backend->create_static_buffer({.size = indices.size() * sizeof(uint32_t), .stride = sizeof(uint32_t),
        .type = RESOURCE_TYPE_INDEX_BUFFER | RESOURCE_TYPE_BUFFER}, indices.data());
    batch.index_count = 6; batch.index_offset = padded ? 3 : 0; batch.object_id = object_id;
    batch.model_matrix.m[3][0] = x; batch.model_matrix.m[3][2] = 0.5f;
    batch.bounds.min = Vec3(-half_x,-half_y,0); batch.bounds.max = Vec3(half_x,half_y,0);
    auto material = std::make_shared<render::DrawMaterial>();
    material->pbr.emplace(); material->pbr->albedo = color;
    batch.material_snapshot = material;
    return batch;
}

using Pixel = std::array<uint8_t,4>;
constexpr Pixel black{0,0,0,255}, red{255,0,0,255}, green{0,255,0,255}, blue{0,0,255,255};
struct Region { uint32_t x0, y0, x1, y1; Pixel color; };
Pixel expected_pixel(const std::vector<Region>& regions, uint32_t x, uint32_t y) {
    for (const auto& r : regions) if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) return r.color;
    return black;
}

}

TEST_CASE("GPU Scene culls and draws multiple geometries across in-flight frames", "[render][gpu_driven][dx12]") {
    test_utils::TestContext::reset();
    {
        auto backend = EngineContext::rhi(); backend->check_debug_messages();
        render::GpuDrivenPass pass;
        REQUIRE(pass.init());
        const auto left = rectangle(false,-0.5f,Vec4(1,0,0,1),101);
        const auto right = rectangle(true,0.5f,Vec4(0,1,0,1),902);
        REQUIRE((left.vertex_buffer && left.index_buffer && right.vertex_buffer && right.index_buffer));
        auto hidden = left; hidden.model_matrix.m[3][0] = 3;
        auto blue_left = left;
        auto blue_material = std::make_shared<render::DrawMaterial>();
        blue_material->pbr.emplace(); blue_material->pbr->albedo = Vec4(0,0,1,1);
        blue_left.material_snapshot = blue_material;
        struct Frame {
            std::vector<render::DrawBatch> candidates;
            std::vector<Region> regions;
            uint32_t expected_count = 0, count = UINT32_MAX;
            Mat4 view_projection = Mat4::Identity();
            std::unique_ptr<RDGBuilder> graph;
            std::shared_ptr<render::GpuSceneFrame> scene;
            RDGTextureHandle output{UINT32_MAX};
            RDGBufferHandle readback{UINT32_MAX};
            RHITextureRef target;
            RHIBufferRef counter;
            RHICommandListRef commands;
            RHIFenceRef fence;
            std::vector<uint8_t> pixels;
            bool compiled = false, recorded = false, read_back = false;
        };
        std::array<Frame,10> frames;
        const Region left_red{10,26,22,38,red}, right_green{40,28,56,36,green}, left_blue{10,26,22,38,blue};
        frames[0].candidates = {hidden,left,right}; frames[0].regions = {left_red,right_green};
        frames[1].candidates = {right,blue_left}; frames[1].regions = {left_blue,right_green};
        frames[2].candidates = {right}; frames[2].regions = {right_green};
        // Empty candidates and a nonempty list wholly outside all six frustum planes.
        for (int plane = 0; plane < 6; ++plane) {
            auto item = hidden; item.model_matrix = Mat4::Identity();
            item.model_matrix.m[3][plane / 2] = plane % 2 ? 3.0f : -3.0f;
            frames[4].candidates.push_back(item);
        }
        for (size_t i : {5u,6u}) {
            frames[i].candidates.assign(73,hidden);
            frames[i].candidates.push_back(i == 5 ? left : blue_left);
            frames[i].candidates.push_back(right);
            frames[i].regions = {i == 5 ? left_red : left_blue,right_green};
        }
        auto clipped = blue_left; clipped.model_matrix.m[3][0] = 0.9375f;
        auto no_indices = left; no_indices.index_count = 0;
        frames[7].candidates = {clipped,no_indices}; frames[7].regions = {{56,26,64,38,blue}};
        auto far_left = left; far_left.model_matrix.m[3][2] = 0.8f;
        auto near_left = blue_left; near_left.model_matrix.m[3][2] = 0.2f;
        frames[8].candidates = {near_left,far_left}; frames[8].regions = {{26,26,38,38,blue}};
        frames[8].view_projection.m[3][0] = 0.5f;
        frames[9].candidates = {right,left}; frames[9].regions = {left_red,right_green};
        for (auto& frame : frames) frame.expected_count = static_cast<uint32_t>(frame.regions.size());
        frames[8].expected_count = 2;

        std::jthread render_thread([&] {
            EngineContext::set_thread_role(EngineContext::Renderer);
            for (auto& frame : frames) {
                frame.graph = std::make_unique<RDGBuilder>(); auto& graph = *frame.graph;
                const RHITextureInfo color_info{.format = FORMAT_R8G8B8A8_UNORM, .extent = {64,64,1},
                    .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET};
                frame.output = graph.create_texture("Output").import(color_info,RESOURCE_STATE_COMMON).finish();
                auto depth = graph.create_texture("Depth").extent({64,64,1}).format(FORMAT_D32_SFLOAT).allow_depth_stencil().finish();
                frame.scene = pass.build(graph,frame.candidates,frame.view_projection,frame.output,depth,{64,64});
                if (!frame.scene) return;
                frame.readback = graph.create_buffer("CountReadback").import(
                    RHIBufferInfo{.size = 4, .memory_usage = MEMORY_USAGE_GPU_TO_CPU}, RESOURCE_STATE_TRANSFER_DST).finish();
                const auto counter = graph.get_buffer("GpuDrawCount");
                graph.create_copy_pass("ReadVisibleCount").read_buffer(counter).write_buffer(frame.readback)
                    .execute([counter,output = frame.readback](RDGPassContext ctx) {
                        ctx.command->copy_buffer(ctx.builder->resolve(counter),0,ctx.builder->resolve(output),0,4);
                    }).finish();
                frame.compiled = graph.compile();
                frame.candidates.clear(); // Graph callbacks own the packet snapshot.
            }
        });
        render_thread.join();
        REQUIRE(std::all_of(frames.begin(),frames.end(),[](const auto& f) { return f.compiled; }));
        std::jthread device_thread([&] {
            EngineContext::set_thread_role(EngineContext::Device);
            for (uint32_t i = 0; i < frames.size(); ++i) {
                auto& frame = frames[i];
                if (i >= FRAMES_IN_FLIGHT) frames[i-FRAMES_IN_FLIGHT].fence->wait();
                frame.target = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM, .extent = {64,64,1},
                    .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET});
                frame.counter = backend->create_buffer({.size = 4, .memory_usage = MEMORY_USAGE_GPU_TO_CPU});
                const auto pool = backend->create_command_pool({backend->get_queue({QUEUE_TYPE_GRAPHICS,0})});
                frame.commands = pool->create_command_list(i % 2 == 0);
                frame.fence = backend->create_fence(true);
                if (!frame.target || !frame.counter || !frame.commands || !frame.fence ||
                    !frame.graph->bind_external_texture(frame.output,frame.target) ||
                    !frame.graph->bind_external_buffer(frame.readback,frame.counter) ||
                    !pass.prepare(*frame.scene,*frame.graph,i % FRAMES_IN_FLIGHT)) return;
                frame.commands->begin_command();
                frame.recorded = frame.graph->execute_compiled(frame.commands,i % FRAMES_IN_FLIGHT);
                frame.commands->end_command(); frame.commands->execute(frame.fence);
                frame.scene.reset(); // Submitted RHI work must retain old geometry-pool versions.
                if (!frame.recorded) return;
            }
            for (auto& frame : frames) {
                frame.fence->wait();
                auto* mapped = frame.counter->map(); if (!mapped) return;
                memcpy(&frame.count,mapped,sizeof(frame.count)); frame.counter->unmap();
                frame.pixels.resize(64*64*4);
                frame.read_back = backend->create_command_context(nullptr)->read_texture(frame.target,frame.pixels.data(),64*64*4);
            }
        });
        device_thread.join();
        backend->get_queue({QUEUE_TYPE_GRAPHICS,0})->wait_idle();
        REQUIRE(std::all_of(frames.begin(),frames.end(),[](const auto& f) { return f.recorded && f.read_back; }));
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames[i]; CAPTURE(i);
            CHECK(frame.count == frame.expected_count);
            CHECK(test_utils::image_matches(frame.pixels,64,[&](auto x,auto y) { return expected_pixel(frame.regions,x,y); }));
        }
        const auto output = utils::get_engine_path() / "build/test-results/gpu-driven-multiple-meshes.png";
        CHECK(test_utils::save_screenshot_png(output.string(),64,64,frames[0].pixels));
        CHECK_FALSE(backend->check_debug_messages("GPU driven multi-frame geometry and count buffer"));
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Renderer submits an immutable GPU Driven packet", "[render][gpu_driven][dx12]") {
    test_utils::TestContext::reset();
    auto* renderer = EngineContext::render_system();
    renderer->enable_gpu_driven_ = true;
    renderer->gpu_driven_debug_colors_ = false;
    auto packet = renderer->make_render_packet(nullptr,nullptr,10);
    renderer->enable_gpu_driven_ = false;
    packet.camera = RenderCamera{Mat4::Identity(),Mat4::Identity(),Vec3::Zero(),0.1f,100.0f};
    packet.instances = {rectangle(false,-0.5f,Vec4(1,0,0,1),7),rectangle(true,0.5f,Vec4(0,1,0,1),500)};
    for (auto& instance : packet.instances) instance.model_matrix.m[1][1] = 8.0f / 9.0f;
    auto debug_packet = packet; debug_packet.frame_id = 11; debug_packet.gpu_driven_debug_colors = true;
    auto frame = renderer->build_frame(packet); REQUIRE(frame);
    packet.instances.clear();
    REQUIRE(renderer->execute_frame(std::move(*frame)));
    std::vector<uint8_t> pixels; Extent2D extent;
    REQUIRE(renderer->capture_frame(pixels,extent));
    const std::vector<Region> regions{{200,300,440,420,red},{800,320,1120,400,green}};
    CHECK(test_utils::image_matches(pixels,extent.width,[&](auto x,auto y) { return expected_pixel(regions,x,y); }));
    REQUIRE(renderer->tick(debug_packet));
    REQUIRE(renderer->capture_frame(pixels,extent));
    const auto sample = [&](uint32_t x) {
        Pixel color; std::copy_n(pixels.data() + (360 * extent.width + x) * 4,4,color.data()); return color;
    };
    const auto left_color = sample(320), right_color = sample(960);
    CHECK((left_color != right_color && left_color != black && right_color != black && left_color != red && right_color != green));
    const std::vector<Region> debug_regions{{200,300,440,420,left_color},{800,320,1120,400,right_color}};
    CHECK(test_utils::image_matches(pixels,extent.width,[&](auto x,auto y) { return expected_pixel(debug_regions,x,y); }));
    CHECK_FALSE(EngineContext::rhi()->check_debug_messages("GPU driven renderer integration"));
    test_utils::TestContext::reset();
}

TEST_CASE("GPU count controls indexed indirect draws with padded arguments", "[rhi][rdg][gpu_driven][dx12]") {
    test_utils::TestContext::reset();
    {
        auto backend = EngineContext::rhi(); backend->check_debug_messages();
        const auto shader = [&](const char* source, ShaderFrequency frequency, const char* profile) {
            return backend->create_shader({.frequency = frequency, .code = backend->compile_shader(source,"main",profile)});
        };
        auto dispatch_shader = shader(R"(
            RWByteAddressBuffer args : register(u0);
            [numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { args.Store3(16,uint3(1,1,1)); }
            )",SHADER_FREQUENCY_COMPUTE,"cs_5_0");
        auto compute = shader(R"(
            cbuffer Config : register(b0) { uint requested_count; };
            RWByteAddressBuffer args : register(u0);
            RWByteAddressBuffer count : register(u1);
            [numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
                args.Store4(16,uint4(6,1,0,0)); args.Store(32,0);
                args.Store4(40,uint4(6,1,6,0)); args.Store(56,0);
                count.Store(4,requested_count);
            })",SHADER_FREQUENCY_COMPUTE,"cs_5_0");
        auto vertex = shader(R"(
            struct Output { float4 position : SV_Position; float4 color : COLOR; };
            Output main(uint id : SV_VertexID) {
                float2 p[8] = {float2(-1,-1),float2(0,-1),float2(0,1),float2(-1,1),
                    float2(0,-1),float2(1,-1),float2(1,1),float2(0,1)};
                Output o; o.position = float4(p[id],0,1);
                o.color = id < 4 ? float4(1,0,0,1) : float4(0,1,0,1); return o;
            })",SHADER_FREQUENCY_VERTEX,"vs_5_0");
        auto fragment = shader("float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }",
            SHADER_FREQUENCY_FRAGMENT,"ps_5_0");
        REQUIRE((dispatch_shader && compute && vertex && fragment));
        RHIRootSignatureInfo compute_layout, raster_layout, dispatch_layout;
        compute_layout.add_entry_from_reflect(compute);
        dispatch_layout.add_entry_from_reflect(dispatch_shader);
        auto compute_root = backend->create_root_signature(compute_layout);
        auto dispatch_root = backend->create_root_signature(dispatch_layout);
        auto raster_root = backend->create_root_signature(raster_layout);
        REQUIRE((dispatch_root && compute_root && raster_root));
        auto generate = backend->create_compute_pipeline({.compute_shader = compute,.root_signature = compute_root});
        auto dispatch_pipeline = backend->create_compute_pipeline({.compute_shader = dispatch_shader,.root_signature = dispatch_root});
        RHIGraphicsPipelineInfo raster_info{};
        raster_info.vertex_shader = vertex; raster_info.fragment_shader = fragment; raster_info.root_signature = raster_root;
        raster_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
        raster_info.depth_stencil_state.enable_depth_test = false; raster_info.depth_stencil_state.enable_depth_write = false;
        raster_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
        auto raster = backend->create_graphics_pipeline(raster_info);
        REQUIRE((dispatch_pipeline && generate && raster));
        const std::array<uint32_t,12> index_data{0,1,2,0,2,3,4,5,6,4,6,7};
        auto indices = backend->create_static_buffer({.size = sizeof(index_data),.stride = 4,
            .type = RESOURCE_TYPE_INDEX_BUFFER},index_data.data());
        auto config = backend->create_buffer({.size = 16,.memory_usage = MEMORY_USAGE_CPU_TO_GPU,.type = RESOURCE_TYPE_UNIFORM_BUFFER});
        REQUIRE((indices && config));
        struct Scenario { uint32_t count, maximum, visible; };
        for (bool bypass : {true,false}) for (const auto scenario : {Scenario{0,2,0},Scenario{1,2,1},Scenario{2,2,2},Scenario{100,1,1}}) {
            CAPTURE(bypass,scenario.count,scenario.maximum);
            auto* mapped = config->map(); REQUIRE(mapped);
            memcpy(mapped,&scenario.count,sizeof(scenario.count)); config->unmap();
            auto output = backend->create_texture({.format = FORMAT_R8G8B8A8_UNORM,.extent = {64,32,1},
                .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET});
            REQUIRE(output);
            const auto pool = backend->create_command_pool({backend->get_queue({QUEUE_TYPE_GRAPHICS,0})});
            auto commands = pool->create_command_list(bypass);
            RDGBuilder graph;
            const auto target = graph.create_texture("Target").import(output,RESOURCE_STATE_COMMON).finish();
            const auto ib = graph.create_buffer("Index").import(indices,RESOURCE_STATE_COMMON).finish();
            const auto constants = graph.create_buffer("Config").import(config,RESOURCE_STATE_COMMON).finish();
            const auto args = graph.create_buffer("Args").size(64).allow_read_write().allow_indirect().finish();
            const auto count = graph.create_buffer("Count").size(12).allow_read_write().allow_indirect().finish();
            const auto dispatch = graph.create_buffer("DispatchArgs").size(28).allow_read_write().allow_indirect().finish();
            graph.create_compute_pass("GenerateDispatch").root_signature(dispatch_root).read_write(0,0,0,dispatch)
                .execute([dispatch_pipeline](RDGPassContext ctx) {
                    ctx.command->set_compute_pipeline(dispatch_pipeline); ctx.command->bind_descriptor_set(ctx.descriptors[0],0);
                    ctx.command->dispatch(1,1,1);
                }).finish();
            graph.create_compute_pass("GenerateDraws").root_signature(compute_root).read(0,0,0,constants)
                .indirect_read(dispatch)
                .read_write(0,0,0,args).read_write(0,1,0,count)
                .execute([generate,dispatch](RDGPassContext ctx) {
                    ctx.command->set_compute_pipeline(generate); ctx.command->bind_descriptor_set(ctx.descriptors[0],0);
                    ctx.command->dispatch_indirect(ctx.builder->resolve(dispatch),16);
                }).finish();
            graph.create_render_pass("CountedIndexedDraw").root_signature(raster_root).indirect_read(args).indirect_read(count).index_read(ib)
                .color(0,target,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,{0,0,0,1})
                .execute([raster,args,count,ib,maximum = scenario.maximum](RDGPassContext ctx) {
                    ctx.command->set_graphics_pipeline(raster); ctx.command->set_viewport({0,0},{64,32}); ctx.command->set_scissor({0,0},{64,32});
                    ctx.command->bind_index_buffer(ctx.builder->resolve(ib),0);
                    ctx.command->draw_indexed_indirect({.arguments = ctx.builder->resolve(args),.offset = 16,.max_draw_count = maximum,
                        .stride = 24,.count_buffer = ctx.builder->resolve(count),.count_offset = 4});
                }).finish();
            REQUIRE(graph.compile());
            commands->begin_command(); REQUIRE(graph.execute_compiled(commands,0)); commands->end_command(); commands->execute();
            std::vector<uint8_t> pixels(64*32*4);
            REQUIRE(backend->create_command_context(nullptr)->read_texture(output,pixels.data(),static_cast<uint32_t>(pixels.size())));
            CHECK(test_utils::image_matches(pixels,64,[&](uint32_t x,auto) {
                return x < 32 ? (scenario.visible ? red : black) : (scenario.visible > 1 ? green : black);
            }));
        }
        CHECK_FALSE(backend->check_debug_messages("counted indexed indirect arguments, offsets and stride"));
    }
    test_utils::TestContext::reset();
}
