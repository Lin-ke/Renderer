#pragma once

#include "engine/function/render/graph/rdg_builder.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include <array>
#include <memory>
#include <vector>

namespace render {

struct GpuGeometrySource {
    RHIBufferRef positions;
    RHIBufferRef indices;
    bool operator==(const GpuGeometrySource&) const = default;
};

struct GpuSceneInstance {
    Mat4 model;
    Vec4 bounds_min;
    Vec4 bounds_max;
    Vec4 color;
    uint32_t index_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t object_id;
};
static_assert(sizeof(GpuSceneInstance) == 128);

struct GpuSceneView {
    Mat4 view_projection;
    uint32_t instance_count;
    uint32_t padding[3]{};
};
static_assert(sizeof(GpuSceneView) == 80);

// Created from an immutable packet on Render; physical bindings are filled on Device.
class GpuSceneFrame {
public:
    static std::shared_ptr<GpuSceneFrame> create(const std::vector<DrawBatch>& instances, const Mat4& view_projection);
    bool bind(RDGBuilder& graph) const;
    void record_uploads(RDGPassContext context) const;
    uint32_t instance_count() const { return static_cast<uint32_t>(instances_.size()); }

private:
    friend class GpuScene;
    friend class GpuDrivenPass;
    std::vector<DrawBatch> instances_;
    std::vector<GpuGeometrySource> sources_;
    std::vector<uint32_t> geometry_indices_;
    uint32_t position_bytes_ = sizeof(Vec3), index_bytes_ = sizeof(uint32_t);
    GpuSceneView view_{};
    RHIBufferInfo instance_info_{}, upload_info_{}, position_info_{}, index_info_{}, view_info_{};
    RDGBufferHandle instances_handle_{UINT32_MAX}, upload_handle_{UINT32_MAX}, positions_handle_{UINT32_MAX},
        indices_handle_{UINT32_MAX}, view_handle_{UINT32_MAX};
    RHIBufferRef instances_buffer_, upload_buffer_, positions_buffer_, indices_buffer_, view_buffer_;
    struct Copy {
        RHIBufferRef source, destination;
        uint64_t destination_offset, bytes;
    };
    std::vector<Copy> copies_;
    bool prepared_ = false;
    bool debug_colors_ = false;
};

// Device-owned scene data. Frame buffers may be updated only after the slot fence.
class GpuScene {
public:
    bool prepare(GpuSceneFrame& frame, uint32_t frame_slot);
private:
    struct FrameBuffers { RHIBufferRef upload, instances, view; };
    std::array<FrameBuffers, FRAMES_IN_FLIGHT> frames_;
    std::vector<GpuGeometrySource> sources_;
    RHIBufferRef positions_, indices_;
};

}
