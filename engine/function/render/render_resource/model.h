#pragma once

#include "engine/function/render/render_resource/buffer.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/asset/asset.h"
#include "engine/function/render/data/render_structs.h"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// Forward declarations for Assimp
struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;
enum aiTextureType;

/**
 * @brief CPU-side mesh data structure containing vertex attributes and bone information
 */
struct Mesh {
    std::vector<Vec3> position;
    std::vector<Vec3> normal;
    std::vector<Vec4> tangent;
    std::vector<Vec2> tex_coord;
    std::vector<Vec3> color;
    std::vector<IVec4> bone_index;
    std::vector<Vec4> bone_weight;
    std::vector<uint32_t> index;
    
    // Bounding volumes
    BoundingBox box;
    BoundingSphere sphere;
    
    // Bone information for skeletal animation
    struct BoneInfo {
        int index;
        std::string name;
        Mat4 offset;
    };
    std::vector<BoneInfo> bone;
    
    std::string name;
    
    /**
     * @brief Merge another mesh into this one
     * @param other The mesh to merge from
     */
    void merge(const Mesh& other);
    
    /**
     * @brief Get the number of triangles in the mesh
     * @return Number of triangles
     */
    inline uint32_t triangle_num() const { return static_cast<uint32_t>(index.size() / 3); }
};

// Forward declaration
struct MeshCluster;
using MeshClusterRef = std::shared_ptr<MeshCluster>;
struct VirtualMesh;
using VirtualMeshRef = std::shared_ptr<VirtualMesh>;

/**
 * @brief Model cache asset for storing generated cluster data
 * ####TODO#### Full implementation with VirtualMesh serialization
 */
class ModelCache : public Asset {
public:
    virtual std::string_view get_asset_type_name() const override { return "ModelCache Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::ModelCache; }
    
    ModelCache() = default;
    
private:
    template<class Archive>
    void serialize(Archive& ar) {
        //####TODO#### Implement cache serialization when VirtualMesh is available
    }
    
    friend class cereal::access;
};

/**
 * @brief Settings for model import processing
 */
struct ModelProcessSetting {
    bool smooth_normal = false;           // Generate smooth normals
    bool flip_uv = false;                 // Flip UV coordinates
    bool load_materials = false;          // Load materials from file
    bool tangent_space = false;           // Generate tangent space
    bool generate_bvh = false;            // Generate BVH acceleration structure
    bool generate_cluster = false;        // Generate mesh clusters
    bool generate_virtual_mesh = false;   // Generate virtual geometry (Nanite-like)
    bool cache_cluster = false;           // Cache cluster data to avoid regeneration
    bool force_png_texture = false;       // Force texture to use .png extension (for unsupported formats)
    
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
 * @brief Submesh data containing CPU mesh data and GPU buffers
 */
struct SubmeshData {
    std::shared_ptr<Mesh> mesh;                          // CPU mesh data
    std::vector<MeshClusterRef> clusters;                // Clusters for clustered rendering
    VirtualMeshRef virtual_mesh;                         // Virtual mesh data
    
    VertexBufferRef vertex_buffer;                       // GPU vertex buffer
    IndexBufferRef index_buffer;                         // GPU index buffer
    
    IndexRange mesh_cluster_id = { 0, 0 };               // Allocated cluster ID range
    IndexRange mesh_cluster_group_id = { 0, 0 };         // Allocated cluster group ID range
    
    // Ray tracing BLAS
    //####TODO#### RHIBottomLevelAccelerationStructureRef blas;
};

/**
 * @brief Model asset class representing a 3D model with multiple submeshes
 * 
 * Model is managed by AssetManager for caching and lifecycle management.
 * Use Model::Load() to load/create models with automatic caching.
 */
class Model : public Asset {
public:
    /**
     * @brief Construct a model from file path with processing settings
     * @param path Path to the model file
     * @param process_setting Processing settings for import
     */
    Model(std::string path, ModelProcessSetting process_setting);
    
    ~Model();

