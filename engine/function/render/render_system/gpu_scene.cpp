#include "gpu_scene.h"
#include "engine/function/render/render_pass/draw_material.h"
#include "engine/main/engine_context.h"
#include "engine/core/utils/profiler.h"
#include <algorithm>
#include <cstring>
#include <limits>

DEFINE_LOG_TAG(LogGpuScene, "GpuScene");

namespace render {

std::shared_ptr<GpuSceneFrame> GpuSceneFrame::create(const std::vector<DrawBatch>& instances, const Mat4& view_projection) {
    PROFILE_FUNCTION();
    // One-dimensional SM5 dispatch, with 64 candidates per group.
    if (instances.size() > 65535u * 64u) {
        ERR(LogGpuScene, "GPU Driven candidate count exceeds the single-dispatch capacity");
        return nullptr;
    }
    auto frame = std::make_shared<GpuSceneFrame>();
    frame->instances_ = instances;
    frame->view_.view_projection = view_projection;
    frame->view_.instance_count = static_cast<uint32_t>(instances.size());
    uint64_t position_bytes = 0, index_bytes = 0;
    for (const auto& instance : instances) {
        if (!instance.vertex_buffer || !instance.index_buffer) return nullptr;
        const auto& positions = instance.vertex_buffer->get_info();
        const auto& indices = instance.index_buffer->get_info();
        if (positions.memory_usage != MEMORY_USAGE_GPU_ONLY || indices.memory_usage != MEMORY_USAGE_GPU_ONLY ||
            positions.stride != sizeof(Vec3) || indices.stride != sizeof(uint32_t) ||
            positions.size % sizeof(Vec3) || !positions.size || indices.size % sizeof(uint32_t) ||
            uint64_t(instance.index_offset) + instance.index_count > indices.size / sizeof(uint32_t)) {
            ERR(LogGpuScene, "GPU Driven solid rendering requires immutable float3 positions and uint32 indices");
            return nullptr;
        }
        GpuGeometrySource source{instance.vertex_buffer, instance.index_buffer};
        auto found = std::find(frame->sources_.begin(), frame->sources_.end(), source);
        const auto geometry_index = static_cast<uint32_t>(found - frame->sources_.begin());
        frame->geometry_indices_.push_back(geometry_index);
        if (found == frame->sources_.end()) {
            frame->sources_.push_back(std::move(source));
            position_bytes += positions.size; index_bytes += indices.size;
        }
    }
    if (position_bytes > uint64_t(INT32_MAX) * sizeof(Vec3) || position_bytes > UINT32_MAX || index_bytes > UINT32_MAX) return nullptr;
    frame->position_bytes_ = static_cast<uint32_t>(std::max<uint64_t>(position_bytes, sizeof(Vec3)));
    frame->index_bytes_ = static_cast<uint32_t>(std::max<uint64_t>(index_bytes, sizeof(uint32_t)));
    frame->instance_info_ = {.size = uint64_t(std::max(1u, frame->instance_count())) * sizeof(GpuSceneInstance),
        .stride = sizeof(GpuSceneInstance), .memory_usage = MEMORY_USAGE_GPU_ONLY, .type = RESOURCE_TYPE_BUFFER};
    frame->upload_info_ = {.size = frame->instance_info_.size, .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_BUFFER};
    frame->position_info_ = {.size = frame->position_bytes_, .stride = sizeof(Vec3),
        .memory_usage = MEMORY_USAGE_GPU_ONLY, .type = RESOURCE_TYPE_VERTEX_BUFFER | RESOURCE_TYPE_BUFFER};
    frame->index_info_ = {.size = frame->index_bytes_, .stride = sizeof(uint32_t),
        .memory_usage = MEMORY_USAGE_GPU_ONLY, .type = RESOURCE_TYPE_INDEX_BUFFER | RESOURCE_TYPE_BUFFER};
    frame->view_info_ = {.size = sizeof(GpuSceneView), .memory_usage = MEMORY_USAGE_CPU_TO_GPU, .type = RESOURCE_TYPE_UNIFORM_BUFFER};
    return frame;
}

bool GpuScene::prepare(GpuSceneFrame& frame, uint32_t frame_slot) {
    PROFILE_FUNCTION();
    auto backend = EngineContext::rhi();
    if (!backend || frame.prepared_) return false;
    auto& resources = frames_[frame_slot % frames_.size()];
    auto ensure = [&](RHIBufferRef& buffer, const RHIBufferInfo& info) {
        if (!buffer || buffer->get_info().size < info.size) buffer = backend->create_buffer(info);
        return bool(buffer);
    };
    if (!ensure(resources.upload, frame.upload_info_) || !ensure(resources.instances, frame.instance_info_) ||
        !ensure(resources.view, frame.view_info_)) return false;
    const bool upload_geometry = !positions_ || frame.sources_ != sources_;
    auto positions = positions_, indices = indices_;
    if (upload_geometry) {
        positions = backend->create_buffer(frame.position_info_);
        indices = backend->create_buffer(frame.index_info_);
        if (!positions || !indices) return false;
    }
    std::vector<uint32_t> vertex_offsets, index_offsets;
    uint64_t position_offset = 0, index_offset = 0;
    for (const auto& source : frame.sources_) {
        vertex_offsets.push_back(static_cast<uint32_t>(position_offset / sizeof(Vec3)));
        index_offsets.push_back(static_cast<uint32_t>(index_offset / sizeof(uint32_t)));
        if (upload_geometry) {
            frame.copies_.push_back({source.positions, positions, position_offset, source.positions->get_info().size});
            frame.copies_.push_back({source.indices, indices, index_offset, source.indices->get_info().size});
        }
        position_offset += source.positions->get_info().size;
        index_offset += source.indices->get_info().size;
    }
    std::vector<GpuSceneInstance> data(std::max(1u, frame.instance_count()));
    for (size_t i = 0; i < frame.instances_.size(); ++i) {
        const auto& input = frame.instances_[i]; auto& output = data[i];
        output.model = input.model_matrix;
        output.bounds_min = Vec4(input.bounds.min.x, input.bounds.min.y, input.bounds.min.z, 0);
        output.bounds_max = Vec4(input.bounds.max.x, input.bounds.max.y, input.bounds.max.z, 0);
        output.color = input.material_snapshot && input.material_snapshot->pbr ?
            input.material_snapshot->pbr->albedo : Vec4(1,1,1,1);
        if (frame.debug_colors_) {
            uint32_t hash = input.object_id * 0x9e3779b9u;
            hash ^= hash >> 16;
            const auto channel = [&](uint32_t shift) { return 0.2f + 0.8f * float((hash >> shift) & 255u) / 255.0f; };
            output.color = Vec4(channel(0),channel(8),channel(16),1);
        }
        output.index_count = input.index_count;
        output.first_index = index_offsets[frame.geometry_indices_[i]] + input.index_offset;
        output.vertex_offset = static_cast<int32_t>(vertex_offsets[frame.geometry_indices_[i]]);
        output.object_id = input.object_id;
    }
    auto* mapped = resources.upload->map(); if (!mapped) return false;
    memcpy(mapped, data.data(), size_t(frame.instance_info_.size));
    resources.upload->unmap_written_range(0, frame.instance_info_.size);
    mapped = resources.view->map(); if (!mapped) return false;
    memcpy(mapped, &frame.view_, sizeof(frame.view_)); resources.view->unmap();
    if (upload_geometry) {
        positions_ = std::move(positions); indices_ = std::move(indices); sources_ = frame.sources_;
    }
    frame.upload_buffer_ = resources.upload; frame.instances_buffer_ = resources.instances;
    frame.view_buffer_ = resources.view; frame.positions_buffer_ = positions_; frame.indices_buffer_ = indices_;
    frame.copies_.push_back({resources.upload, resources.instances, 0, frame.instance_info_.size});
    frame.prepared_ = true;
    return true;
}

bool GpuSceneFrame::bind(RDGBuilder& graph) const {
    return prepared_ && graph.bind_external_buffer(instances_handle_, instances_buffer_) &&
        graph.bind_external_buffer(upload_handle_, upload_buffer_) && graph.bind_external_buffer(view_handle_, view_buffer_) &&
        graph.bind_external_buffer(positions_handle_, positions_buffer_) && graph.bind_external_buffer(indices_handle_, indices_buffer_);
}

void GpuSceneFrame::record_uploads(RDGPassContext context) const {
    for (const auto& copy : copies_)
        context.command->copy_buffer(copy.source, 0, copy.destination, copy.destination_offset, copy.bytes);
}

}
