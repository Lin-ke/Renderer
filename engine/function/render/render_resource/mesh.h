#pragma once

#include "engine/core/math/math.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/function/render/render_resource/buffer.h"
#include "engine/function/asset/asset.h"
#include "engine/function/asset/asset_macros.h"
#include <cereal/access.hpp>
#include <cereal/types/memory.hpp>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief CPU-side mesh data structure containing vertex attributes and bone information
 * 
 * This is a standalone asset that can be shared between Models.
 * For example, LOD meshes or instanced rendering can reuse the same Mesh asset.
 */
class Mesh : public Asset {
public:
    Mesh() = default;
    explicit Mesh(const std::string& name);
    ~Mesh();

    virtual std::string_view get_asset_type_name() const override { return "Mesh Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Mesh; }
    virtual void on_load() override;
    virtual void on_save() override;

    /**
     * @brief Load a mesh from a mesh asset file
     * @param path Virtual path to the mesh asset
     * @return Shared pointer to the loaded mesh
     */
    static std::shared_ptr<Mesh> Load(const std::string& path);

    /**
     * @brief Create mesh from raw vertex data
     * @param positions Vertex positions
     * @param normals Vertex normals
     * @param tangents Vertex tangents
     * @param tex_coords Texture coordinates
     * @param indices Index data
     * @return Shared pointer to the created mesh
     */
    static std::shared_ptr<Mesh> Create(
        const std::vector<Vec3>& positions,
        const std::vector<Vec3>& normals,
        const std::vector<Vec4>& tangents,
        const std::vector<Vec2>& tex_coords,
        const std::vector<uint32_t>& indices,
        const std::string& name = ""
    );

    // Data access
    inline const std::vector<Vec3>& get_positions() const { return position_; }
    inline const std::vector<Vec3>& get_normals() const { return normal_; }
    inline const std::vector<Vec4>& get_tangents() const { return tangent_; }
    inline const std::vector<Vec2>& get_tex_coords() const { return tex_coord_; }
    inline const std::vector<Vec3>& get_colors() const { return color_; }
    inline const std::vector<uint32_t>& get_indices() const { return index_; }
    inline const std::vector<IVec4>& get_bone_indices() const { return bone_index_; }
    inline const std::vector<Vec4>& get_bone_weights() const { return bone_weight_; }

    // Buffer access
    inline VertexBufferRef get_vertex_buffer() const { return vertex_buffer_; }
    inline IndexBufferRef get_index_buffer() const { return index_buffer_; }

    // Bounding volumes
    inline const BoundingBox& get_bounding_box() const { return bounding_box_; }
    inline const BoundingSphere& get_bounding_sphere() const { return bounding_sphere_; }

    // Statistics
    inline uint32_t get_vertex_count() const { return static_cast<uint32_t>(position_.size()); }
    inline uint32_t get_index_count() const { return static_cast<uint32_t>(index_.size()); }
    inline const std::string& get_mesh_name() const { return name_; }

    /**
     * @brief Set mesh data and create GPU buffers
     * @param positions Vertex positions (required)
     * @param normals Vertex normals (optional)
     * @param tangents Vertex tangents (optional)
     * @param tex_coords Texture coordinates (optional)
     * @param colors Vertex colors (optional)
     * @param indices Index data (required)
     */
    void set_data(
        const std::vector<Vec3>& positions,
        const std::vector<uint32_t>& indices,
        const std::vector<Vec3>& normals = {},
        const std::vector<Vec4>& tangents = {},
        const std::vector<Vec2>& tex_coords = {},
        const std::vector<Vec3>& colors = {}
    );

    /**
     * @brief Merge another mesh into this one
     * @param other The mesh to merge from
     */
    void merge(const Mesh& other);

    // Bone information for skeletal animation
    struct BoneInfo {
        int index;
        std::string name;
        Mat4 offset;
        
        template<class Archive>
        void serialize(Archive& ar) {
            ar(cereal::make_nvp("index", index));
            ar(cereal::make_nvp("name", name));
            ar(cereal::make_nvp("offset", offset));
        }
    };
    
    inline const std::vector<BoneInfo>& get_bones() const { return bones_; }
    void set_bones(const std::vector<BoneInfo>& bones) { bones_ = bones; }

    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("name", name_));
        ar(cereal::make_nvp("position", position_));
        ar(cereal::make_nvp("normal", normal_));
        ar(cereal::make_nvp("tangent", tangent_));
        ar(cereal::make_nvp("tex_coord", tex_coord_));
        ar(cereal::make_nvp("color", color_));
        ar(cereal::make_nvp("bone_index", bone_index_));
        ar(cereal::make_nvp("bone_weight", bone_weight_));
        ar(cereal::make_nvp("index", index_));
        ar(cereal::make_nvp("bones", bones_));
        ar(cereal::make_nvp("bounding_box", bounding_box_));
        ar(cereal::make_nvp("bounding_sphere", bounding_sphere_));
    }

private:
    // CPU data
    std::string name_;
    std::vector<Vec3> position_;
    std::vector<Vec3> normal_;
    std::vector<Vec4> tangent_;
    std::vector<Vec2> tex_coord_;
    std::vector<Vec3> color_;
    std::vector<IVec4> bone_index_;
    std::vector<Vec4> bone_weight_;
    std::vector<uint32_t> index_;
    std::vector<BoneInfo> bones_;

    // Bounding volumes
    BoundingBox bounding_box_;
    BoundingSphere bounding_sphere_;

    // GPU resources (transient, not serialized)
    VertexBufferRef vertex_buffer_;
    IndexBufferRef index_buffer_;

    void create_gpu_buffers();
    void calculate_bounds();

    friend class cereal::access;
};

using MeshRef = std::shared_ptr<Mesh>;
