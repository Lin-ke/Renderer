#pragma once

#include "engine/core/math/math.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/data/render_structs.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

RHIBackendRef global_rhi_backend();

class VertexBuffer {
public:
    VertexBuffer();
    ~VertexBuffer();

    void set_position(const std::vector<Vec3>& position);
    void set_normal(const std::vector<Vec3>& normal);
    void set_tangent(const std::vector<Vec4>& tangent);
    void set_tex_coord(const std::vector<Vec2>& tex_coord);
    void set_color(const std::vector<Vec3>& color);
    void set_bone_index(const std::vector<IVec4>& bone_index);
    void set_bone_weight(const std::vector<Vec4>& bone_weight);

    RHIBufferRef position_buffer_;
    RHIBufferRef normal_buffer_;
    RHIBufferRef tangent_buffer_;
    RHIBufferRef tex_coord_buffer_;
    RHIBufferRef color_buffer_;
    RHIBufferRef bone_index_buffer_;
    RHIBufferRef bone_weight_buffer_;

    uint32_t vertex_id_ = 0;
    VertexInfo vertex_info_ = {
        .position_id = 0,
        .normal_id = 0,
        .tangent_id = 0,
        .tex_coord_id = 0,
        .color_id = 0,
        .bone_index_id = 0,
        .bone_weight_id = 0,
        ._padding = 0
    };

    inline uint32_t vertex_num() const { return vertex_num_; }

private:
    void set_buffer_data(void* data, uint32_t size, RHIBufferRef& buffer, uint32_t& id, uint32_t slot);

    uint32_t vertex_num_ = 0;
};
using VertexBufferRef = std::shared_ptr<VertexBuffer>;

class IndexBuffer {
public:
    IndexBuffer() = default;
    ~IndexBuffer();

    void set_index(const std::vector<uint32_t>& index);

    RHIBufferRef buffer_;

    uint32_t index_id_ = 0;

    inline uint32_t index_num() const { return index_num_; }
    inline uint32_t triangle_num() const { return index_num_ / 3; }

private:
    uint32_t index_num_ = 0;
};
using IndexBufferRef = std::shared_ptr<IndexBuffer>;

template<typename Type>
class Buffer {
public:
    Buffer(ResourceType type = RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_UNIFORM_BUFFER, MemoryUsage usage = MEMORY_USAGE_CPU_TO_GPU) {
        RHIBufferInfo info = {};
        info.size = sizeof(Type);
        info.memory_usage = usage;
        info.type = type;
        info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
        buffer_ = global_rhi_backend()->create_buffer(info);
    }

    void set_data(const Type& data) {
        memcpy(buffer_->map(), &data, sizeof(Type));
        buffer_->unmap();
    }

    void set_data(const void* data, uint32_t size, uint32_t offset = 0) {
        memcpy((uint8_t*)buffer_->map() + offset, data, size);
        buffer_->unmap();
    }

    void get_data(Type* data) {
        memcpy(data, buffer_->map(), sizeof(Type));
        buffer_->unmap();
    }

    void get_data(void* data, uint32_t size, uint32_t offset = 0) {
        memcpy(data, (uint8_t*)buffer_->map() + offset, size);
        buffer_->unmap();
    }

    RHIBufferRef buffer_;
};
template<typename Type>
using BufferRef = std::shared_ptr<Buffer<Type>>;