    virtual std::string_view get_asset_type_name() const override { return "Model Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Model; }

    virtual void on_load_asset() override;
    virtual void on_save_asset() override;
    
    /**
     * @brief Load a model from file path with caching via AssetManager
     * @param path Path to the model file (relative to ENGINE_PATH or absolute)
     * @param process_setting Processing settings for import
     * @return Shared pointer to the loaded model (cached if already loaded)
     */
    static std::shared_ptr<Model> Load(const std::string& path, 
                                        const ModelProcessSetting& process_setting = ModelProcessSetting());
    
    /**
     * @brief Load a model from file path with caching via AssetManager
     * Convenience overload that takes path and individual settings
     * @param path Path to the model file
     * @param smooth_normal Generate smooth normals
     * @param load_materials Load materials from file
     * @param flip_uv Flip UV coordinates
     * @return Shared pointer to the loaded model
     */
    static std::shared_ptr<Model> Load(const std::string& path, 
                                        bool smooth_normal = true,
                                        bool load_materials = false,
                                        bool flip_uv = false);

    /**
     * @brief Get the number of submeshes
     * @return Number of submeshes
     */
    inline uint32_t get_submesh_count() const { return static_cast<uint32_t>(submeshes_.size()); }
    
    /**
     * @brief Get submesh data by index
     * @param index Submesh index
     * @return Const reference to submesh data
     */
    const SubmeshData& submesh(uint32_t index) const { return submeshes_[index]; }
    
    /**
     * @brief Get vertex buffer for a submesh
     * @param submesh_index Submesh index
     * @return Vertex buffer reference
     */
    VertexBufferRef get_vertex_buffer(uint32_t submesh_index) { return submeshes_[submesh_index].vertex_buffer; }
    
    /**
     * @brief Get index buffer for a submesh
     * @param submesh_index Submesh index
     * @return Index buffer reference
     */
    IndexBufferRef get_index_buffer(uint32_t submesh_index) { return submeshes_[submesh_index].index_buffer; }
    
    /**
     * @brief Get material for a submesh
     * @param submesh_index Submesh index
     * @return Material reference
     */
    MaterialRef get_material(uint32_t submesh_index) { 
        if (submesh_index < materials_.size()) return materials_[submesh_index];
        return nullptr;
    }

protected:
    /**
     * @brief Load model data from file using Assimp
     * @param path File path
     * @return true if successful
     */
    bool load_from_file(std::string path);
    
    /**
     * @brief Process a node in the Assimp scene graph
     * @param node Assimp node
     * @param scene Assimp scene
     * @param process_meshes Output list of meshes to process
     */
    void process_node(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& process_meshes);
    
    /**
     * @brief Process a single mesh
     * @param mesh Assimp mesh
     * @param scene Assimp scene
     * @param index Submesh index
     */
    void process_mesh(aiMesh* mesh, const aiScene* scene, int index);
    
    /**
     * @brief Extract bone weights from Assimp mesh
     * @param submesh Output mesh structure
     * @param mesh Assimp mesh
     * @param scene Assimp scene
     */
    void extract_bone_weights(Mesh* submesh, aiMesh* mesh, const aiScene* scene);
    
    /**
     * @brief Load material texture from Assimp material
     * @param mat Assimp material
     * @param type Texture type
     * @return Loaded texture or nullptr
     */
    std::shared_ptr<Texture> load_material_texture(aiMaterial* mat, aiTextureType type);

protected:
    std::vector<SubmeshData> submeshes_;
    std::vector<MaterialRef> materials_;
    std::shared_ptr<ModelCache> cache_;
    
    std::string path_;
    ModelProcessSetting process_setting_;
    
    // Statistics
    uint64_t total_index_ = 0;
    uint64_t total_vertex_ = 0;
    uint32_t total_cluster_count_ = 0;
    uint32_t total_cluster_max_mip_ = 0;
    
    // Texture cache to avoid duplicate loading
    std::unordered_map<std::string, TextureRef> texture_map_;

private:
    Model() = default;
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("path", path_));
        ar(cereal::make_nvp("process_setting", process_setting_));
    }
    
    friend class cereal::access;
};
using ModelRef = std::shared_ptr<Model>;
