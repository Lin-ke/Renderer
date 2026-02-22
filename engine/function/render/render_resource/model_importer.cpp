#include "engine/function/render/render_resource/model_importer.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <Windows.h>
#endif

DEFINE_LOG_TAG(LogModelImporter, "ModelImporter");

// MTL material data structure for manual parsing
struct MtlMaterial {
    std::string name;
    std::string diffuse_map;
    Vec4 ambient_color{0.2f, 0.2f, 0.2f, 1.0f};
    Vec4 diffuse_color{0.8f, 0.8f, 0.8f, 1.0f};
    Vec4 specular_color{0.0f, 0.0f, 0.0f, 1.0f};
    float shininess = 32.0f;
    float opacity = 1.0f;
    float roughness = 0.5f;
};

// Helper function to safely convert aiString to std::string
static std::string safe_ai_string(const aiString& ai_str) {
    const char* data = ai_str.C_Str();
    std::string result;
    result.reserve(ai_str.length);
    for (size_t i = 0; i < ai_str.length; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c >= 32 && c < 127) {
            result += static_cast<char>(c);
        } else {
            result += '?';
        }
    }
    return result;
}

// Parse MTL file manually
static bool parse_mtl_file(const std::filesystem::path& mtl_path, std::vector<MtlMaterial>& out_materials) {
    std::ifstream file(mtl_path);
    if (!file.is_open()) {
        WARN(LogModelImporter, "Failed to open MTL file: {}", mtl_path.string());
        return false;
    }
    
    out_materials.clear();
    MtlMaterial* current = nullptr;
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "newmtl") {
            out_materials.emplace_back();
            current = &out_materials.back();
            iss >> current->name;
        } else if (keyword == "map_Kd" && current) {
            std::string rest;
            std::getline(iss, rest);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                current->diffuse_map = rest.substr(start);
            }
        } else if (keyword == "Ka" && current) {
            float r, g, b;
            if (iss >> r >> g >> b) current->ambient_color = Vec4(r, g, b, 1.0f);
        } else if (keyword == "Kd" && current) {
            float r, g, b;
            if (iss >> r >> g >> b) current->diffuse_color = Vec4(r, g, b, 1.0f);
        } else if (keyword == "Ks" && current) {
            float r, g, b;
            if (iss >> r >> g >> b) current->specular_color = Vec4(r, g, b, 1.0f);
        } else if (keyword == "Ns" && current) {
            float ns;
            if (iss >> ns) {
                current->shininess = ns;
                current->roughness = std::clamp(std::sqrt(2.0f / (ns + 2.0f)), 0.01f, 1.0f);
            }
        } else if (keyword == "d" && current) {
            float d;
            if (iss >> d) current->opacity = d;
        }
    }
    
    INFO(LogModelImporter, "Parsed MTL file: {} materials from {}", out_materials.size(), mtl_path.string());
    return !out_materials.empty();
}

// Helper to convert UTF-8 to wide string
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

// Helper to convert local encoding to wide string
static std::wstring acp_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed - 1, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

// Safe path creation helpers
static std::optional<std::filesystem::path> safe_path_from_string(const std::string& str) {
    try { return std::filesystem::path(str); } catch (...) { return std::nullopt; }
}

static std::optional<std::filesystem::path> safe_path_from_wstring(const std::wstring& wstr) {
    try { return std::filesystem::path(wstr); } catch (...) { return std::nullopt; }
}

// ============================================================================
// ModelImporter Implementation
// ============================================================================

