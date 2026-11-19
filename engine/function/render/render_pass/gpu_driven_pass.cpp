#include "gpu_driven_pass.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/main/engine_context.h"
#include <algorithm>

namespace render {

bool GpuDrivenPass::init() {
    auto backend = EngineContext::rhi();
    if (!backend) return false;
    const auto shader = [&](const char* entry, ShaderFrequency frequency, const char* profile) {
        auto source = ShaderUtils::load_shader_source("gpu_driven.hlsl");
        if (!source) return RHIShaderRef{};
        return backend->create_shader({.frequency = frequency, .code = backend->compile_shader(source->c_str(), entry, profile)});
    };
    const auto reset = shader("ResetCS", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    const auto cull = shader("CullCS", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    const auto vertex = shader("VSMain", SHADER_FREQUENCY_VERTEX, "vs_5_0");
    const auto fragment = shader("PSMain", SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
    if (!reset || !cull || !vertex || !fragment) return false;
    RHIRootSignatureInfo reset_layout, cull_layout, raster_layout;
    reset_layout.add_entry_from_reflect(reset); cull_layout.add_entry_from_reflect(cull);
    raster_layout.add_entry_from_reflect(vertex).add_entry_from_reflect(fragment);
    reset_root_ = backend->create_root_signature(reset_layout);
    cull_root_ = backend->create_root_signature(cull_layout);
    raster_root_ = backend->create_root_signature(raster_layout);
    if (!reset_root_ || !cull_root_ || !raster_root_) return false;
    reset_pipeline_ = backend->create_compute_pipeline({.compute_shader = reset, .root_signature = reset_root_});
    cull_pipeline_ = backend->create_compute_pipeline({.compute_shader = cull, .root_signature = cull_root_});
    RHIGraphicsPipelineInfo info{};
    info.vertex_shader = vertex; info.fragment_shader = fragment; info.root_signature = raster_root_;
    info.vertex_input_state.vertex_elements = {
        {.stream_index = 0, .format = FORMAT_R32G32B32_SFLOAT, .stride = sizeof(Vec3), .semantic_name = "POSITION"},
        {.stream_index = 1, .format = FORMAT_R32_UINT, .stride = sizeof(uint32_t), .use_instance_index = true, .semantic_name = "INSTANCE_ID"}};
    info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    info.depth_stencil_state.enable_depth_test = true; info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS_EQUAL;
    info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM;
    info.depth_stencil_attachment_format = FORMAT_D32_SFLOAT;
    raster_pipeline_ = backend->create_graphics_pipeline(info);
    return reset_pipeline_ && cull_pipeline_ && raster_pipeline_;
}

std::shared_ptr<GpuSceneFrame> GpuDrivenPass::build(RDGBuilder& graph, const std::vector<DrawBatch>& instances,
    const Mat4& view_projection, RDGTextureHandle color, RDGTextureHandle depth, Extent2D extent, bool debug_colors) {
    if (!raster_pipeline_) return nullptr;
    auto frame = GpuSceneFrame::create(instances, view_projection);
    if (!frame) return nullptr;
    frame->debug_colors_ = debug_colors;
    frame->instances_handle_ = graph.create_buffer("GpuSceneInstances").import(frame->instance_info_, RESOURCE_STATE_COMMON).finish();
    frame->upload_handle_ = graph.create_buffer("GpuSceneUpload").import(frame->upload_info_, RESOURCE_STATE_COMMON).finish();
    frame->view_handle_ = graph.create_buffer("GpuSceneView").import(frame->view_info_, RESOURCE_STATE_COMMON).finish();
    frame->positions_handle_ = graph.create_buffer("GpuScenePositions").import(frame->position_info_, RESOURCE_STATE_COMMON).finish();
    frame->indices_handle_ = graph.create_buffer("GpuSceneIndices").import(frame->index_info_, RESOURCE_STATE_COMMON).finish();
    const auto arguments = graph.create_buffer("GpuDrawArguments").size(uint64_t(std::max(1u,frame->instance_count())) * sizeof(RHIIndexedIndirectCommand))
        .allow_read_write().allow_indirect().finish();
    const auto visible_ids = graph.create_buffer("GpuVisibleInstances").size(uint64_t(std::max(1u,frame->instance_count())) * sizeof(uint32_t))
        .stride(sizeof(uint32_t)).allow_read_write().allow_vertex_buffer().finish();
    const auto counter = graph.create_buffer("GpuDrawCount").size(sizeof(uint32_t)).allow_read_write().allow_indirect().finish();

    auto upload = graph.create_copy_pass("GpuSceneUpload")
        .read_buffer(frame->upload_handle_).write_buffer(frame->instances_handle_)
        .write_buffer(frame->positions_handle_).write_buffer(frame->indices_handle_);
    for (size_t i = 0; i < frame->sources_.size(); ++i) {
        auto positions = graph.create_buffer("GpuSourcePositions" + std::to_string(i))
            .import(frame->sources_[i].positions, RESOURCE_STATE_COMMON).finish();
        auto indices = graph.create_buffer("GpuSourceIndices" + std::to_string(i))
            .import(frame->sources_[i].indices, RESOURCE_STATE_COMMON).finish();
        upload.read_buffer(positions).read_buffer(indices);
    }
    upload.execute([frame](RDGPassContext context) { frame->record_uploads(context); }).finish();
    graph.create_compute_pass("GpuResetDrawCount").root_signature(reset_root_)
        .read_write(0,2,0,counter)
        .execute([pipeline = reset_pipeline_](RDGPassContext context) {
            context.command->set_compute_pipeline(pipeline);
            context.command->bind_descriptor_set(context.descriptors[0],0);
            context.command->dispatch(1,1,1);
        }).finish();
    graph.create_compute_pass("GpuCullAndBuildArgs").root_signature(cull_root_)
        .read(0,0,0,frame->view_handle_)
        .read(0,0,0,frame->instances_handle_,0,0,RESOURCE_TYPE_BUFFER)
        .read_write(0,0,0,arguments).read_write(0,1,0,visible_ids).read_write(0,2,0,counter)
        .execute([pipeline = cull_pipeline_, count = frame->instance_count()](RDGPassContext context) {
            context.command->set_compute_pipeline(pipeline);
            context.command->bind_descriptor_set(context.descriptors[0],0);
            if (count) context.command->dispatch((count+63)/64,1,1);
        }).finish();
    graph.create_render_pass("GpuDrivenRaster").root_signature(raster_root_)
        .read(0,0,0,frame->view_handle_)
        .read(0,0,0,frame->instances_handle_,0,0,RESOURCE_TYPE_BUFFER)
        .vertex_read(frame->positions_handle_).vertex_read(visible_ids).index_read(frame->indices_handle_)
        .indirect_read(arguments).indirect_read(counter)
        .color(0,color,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,{0,0,0,1})
        .depth_stencil(depth,ATTACHMENT_LOAD_OP_CLEAR,ATTACHMENT_STORE_OP_STORE,1,0)
        .execute([frame, pipeline = raster_pipeline_, extent, arguments, visible_ids, counter](RDGPassContext context) {
            auto command = context.command;
            command->set_graphics_pipeline(pipeline);
            command->set_viewport({0,0},{extent.width,extent.height});
            command->set_scissor({0,0},{extent.width,extent.height});
            command->bind_vertex_buffer(context.builder->resolve(frame->positions_handle_),0,0);
            command->bind_vertex_buffer(context.builder->resolve(visible_ids),1,0);
            command->bind_index_buffer(context.builder->resolve(frame->indices_handle_),0);
            command->bind_descriptor_set(context.descriptors[0],0);
            command->draw_indexed_indirect({.arguments = context.builder->resolve(arguments), .max_draw_count = frame->instance_count(),
                .count_buffer = context.builder->resolve(counter)});
        }).finish();
    return frame;
}

bool GpuDrivenPass::prepare(GpuSceneFrame& frame, RDGBuilder& graph, uint32_t frame_slot) {
    return scene_.prepare(frame,frame_slot) && frame.bind(graph);
}

}
