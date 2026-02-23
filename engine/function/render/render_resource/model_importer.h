#pragma once

#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>

// Forward declarations
struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
enum aiTextureType;

struct MtlMaterial;

/**
 * @brief Handles importing of 3D model files (FBX, OBJ, etc.)
 * 
 * Separates the import process from the runtime Model asset.
 * Reads raw files, processes geometry and materials, and generates
 * native engine assets (Mesh, Material, Model).
 */
class ModelImporter {
public:
    ModelImporter() = default;
    
    /**
     * @brief Import a model from file
     * @param physical_path Absolute path to the source file
     * @param virtual_path Virtual path to the source file (for generating sub-asset paths)
     * @param settings Import settings
     * @return Shared pointer to the created Model asset
     */
    std::shared_ptr<Model> import_model(const std::string& physical_path, const std::string& virtual_path, const ModelProcessSetting& settings);

private:
    // Processing state
    std::filesystem::path source_path_;
    std::string virtual_path_; // Base virtual path for generating sub-assets
    std::filesystem::path output_dir_; // Where to save generated assets
    std::string model_name_;
    ModelProcessSetting settings_;
    
    // Caches to avoid duplicates during import
    std::unordered_map<std::string, std::shared_ptr<Texture>> texture_cache_;
    std::unordered_map<std::string, std::shared_ptr<Material>> material_cache_;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> mesh_cache_;
    
    // Internal processing methods
    void process_node(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& process_meshes);
    
    std::shared_ptr<Mesh> process_mesh(
        aiMesh* mesh, 
        const aiScene* scene, 
        int index,
        const std::vector<MtlMaterial>& mtl_materials,
        const std::unordered_map<std::string, size_t>& mtl_name_to_index,
        std::shared_ptr<Material>& out_material);
        
    std::shared_ptr<Material> get_or_create_material(
        const std::string& mat_name,
        aiMaterial* ai_mat,
        const MtlMaterial* mtl_mat,
        int mesh_index);
        
    std::shared_ptr<Texture> load_material_texture(aiMaterial* mat, aiTextureType type);
    
    void extract_bone_weights(std::shared_ptr<Mesh> target_mesh, aiMesh* mesh, const aiScene* scene);
    
    // Helper to generate a deterministic UID for a sub-asset
    UID generate_sub_asset_uid(const std::string& sub_name, const std::string& type_suffix);
};