std::shared_ptr<Model> ModelImporter::import_model(const std::string& physical_path, const std::string& virtual_path, const ModelProcessSetting& settings) {
    // DEBUG PRINT
    printf("DEBUG: ModelImporter::import_model called for %s\n", physical_path.c_str());

    source_path_ = std::filesystem::path(physical_path);
    virtual_path_ = virtual_path;
    model_name_ = source_path_.stem().string();
    output_dir_ = source_path_.parent_path();
    settings_ = settings;
    
    // Check if asset manager is available
    if (!EngineContext::asset()) {
        ERR(LogModelImporter, "AssetManager not initialized, cannot import model");
        return nullptr;
    }
    
    // Clear caches
    texture_cache_.clear();
    material_cache_.clear();
    mesh_cache_.clear();
    
    try {
        // Set up Assimp flags
        uint32_t process_steps = aiProcess_Triangulate | aiProcess_FixInfacingNormals;
        if (settings_.flip_uv) process_steps |= aiProcess_FlipUVs;
        if (settings_.smooth_normal) {
            process_steps |= aiProcess_DropNormals | aiProcess_GenSmoothNormals;
        } else {
            process_steps |= aiProcess_JoinIdenticalVertices | aiProcess_GenNormals;
        }
        
        // Import with Assimp
        Assimp::Importer importer;
        const aiScene* scene = nullptr;
        
        std::string absolute_path = source_path_.string();
        scene = importer.ReadFile(absolute_path, process_steps);
        
        if (!scene && !source_path_.is_absolute()) {
            std::ifstream file(source_path_, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::vector<char> buffer(size);
                if (file.read(buffer.data(), size)) {
                    std::string ext = source_path_.extension().string();
                    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                    scene = importer.ReadFileFromMemory(buffer.data(), size, process_steps, ext.c_str());
                }
            }
        }
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            ERR(LogModelImporter, "Assimp load error: {}", importer.GetErrorString());
            return nullptr;
        }
        
        // Parse MTL if available
        std::vector<MtlMaterial> mtl_materials;
        std::unordered_map<std::string, size_t> mtl_name_to_index;
        if (settings_.load_materials) {
            std::filesystem::path mtl_path = source_path_;
            mtl_path.replace_extension(".mtl");
            if (parse_mtl_file(mtl_path, mtl_materials)) {
                for (size_t i = 0; i < mtl_materials.size(); ++i) {
                    mtl_name_to_index[mtl_materials[i].name] = i;
                }
            }
        }
        
        // Collect meshes
        std::vector<aiMesh*> process_meshes;
        process_node(scene->mRootNode, scene, process_meshes);
        
        // Create new Model asset
        auto model = std::make_shared<Model>(physical_path, settings);
        // Use hash of virtual path if available for deterministic UID, otherwise physical
        std::string uid_seed = virtual_path.empty() ? physical_path : virtual_path;
        model->set_uid(UID::from_hash(uid_seed)); 
        
        // Process each mesh
        for (int i = 0; i < process_meshes.size(); i++) {
            aiMesh* mesh = process_meshes[i];
            std::string mesh_name_safe = safe_ai_string(mesh->mName);
            INFO(LogModelImporter, "[{}/{}] Processing mesh [{}]", i, scene->mNumMeshes, mesh_name_safe);
            
            std::shared_ptr<Material> material;
            std::shared_ptr<Mesh> mesh_asset = process_mesh(mesh, scene, i, mtl_materials, mtl_name_to_index, material);
            
            if (mesh_asset) {
                model->add_slot(mesh_asset, material);
            }
        }
        
        return model;
        
    } catch (const std::exception& e) {
        ERR(LogModelImporter, "Exception in import_model: {}", e.what());
        return nullptr;
    }
}

void ModelImporter::process_node(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& process_meshes) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        process_meshes.push_back(scene->mMeshes[node->mMeshes[i]]);
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        process_node(node->mChildren[i], scene, process_meshes);
    }
}

