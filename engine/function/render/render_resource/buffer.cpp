#include "engine/function/render/render_resource/buffer.h"
#include "engine/core/math/math.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi_structs.h"
// #include "engine/function/render/render_resource/render_resource_manager.h" //####TODO####
#include "engine/function/render/data/render_structs.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

RHIBackendRef global_rhi_backend() {
    return EngineContext::rhi();
}

VertexBuffer::VertexBuffer() {
    // vertex_id_ = EngineContext::render_resource()->allocate_vertex_id(); //####TODO####
}

VertexBuffer::~VertexBuffer() {
    /* //####TODO####
    if (!EngineContext::destroyed()) {
        if (vertex_info_.position_id != 0) EngineContext::render_resource()->release_bindless_id(vertex_info_.position_id, BINDLESS_SLOT_POSITION);
        // ... (other releases)
        if (vertex_id_ != 0) EngineContext::render_resource()->release_vertex_id(vertex_id_);
    }
    */
}

void VertexBuffer::set_buffer_data(void* data, uint32_t size, RHIBufferRef& buffer, uint32_t& id, uint32_t slot) {
    if (size == 0) return;
    if (!buffer || buffer->get_info().size < size) {
        RHIBufferInfo info = {};
        info.size = size;
        info.stride = (size > 0 && vertex_num_ > 0) ? (size / vertex_num_) : 0;
        info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
        info.type = RESOURCE_TYPE_VERTEX_BUFFER;
        info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
        buffer = EngineContext::rhi()->create_buffer(info);

        /* //####TODO####
        if (id != 0) EngineContext::render_resource()->release_bindless_id(id, (BindlessSlot)slot);
        id = EngineContext::render_resource()->allocate_bindless_id({
            .resource_type = RESOURCE_TYPE_RW_BUFFER,
            .buffer = buffer,
            .buffer_offset = 0,
            .buffer_range = size},
            (BindlessSlot)slot);
        */
    }

    void* mapped = buffer->map();
    if(mapped) {
        memcpy(mapped, data, size);
        buffer->unmap();
    }

    // EngineContext::render_resource()->set_vertex_info(vertex_info_, vertex_id_); //####TODO####
}

void VertexBuffer::set_position(const std::vector<Vec3>& position) {
    vertex_num_ = position.size();
    set_buffer_data((void*)position.data(), position.size() * sizeof(Vec3), position_buffer_, vertex_info_.position_id, 0); // Slot 0
}

void VertexBuffer::set_normal(const std::vector<Vec3>& normal) {
    set_buffer_data((void*)normal.data(), normal.size() * sizeof(Vec3), normal_buffer_, vertex_info_.normal_id, 1);
}

void VertexBuffer::set_tangent(const std::vector<Vec4>& tangent) {
    set_buffer_data((void*)tangent.data(), tangent.size() * sizeof(Vec4), tangent_buffer_, vertex_info_.tangent_id, 2);
}

void VertexBuffer::set_tex_coord(const std::vector<Vec2>& tex_coord) {
    set_buffer_data((void*)tex_coord.data(), tex_coord.size() * sizeof(Vec2), tex_coord_buffer_, vertex_info_.tex_coord_id, 3);
}

void VertexBuffer::set_color(const std::vector<Vec3>& color) {
    set_buffer_data((void*)color.data(), color.size() * sizeof(Vec3), color_buffer_, vertex_info_.color_id, 4);
}

void VertexBuffer::set_bone_index(const std::vector<IVec4>& bone_index) {
    set_buffer_data((void*)bone_index.data(), bone_index.size() * sizeof(IVec4), bone_index_buffer_, vertex_info_.bone_index_id, 5);
}

void VertexBuffer::set_bone_weight(const std::vector<Vec4>& bone_weight) {
    set_buffer_data((void*)bone_weight.data(), bone_weight.size() * sizeof(Vec4), bone_weight_buffer_, vertex_info_.bone_weight_id, 6);
}

IndexBuffer::~IndexBuffer() {
    // if (!EngineContext::destroyed() && index_id_ != 0) EngineContext::render_resource()->release_bindless_id(index_id_, BINDLESS_SLOT_INDEX); //####TODO####
}

void IndexBuffer::set_index(const std::vector<uint32_t>& index) {
    index_num_ = index.size();
    uint32_t size = index.size() * sizeof(uint32_t);

    if (size == 0) return;
    if (!buffer_ || buffer_->get_info().size < size) {
        RHIBufferInfo info = {};
        info.size = size;
        info.stride = sizeof(uint32_t);
        info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
        info.type = RESOURCE_TYPE_INDEX_BUFFER;
        info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
        buffer_ = EngineContext::rhi()->create_buffer(info);

        /* //####TODO####
        if (index_id_ != 0) EngineContext::render_resource()->release_bindless_id(index_id_, BINDLESS_SLOT_INDEX);
        index_id_ = EngineContext::render_resource()->allocate_bindless_id({
            .resource_type = RESOURCE_TYPE_RW_BUFFER,
            .buffer = buffer_,
            .buffer_offset = 0,
            .buffer_range = size},
            BINDLESS_SLOT_INDEX);
        */
    }

    void* mapped = buffer_->map();
    if(mapped) {
        memcpy(mapped, index.data(), size);
        buffer_->unmap();
    }
}
