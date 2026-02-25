#include "engine/function/render/render_resource/mesh.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"

#include <cassert>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>

DECLARE_LOG_TAG(LogMesh);
DEFINE_LOG_TAG(LogMesh, "Mesh");

CEREAL_REGISTER_TYPE(Mesh)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Mesh)

Mesh::Mesh(const std::string& name) : name_(name) {
}

Mesh::~Mesh() {
}

void Mesh::on_load() {
    create_gpu_buffers();
}

void Mesh::on_save() {
}

std::shared_ptr<Mesh> Mesh::Load(const std::string& path) {
    if (!EngineContext::asset()) {
        ERR(LogMesh, "AssetManager not initialized");
        return nullptr;
    }
    return EngineContext::asset()->load_asset<Mesh>(path);
}

std::shared_ptr<Mesh> Mesh::Create(
    const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals,
    const std::vector<Vec4>& tangents,
    const std::vector<Vec2>& tex_coords,
    const std::vector<uint32_t>& indices,
    const std::string& name) {
    
    auto mesh = std::make_shared<Mesh>(name);
    mesh->set_data(positions, indices, normals, tangents, tex_coords);
    return mesh;
}

void Mesh::set_data(
    const std::vector<Vec3>& positions,
    const std::vector<uint32_t>& indices,
    const std::vector<Vec3>& normals,
    const std::vector<Vec4>& tangents,
    const std::vector<Vec2>& tex_coords,
    const std::vector<Vec3>& colors) {
    
    position_ = positions;
    normal_ = normals;
    tangent_ = tangents;
    tex_coord_ = tex_coords;
    color_ = colors;
    index_ = indices;

    if (!bone_index_.empty()) {
        bone_index_.resize(position_.size(), IVec4(-1, -1, -1, -1));
        bone_weight_.resize(position_.size(), Vec4::Zero());
    }

    calculate_bounds();
    create_gpu_buffers();
    mark_dirty();
}

void Mesh::create_gpu_buffers() {
    if (!EngineContext::rhi()) {
        return;
    }

    if (!position_.empty()) {
        vertex_buffer_ = std::make_shared<VertexBuffer>();
        vertex_buffer_->set_position(position_);
        if (!normal_.empty()) {
            vertex_buffer_->set_normal(normal_);
        }
        if (!tangent_.empty()) {
            vertex_buffer_->set_tangent(tangent_);
        }
        if (!tex_coord_.empty()) {
            vertex_buffer_->set_tex_coord(tex_coord_);
        }
        if (!color_.empty()) {
            vertex_buffer_->set_color(color_);
        }
        if (!bone_index_.empty()) {
            vertex_buffer_->set_bone_index(bone_index_);
        }
        if (!bone_weight_.empty()) {
            vertex_buffer_->set_bone_weight(bone_weight_);
        }
    }

    if (!index_.empty()) {
        index_buffer_ = std::make_shared<IndexBuffer>();
        index_buffer_->set_index(index_);
    }
}

void Mesh::calculate_bounds() {
    if (position_.empty()) {
        bounding_box_ = BoundingBox{};
        bounding_sphere_ = BoundingSphere{};
        return;
    }

    bounding_box_.min = position_[0];
    bounding_box_.max = position_[0];
    
    for (const auto& pos : position_) {
        bounding_box_.min = bounding_box_.min.cwiseMin(pos);
        bounding_box_.max = bounding_box_.max.cwiseMax(pos);
    }

    Vec3 center = (bounding_box_.min + bounding_box_.max) * 0.5f;
    float radius = (bounding_box_.max - bounding_box_.min).norm() * 0.5f;
    bounding_sphere_ = BoundingSphere{center, radius};
}

void Mesh::merge(const Mesh& other) {
    assert(!other.position_.empty() && "Mesh::merge: cannot merge empty mesh");

    uint32_t vertex_offset = static_cast<uint32_t>(position_.size());

    position_.insert(position_.end(), other.position_.begin(), other.position_.end());
    normal_.insert(normal_.end(), other.normal_.begin(), other.normal_.end());
    tangent_.insert(tangent_.end(), other.tangent_.begin(), other.tangent_.end());
    tex_coord_.insert(tex_coord_.end(), other.tex_coord_.begin(), other.tex_coord_.end());
    color_.insert(color_.end(), other.color_.begin(), other.color_.end());
    bone_index_.insert(bone_index_.end(), other.bone_index_.begin(), other.bone_index_.end());
    bone_weight_.insert(bone_weight_.end(), other.bone_weight_.begin(), other.bone_weight_.end());

    for (uint32_t idx : other.index_) {
        index_.push_back(idx + vertex_offset);
    }

    calculate_bounds();
    create_gpu_buffers();
    mark_dirty();
}