std::shared_ptr<Mesh> ModelImporter::process_mesh(
    aiMesh* mesh, 
    const aiScene* scene, 
    int index,
    const std::vector<MtlMaterial>& mtl_materials,
    const std::unordered_map<std::string, size_t>& mtl_name_to_index,
    std::shared_ptr<Material>& out_material) {
    
    // Generate deterministic UID and path for this mesh
    std::string mesh_sub_name = "mesh_" + std::to_string(index);
    UID mesh_uid = generate_sub_asset_uid(mesh_sub_name, "mesh");
    
    // Construct virtual path for the mesh asset
    // If original model was "/Game/models/bunny.obj", mesh is "/Game/models/bunny.obj.mesh_0.asset"
    // Use virtual path if available, otherwise physical path (which might fail save_asset)
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string mesh_asset_path = base_path + "." + mesh_sub_name + ".asset";
    
    // Check if mesh is already cached or loaded
    if (EngineContext::asset()->get_asset_immediate(mesh_uid)) {
        out_material = nullptr; // Material will be handled separately if mesh is cached
        // Note: For full correctness we should probably still try to get the material if requested
    }

    // Extract vertex data
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec4> tangents;
    std::vector<Vec2> tex_coords;
    std::vector<uint32_t> indices;
    
    positions.reserve(mesh->mNumVertices);
    normals.reserve(mesh->mNumVertices);
    tex_coords.reserve(mesh->mNumVertices);
    
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        positions.emplace_back(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        
        if (mesh->HasNormals()) {
            normals.emplace_back(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        } else {
            normals.emplace_back(0.0f, 1.0f, 0.0f); // Default normal
        }
        
        if (mesh->HasTangentsAndBitangents()) {
            tangents.emplace_back(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 1.0f);
        } else {
            tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f); // Default tangent
        }
        
        if (mesh->HasTextureCoords(0)) {
            tex_coords.emplace_back(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        } else {
            tex_coords.emplace_back(0.0f, 0.0f); // Ensure UV count matches position count
        }
    }
    
    // Extract indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }
    
    // Create Mesh asset
    std::string mesh_name = model_name_ + "_" + mesh_sub_name;
    auto mesh_asset = Mesh::Create(positions, normals, tangents, tex_coords, indices, mesh_name);
    mesh_asset->set_uid(mesh_uid);
    
    // Extract bone weights if present
    if (mesh->HasBones()) {
        extract_bone_weights(mesh_asset, mesh, scene);
    }
    
    // Save Mesh Asset
    // Using save_asset will write to disk AND register in AssetManager
    EngineContext::asset()->save_asset(mesh_asset, mesh_asset_path);
    
    // Get or create material
    out_material = nullptr;
    if (settings_.load_materials && mesh->mMaterialIndex >= 0) {
        aiMaterial* ai_mat = scene->mMaterials[mesh->mMaterialIndex];
        aiString mat_name;
        ai_mat->Get(AI_MATKEY_NAME, mat_name);
        std::string mat_name_str = safe_ai_string(mat_name);
        
        // Look up MTL data
        const MtlMaterial* mtl_mat = nullptr;
        if (!mtl_materials.empty()) {
            // First try name match
            auto it = mtl_name_to_index.find(mat_name_str);
            if (it != mtl_name_to_index.end()) {
                mtl_mat = &mtl_materials[it->second];
            } else {
                // Fallback: use material index to map to MTL (m0, m1, m2...)
                std::string mtl_name = "m" + std::to_string(mesh->mMaterialIndex);
                auto it2 = mtl_name_to_index.find(mtl_name);
                if (it2 != mtl_name_to_index.end()) {
                    mtl_mat = &mtl_materials[it2->second];
                }
            }
        }
        
        out_material = get_or_create_material(mat_name_str, ai_mat, mtl_mat);
    }
    
    return mesh_asset;
}

UID ModelImporter::generate_sub_asset_uid(const std::string& sub_name, const std::string& type_suffix) {
    // Determine UID by hashing the virtual path + sub-name if available
    // Otherwise fallback to source physical path
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string key = base_path + "::" + sub_name + "::" + type_suffix;
    return UID::from_hash(key);
}

std::shared_ptr<Material> ModelImporter::get_or_create_material(
    const std::string& mat_name,
    aiMaterial* ai_mat,
    const MtlMaterial* mtl_mat) {
    
    // Check cache first (importer-local cache)
    auto cache_it = material_cache_.find(mat_name);
    if (cache_it != material_cache_.end()) {
        return cache_it->second;
    }
    
    // Deterministic UID for material
    UID mat_uid = generate_sub_asset_uid(mat_name, "material");
    
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string mat_asset_path = base_path + "." + mat_name + ".asset";
    
    // Check if asset already exists in engine
    if (auto existing = EngineContext::asset()->get_asset_immediate(mat_uid)) {
        auto mat = std::dynamic_pointer_cast<Material>(existing);
        if (mat) {
            material_cache_[mat_name] = mat;
            return mat;
        }
    }
    
    // Create new PBR material
    auto material = std::make_shared<PBRMaterial>();
    material->set_uid(mat_uid);
    
    if (mtl_mat) {
        // Use MTL data (preferred, no encoding issues)
        material->set_diffuse(Vec4(1.0f, 1.0f, 1.0f, 1.0f)); // Use white to see texture clearly
        material->set_roughness(0.5f); // Use moderate roughness
        material->set_metallic(0.0f); // MTL doesn't have metallic
        
        // Load diffuse texture from MTL path
        if (!mtl_mat->diffuse_map.empty()) {
            try {
                std::filesystem::path tex_path = output_dir_ / mtl_mat->diffuse_map;
                if (settings_.force_png_texture) {
                    tex_path.replace_extension(".png");
                }
                if (std::filesystem::exists(tex_path)) {
                    auto texture = std::make_shared<Texture>(tex_path.string());
                    material->set_diffuse_texture(texture);
                }
            } catch (...) {}
        }
    } else {
        // Fall back to FBX material data
        float metallic = 0.0f, roughness = 0.5f;
        aiColor4D base_color;
        
        ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
        
        if (roughness < 0.001f) roughness = 0.5f;
        
        material->set_metallic(metallic);
        material->set_roughness(roughness);
        
        if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, base_color)) {
            material->set_diffuse(Vec4(base_color.r, base_color.g, base_color.b, base_color.a));
        }
        
        auto tex = load_material_texture(ai_mat, (aiTextureType)1 /* aiTextureType_DIFFUSE */);
        if (tex) material->set_diffuse_texture(tex);
    }
    
    // Save Material Asset
    EngineContext::asset()->save_asset(material, mat_asset_path);
    
    material_cache_[mat_name] = material;
    return material;
}

