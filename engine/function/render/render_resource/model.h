#pragma once

#include "engine/function/render/render_resource/buffer.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/function/asset/asset.h"
#include "engine/function/asset/asset_macros.h"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

/**
 * @brief Material type for model import
 */
enum class ModelMaterialType {
    PBR = 0,    // Physically Based Rendering
    NPR = 1,    // Non-Photorealistic Rendering (Toon/Cel shading)
};

/**
 * @brief Settings for model import processing
 */
struct ModelProcessSetting {
    bool smooth_normal = false;           // Generate smooth normals
    bool flip_uv = true;                  // Flip UV coordinates (default true for DirectX)
    bool load_materials = false;          // Load materials from file
    bool tangent_space = false;           // Generate tangent space
    bool generate_bvh = false;            // Generate BVH acceleration structure
    bool generate_cluster = false;        // Generate mesh clusters
    bool generate_virtual_mesh = false;   // Generate virtual geometry (Nanite-like)
    bool cache_cluster = false;           // Cache cluster data to avoid regeneration
    bool force_png_texture = false;       // Force texture to use .png extension (for unsupported formats)
    ModelMaterialType material_type = ModelMaterialType::PBR;  // Material type to create
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("smooth_normal", smooth_normal));
        ar(cereal::make_nvp("flip_uv", flip_uv));
        ar(cereal::make_nvp("load_materials", load_materials));
        ar(cereal::make_nvp("tangent_space", tangent_space));
        ar(cereal::make_nvp("generate_bvh", generate_bvh));
        ar(cereal::make_nvp("generate_cluster", generate_cluster));
        ar(cereal::make_nvp("generate_virtual_mesh", generate_virtual_mesh));
        ar(cereal::make_nvp("cache_cluster", cache_cluster));
        ar(cereal::make_nvp("force_png_texture", force_png_texture));
        ar(cereal::make_nvp("material_type", material_type));
    }
};

/**
 * @brief Index range for cluster/group ID allocation
 */
struct IndexRange {
    uint32_t begin = 0;
    uint32_t end = 0;
};

/**
 * @brief Material slot that binds a Material to a Mesh
 * Runtime structure - pointers are resolved from deps after loading
 */
struct MaterialSlot {
    MeshRef mesh_;           // The mesh geometry (resolved from model's mesh_deps)
    MaterialRef material_;   // The material to render with (resolved from model's material_deps)
    uint32_t mesh_index_ = 0;     // Index into mesh_deps
    uint32_t material_index_ = 0; // Index into material_deps
    uint32_t slot_index_ = 0;     // Original slot index for tracking
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("mesh_index", mesh_index_));
        ar(cereal::make_nvp("material_index", material_index_));
        ar(cereal::make_nvp("slot_index", slot_index_));
    }
};

/**
 * @brief Model asset - a collection of Mesh + Material bindings
 * 
 * Model is a lightweight container that references:
 * - Mesh assets (geometry data, GPU buffers)
 * - Material assets (shaders, textures, parameters)
 * 
 * Dependencies are managed via ASSET_DEPS macro for automatic serialization
 */
class Model : public Asset {
public:
    Model() = default;
    Model(std::string path, ModelProcessSetting process_setting);
    ~Model();

    virtual std::string_view get_asset_type_name() const override { return "Model Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Model; }

    virtual void on_load() override;
    virtual void on_save() override;

    // Asset Dependencies - managed by ASSET_DEPS macro
    ASSET_DEPS(
        (std::vector<MeshRef>, mesh_deps_),
        (std::vector<MaterialRef>, material_deps_)
    )
    
    /**
     * @brief Load a model from file path with caching via AssetManager
     * Uses ModelImporter internally for non-native formats.
     */
    static std::shared_ptr<Model> Load(const std::string& virtual_path, 
                                        const ModelProcessSetting& process_setting = ModelProcessSetting(),
                                        const UID& explicit_uid = UID::empty());
    
    /**
     * @brief Convenience overload with material type support
     */
    static std::shared_ptr<Model> Load(const std::string& path, 
                                        bool smooth_normal = true,
                                        bool load_materials = false,
                                        bool flip_uv = false,
                                        ModelMaterialType material_type = ModelMaterialType::PBR,
                                        const UID& explicit_uid = UID::empty());

    /**
     * @brief Create a model from existing mesh and material assets
     */
    static std::shared_ptr<Model> Create(MeshRef mesh, MaterialRef material);

    /**
     * @brief Create a model from multiple mesh-material pairs
     */
    static std::shared_ptr<Model> Create(const std::vector<std::pair<MeshRef, MaterialRef>>& slots);

    // Material slot access
    inline uint32_t get_slot_count() const { return static_cast<uint32_t>(material_slots_.size()); }
    inline const MaterialSlot& get_slot(uint32_t index) const { return material_slots_[index]; }
    inline MaterialSlot& get_slot(uint32_t index) { return material_slots_[index]; }
    
    void add_slot(MeshRef mesh, MaterialRef material);
    void set_material(uint32_t slot_index, MaterialRef material_);
    
    MaterialRef get_material(uint32_t slot_index) const;
    MeshRef get_mesh(uint32_t slot_index) const;

    // Backwards compatibility helpers
    inline uint32_t get_submesh_count() const { return get_slot_count(); }
    MaterialRef get_material_compat(uint32_t index) const { return get_material(index); }
    VertexBufferRef get_vertex_buffer(uint32_t index) const;
    IndexBufferRef get_index_buffer(uint32_t index) const;

    BoundingBox get_bounding_box() const;

    // Statistics
    inline uint64_t get_total_vertex_count() const { return total_vertex_; }
    inline uint64_t get_total_index_count() const { return total_index_; }
    inline const std::string& get_source_path() const { return path_; }

protected:
    // Sync material_slots_ with mesh_deps_/material_deps_
    void sync_slots_to_deps();
    void sync_deps_to_slots();

    // Core data - just references to other assets
    std::vector<MaterialSlot> material_slots_;
    
    // Source file info
    std::string path_;
    ModelProcessSetting process_setting_;
    
    // Statistics (transient)
    uint64_t total_index_ = 0;
    uint64_t total_vertex_ = 0;
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
        serialize_deps(ar);  // Serialize mesh_deps_ and material_deps_ UIDs via ASSET_DEPS
        ar(cereal::make_nvp("material_slots", material_slots_));  // Slot indices
        ar(cereal::make_nvp("path", path_));
        ar(cereal::make_nvp("process_setting", process_setting_));
    }
    
    friend class cereal::access;
};

using ModelRef = std::shared_ptr<Model>;

CEREAL_REGISTER_TYPE(Model)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Model)