std::shared_ptr<Texture> ModelImporter::load_material_texture(aiMaterial* mat, aiTextureType type) {
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
        aiString str;
        mat->GetTexture(type, i, &str);
        std::string texture_name = str.C_Str();
        
        // Check cache
        auto cache_it = texture_cache_.find(texture_name);
        if (cache_it != texture_cache_.end()) {
            return cache_it->second;
        }

        // Try find file
        std::filesystem::path found_path;
        if (auto p = safe_path_from_string(texture_name)) {
            std::filesystem::path try_path = output_dir_ / *p;
            if (std::filesystem::exists(try_path)) found_path = try_path;
        }

        if (found_path.empty()) continue;
        
        auto texture = std::make_shared<Texture>(found_path.string());
        texture_cache_[texture_name] = texture;
        return texture;
    }
    return nullptr;
}

void ModelImporter::extract_bone_weights(std::shared_ptr<Mesh> target_mesh, aiMesh* mesh, const aiScene* scene) {
    if (!mesh->HasBones() || !target_mesh) return;
    
    // Initialize bone data arrays
    uint32_t vertex_count = target_mesh->get_vertex_count();
    std::vector<IVec4> bone_indices(vertex_count, IVec4(-1, -1, -1, -1));
    std::vector<Vec4> bone_weights(vertex_count, Vec4::Zero());
    std::vector<Mesh::BoneInfo> bones;
    
    for (uint32_t bone_idx = 0; bone_idx < mesh->mNumBones; ++bone_idx) {
        aiBone* bone = mesh->mBones[bone_idx];
        
        Mesh::BoneInfo info;
        info.index = bone_idx;
        info.name = bone->mName.C_Str();
        
        // Convert offset matrix
        aiMatrix4x4 m = bone->mOffsetMatrix;
        info.offset << m.a1, m.b1, m.c1, m.d1,
                       m.a2, m.b2, m.c2, m.d2,
                       m.a3, m.b3, m.c3, m.d3,
                       m.a4, m.b4, m.c4, m.d4;
        bones.push_back(info);
        
        // Process weights
        for (uint32_t w = 0; w < bone->mNumWeights; ++w) {
            uint32_t vertex_id = bone->mWeights[w].mVertexId;
            float weight = bone->mWeights[w].mWeight;
            
            // Find first empty slot
            for (int i = 0; i < 4; ++i) {
                if (bone_indices[vertex_id](i) < 0) {
                    bone_indices[vertex_id](i) = bone_idx;
                    bone_weights[vertex_id](i) = weight;
                    break;
                }
            }
        }
    }
    
    target_mesh->set_bones(bones);
}
