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

enum class MtlMaterialTypeHint {
    Default = 0,  // Use global setting or fallback
    PBR = 1,
    NPR = 2,
};

// MTL file global settings
struct MtlGlobalSettings {
    ModelMaterialType default_material_type = ModelMaterialType::PBR;
};

// MTL material data structure for manual parsing (PBR Roughness-Metallic workflow)
struct MtlMaterial {
    std::string name;
    std::string diffuse_map;
    std::string light_map;      // NPR LightMap (map_Ke)
    std::string ramp_map;       // NPR Ramp texture (map_Ramp)
    Vec4 diffuse_color{0.8f, 0.8f, 0.8f, 1.0f};  // Kd (BaseColor)
    float opacity = 1.0f;
    float roughness = 0.5f;     // R field in PBR workflow
    float metallic = 0.0f;      // M field in PBR workflow
    // Material type (overrides global setting if specified)
    MtlMaterialTypeHint material_type_hint = MtlMaterialTypeHint::Default;
    // NPR parameters
    float lambert_clamp = 0.5f;
    float ramp_offset = 0.0f;
    float rim_width = 0.5f;
    float rim_threshold = 0.1f;
    float rim_strength = 1.0f;
    Vec3 rim_color{1.0f, 1.0f, 1.0f};
};

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

static bool parse_mtl_file(const std::filesystem::path& mtl_path, 
                           std::vector<MtlMaterial>& out_materials,
                           MtlGlobalSettings& out_settings) {
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
        
        // Global MaterialType setting (can appear outside newmtl blocks)
        if (keyword == "MaterialType") {
            std::string type_str;
            iss >> type_str;
            if (type_str == "NPR" || type_str == "npr") {
                out_settings.default_material_type = ModelMaterialType::NPR;
                INFO(LogModelImporter, "MTL global MaterialType set to NPR");
            } else if (type_str == "PBR" || type_str == "pbr") {
                out_settings.default_material_type = ModelMaterialType::PBR;
                INFO(LogModelImporter, "MTL global MaterialType set to PBR");
            }
        }
        else if (keyword == "newmtl") {
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
        } else if (keyword == "Kd" && current) {
            float r, g, b;
            if (iss >> r >> g >> b) current->diffuse_color = Vec4(r, g, b, 1.0f);
        // PBR workflow: R = roughness
        } else if (keyword == "R" && current) {
            float r;
            if (iss >> r) {
                current->roughness = std::clamp(r, 0.0f, 1.0f);
            }
        // PBR workflow: M = metallic
        } else if (keyword == "M" && current) {
            float m;
            if (iss >> m) {
                current->metallic = std::clamp(m, 0.0f, 1.0f);
            }
        } else if (keyword == "d" && current) {
            float d;
            if (iss >> d) current->opacity = d;
        }
        // NPR LightMap (using map_Ke as custom extension)
        else if ((keyword == "map_Ke" || keyword == "map_lightmap") && current) {
            std::string rest;
            std::getline(iss, rest);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                current->light_map = rest.substr(start);
            }
        }
        // NPR Ramp texture
        else if ((keyword == "map_Ramp" || keyword == "map_ramp") && current) {
            std::string rest;
            std::getline(iss, rest);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                current->ramp_map = rest.substr(start);
            }
        }
        // RIM parameters
        else if (keyword == "RimWidth" && current) {
            float v;
            if (iss >> v) current->rim_width = v;
        }
        else if (keyword == "RimThreshold" && current) {
            float v;
            if (iss >> v) current->rim_threshold = v;
        }
        else if (keyword == "RimStrength" && current) {
            float v;
            if (iss >> v) current->rim_strength = v;
        }
        else if (keyword == "RimColor" && current) {
            float r, g, b;
            if (iss >> r >> g >> b) current->rim_color = Vec3(r, g, b);
        }
        // Per-material MaterialType (overrides global setting)
        else if (keyword == "MaterialType" && current) {
            std::string type_str;
            iss >> type_str;
            if (type_str == "NPR" || type_str == "npr") {
                current->material_type_hint = MtlMaterialTypeHint::NPR;
            } else if (type_str == "PBR" || type_str == "pbr") {
                current->material_type_hint = MtlMaterialTypeHint::PBR;
            }
        }
        // NPR parameters
        else if (keyword == "LambertClamp" && current) {
            float v;
            if (iss >> v) current->lambert_clamp = v;
        }
        else if (keyword == "RampOffset" && current) {
            float v;
            if (iss >> v) current->ramp_offset = v;
        }
    }
    
    INFO(LogModelImporter, "Parsed MTL file: {} materials from {} (global type: {})", 
         out_materials.size(), mtl_path.string(),
         out_settings.default_material_type == ModelMaterialType::NPR ? "NPR" : "PBR");
    return !out_materials.empty();
}

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

static std::wstring acp_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed - 1, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

static std::optional<std::filesystem::path> safe_path_from_string(const std::string& str) {
    try { return std::filesystem::path(str); } catch (...) { return std::nullopt; }
}

static std::optional<std::filesystem::path> safe_path_from_wstring(const std::wstring& wstr) {
    try { return std::filesystem::path(wstr); } catch (...) { return std::nullopt; }
}

std::shared_ptr<Model> ModelImporter::import_model(const std::string& physical_path, const std::string& virtual_path, const ModelProcessSetting& settings) {
    printf("DEBUG: ModelImporter::import_model called for %s\n", physical_path.c_str());

    source_path_ = std::filesystem::path(physical_path);
    virtual_path_ = virtual_path;
    model_name_ = source_path_.stem().string();
    output_dir_ = source_path_.parent_path();
    settings_ = settings;
    
    if (!EngineContext::asset()) {
        ERR(LogModelImporter, "AssetManager not initialized, cannot import model");
        return nullptr;
    }
    
    texture_cache_.clear();
    material_cache_.clear();
    mesh_cache_.clear();
    
    try {
        uint32_t process_steps = aiProcess_Triangulate | aiProcess_FixInfacingNormals;
        if (settings_.flip_uv) process_steps |= aiProcess_FlipUVs;
        if (settings_.smooth_normal) {
            process_steps |= aiProcess_DropNormals | aiProcess_GenSmoothNormals;
        } else {
            process_steps |= aiProcess_JoinIdenticalVertices | aiProcess_GenNormals;
        }
        
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
        
        std::vector<MtlMaterial> mtl_materials;
        std::unordered_map<std::string, size_t> mtl_name_to_index;
        MtlGlobalSettings mtl_settings;
        if (settings_.load_materials) {
            std::filesystem::path mtl_path = source_path_;
            mtl_path.replace_extension(".mtl");
            if (parse_mtl_file(mtl_path, mtl_materials, mtl_settings)) {
                for (size_t i = 0; i < mtl_materials.size(); ++i) {
                    mtl_name_to_index[mtl_materials[i].name] = i;
                }
                if (mtl_settings.default_material_type != ModelMaterialType::PBR || 
                    mtl_materials.empty() == false) {
                    settings_.material_type = mtl_settings.default_material_type;
                }
            }
        }
        
        std::vector<aiMesh*> process_meshes;
        process_node(scene->mRootNode, scene, process_meshes);
        
        auto model = std::make_shared<Model>(physical_path, settings);
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
    
    std::string mesh_name_safe = safe_ai_string(mesh->mName);
    std::string mesh_sub_name = "mesh_" + std::to_string(index);
    UID mesh_uid = generate_sub_asset_uid(mesh_sub_name, "mesh");
    
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string mesh_asset_path = base_path + "." + mesh_sub_name + ".asset";
    
    if (EngineContext::asset()->get_asset_immediate(mesh_uid)) {
        out_material = nullptr;
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
    
    // Mesh asset will be saved automatically when Model is saved via ASSET_DEPS
    // Just mark it dirty so it will be included in the save
    mesh_asset->mark_dirty();
    
    // Get or create material
    out_material = nullptr;
    if (settings_.load_materials && mesh->mMaterialIndex >= 0) {
        aiMaterial* ai_mat = scene->mMaterials[mesh->mMaterialIndex];
        aiString mat_name;
        ai_mat->Get(AI_MATKEY_NAME, mat_name);
        std::string mat_name_str = safe_ai_string(mat_name);
        
        // Log mesh-material mapping and material info
        INFO(LogModelImporter, "  Mesh[{}] '{}' -> Material[{}] '{}'", 
             index, mesh_name_safe, mesh->mMaterialIndex, mat_name_str);
        
        // Query material properties from FBX
        aiColor4D base_color;
        float metallic = 0.0f, roughness = 0.5f;
        aiString tex_path;
        
        if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, base_color)) {
            INFO(LogModelImporter, "    BaseColor: ({:.3f}, {:.3f}, {:.3f}, {:.3f})",
                 base_color.r, base_color.g, base_color.b, base_color.a);
        }
        if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic)) {
            INFO(LogModelImporter, "    Metallic: {:.3f}", metallic);
        }
        if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness)) {
            INFO(LogModelImporter, "    Roughness: {:.3f}", roughness);
        }
        // Query diffuse texture from FBX
        if (ai_mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString tex_path;
            if (AI_SUCCESS == ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path)) {
                INFO(LogModelImporter, "    DiffuseTexture: {}", tex_path.C_Str());
            }
        }
        
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
        

        
        // Use mesh's material index for consistent material caching
        int mat_index = (mesh->mMaterialIndex >= 0) ? static_cast<int>(mesh->mMaterialIndex) : index;
        out_material = get_or_create_material(mat_name_str, ai_mat, mtl_mat, mat_index);
    }
    
    return mesh_asset;
}

UID ModelImporter::generate_sub_asset_uid(const std::string& sub_name, const std::string& type_suffix) {
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string mat_type_str = (settings_.material_type == ModelMaterialType::NPR) ? "npr" : "pbr";
    std::string key = base_path + "::" + sub_name + "::" + type_suffix + "::" + mat_type_str;
    return UID::from_hash(key);
}

std::shared_ptr<Material> ModelImporter::get_or_create_material(
    const std::string& mat_name,
    aiMaterial* ai_mat,
    const MtlMaterial* mtl_mat,
    int mesh_index) {
    
    // Use material index from aiMesh for cache key to ensure consistent mapping
    // This ensures meshes with the same material share the same material instance
    std::string cache_key = mat_name + "_mat_" + std::to_string(mesh_index);
    
    // Check cache first (importer-local cache)
    auto cache_it = material_cache_.find(cache_key);
    if (cache_it != material_cache_.end()) {
        return cache_it->second;
    }
    
    // Deterministic UID for material (based on material index for consistency)
    UID mat_uid = generate_sub_asset_uid(cache_key, "material");
    
    std::string base_path = !virtual_path_.empty() ? virtual_path_ : source_path_.string();
    std::string mat_asset_path = base_path + "." + cache_key + ".asset";
    
    // Check if asset already exists in engine
    if (auto existing = EngineContext::asset()->get_asset_immediate(mat_uid)) {
        auto mat = std::dynamic_pointer_cast<Material>(existing);
        if (mat) {
            material_cache_[cache_key] = mat;
            return mat;
        }
    }
    
    ModelMaterialType mat_type = settings_.material_type;
    if (mtl_mat && mtl_mat->material_type_hint != MtlMaterialTypeHint::Default) {
        mat_type = (mtl_mat->material_type_hint == MtlMaterialTypeHint::NPR) 
                   ? ModelMaterialType::NPR 
                   : ModelMaterialType::PBR;
    }
    
    std::shared_ptr<Material> material;
    if (mat_type == ModelMaterialType::NPR) {
        auto npr_mat = std::make_shared<NPRMaterial>();
        // Set NPR parameters from MTL or defaults
        if (mtl_mat) {
            npr_mat->set_lambert_clamp(mtl_mat->lambert_clamp);
            npr_mat->set_ramp_offset(mtl_mat->ramp_offset);
            npr_mat->set_rim_threshold(mtl_mat->rim_threshold);
            npr_mat->set_rim_strength(mtl_mat->rim_strength);
            npr_mat->set_rim_width(mtl_mat->rim_width);
            npr_mat->set_rim_color(mtl_mat->rim_color);
        } else {
            // Set default NPR parameters
            npr_mat->set_lambert_clamp(0.5f);
            npr_mat->set_ramp_offset(0.0f);
            npr_mat->set_rim_threshold(0.1f);
            npr_mat->set_rim_strength(1.0f);
            npr_mat->set_rim_width(0.5f);
            npr_mat->set_rim_color(Vec3(1.0f, 1.0f, 1.0f));
        }
        material = npr_mat;
    } else {
        material = std::make_shared<PBRMaterial>();
    }
    material->set_uid(mat_uid);
    
    if (mat_type == ModelMaterialType::NPR) {
        auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
        if (npr_mat) npr_mat->set_diffuse(Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
        if (pbr_mat) pbr_mat->set_diffuse(Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    
    if (mat_type == ModelMaterialType::PBR) {
        auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
        if (pbr_mat) {
            // Use R and M fields from PBR workflow
            if (mtl_mat) {
                pbr_mat->set_roughness(mtl_mat->roughness);
                pbr_mat->set_metallic(mtl_mat->metallic);
            } else {
                pbr_mat->set_roughness(0.5f);
                pbr_mat->set_metallic(0.0f);
            }
        }
    }
    if (mtl_mat && !mtl_mat->diffuse_map.empty()) {
        try {
            std::filesystem::path tex_path = output_dir_ / mtl_mat->diffuse_map;
            if (settings_.force_png_texture) {
                tex_path.replace_extension(".png");
            }
            if (std::filesystem::exists(tex_path)) {
                auto texture = std::make_shared<Texture>(tex_path.string());
                // Texture constructor already sets UID from path and marks dirty
                // Set texture based on material type
                if (mat_type == ModelMaterialType::NPR) {
                    auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
                    if (npr_mat) npr_mat->set_diffuse_texture(texture);
                } else {
                    auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
                    if (pbr_mat) pbr_mat->set_diffuse_texture(texture);
                }
            }
        } catch (const std::exception& e) {
            ERR(LogModelImporter, "Failed to load diffuse texture: {}", e.what());
        } catch (...) {
            ERR(LogModelImporter, "Failed to load diffuse texture: unknown error");
        }
        
        // Load NPR LightMap texture from MTL
        if (mat_type == ModelMaterialType::NPR && !mtl_mat->light_map.empty()) {
            try {
                std::filesystem::path lightmap_path = output_dir_ / mtl_mat->light_map;
                if (settings_.force_png_texture) {
                    lightmap_path.replace_extension(".png");
                }
                if (std::filesystem::exists(lightmap_path)) {
                    auto lightmap_tex = std::make_shared<Texture>(lightmap_path.string());
                    auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
                    if (npr_mat) npr_mat->set_light_map_texture(lightmap_tex);
                }
            } catch (const std::exception& e) {
                ERR(LogModelImporter, "Failed to load light map texture: {}", e.what());
            } catch (...) {
                ERR(LogModelImporter, "Failed to load light map texture: unknown error");
            }
        }
        
        // Load NPR Ramp texture from MTL
        if (mat_type == ModelMaterialType::NPR && !mtl_mat->ramp_map.empty()) {
            try {
                std::filesystem::path ramp_path = output_dir_ / mtl_mat->ramp_map;
                if (settings_.force_png_texture) {
                    ramp_path.replace_extension(".png");
                }
                if (std::filesystem::exists(ramp_path)) {
                    auto ramp_tex = std::make_shared<Texture>(ramp_path.string());
                    auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
                    if (npr_mat) npr_mat->set_ramp_texture(ramp_tex);
                }
            } catch (const std::exception& e) {
                ERR(LogModelImporter, "Failed to load ramp texture: {}", e.what());
            } catch (...) {
                ERR(LogModelImporter, "Failed to load ramp texture: unknown error");
            }
        }
    } else if (!mtl_mat) {
        // Fall back to FBX material data for texture
        auto tex = load_material_texture(ai_mat, (aiTextureType)1 /* aiTextureType_DIFFUSE */);
        if (tex) {
            if (mat_type == ModelMaterialType::NPR) {
                auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
                if (npr_mat) npr_mat->set_diffuse_texture(tex);
            } else {
                auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
                if (pbr_mat) pbr_mat->set_diffuse_texture(tex);
            }
        }
        
        // For PBR, also load metallic/roughness from FBX
        if (mat_type == ModelMaterialType::PBR) {
            auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
            if (pbr_mat) {
                float metallic = 0.0f, roughness = 0.5f;
                ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
                ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
                if (roughness < 0.001f) roughness = 0.5f;
                pbr_mat->set_metallic(metallic);
                pbr_mat->set_roughness(roughness);
            }
        }
        
        // Load diffuse color from FBX
        aiColor4D base_color;
        if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, base_color)) {
            if (mat_type == ModelMaterialType::NPR) {
                auto npr_mat = std::dynamic_pointer_cast<NPRMaterial>(material);
                if (npr_mat) npr_mat->set_diffuse(Vec4(base_color.r, base_color.g, base_color.b, base_color.a));
            } else {
                auto pbr_mat = std::dynamic_pointer_cast<PBRMaterial>(material);
                if (pbr_mat) pbr_mat->set_diffuse(Vec4(base_color.r, base_color.g, base_color.b, base_color.a));
            }
        }
    }
    
    material->mark_dirty();
    
    material_cache_[cache_key] = material;
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

        std::filesystem::path found_path;
        if (auto p = safe_path_from_string(texture_name)) {
            // FBX 路径策略：准备多种可能的路径进行尝试
            std::vector<std::filesystem::path> try_paths = {
                *p,                              // 1. 尝试原始路径 (如果是相对路径或是恰好存在的绝对路径)
                output_dir_ / *p,                // 2. 尝试相对于模型所在目录
                output_dir_ / p->filename()      // 3. [FBX关键] 忽略原路径的目录，只用文件名在模型同级目录找
            };

            for (auto try_path : try_paths) {
                // 补上缺失的 PNG 强制转换逻辑
                if (settings_.force_png_texture) {
                    try_path.replace_extension(".png");
                }
                
                if (std::filesystem::exists(try_path)) {
                    found_path = try_path;
                    break; // 找到了就跳出
                }
            }
        }

        if (found_path.empty()) {
            WARN(LogModelImporter, "Failed to find texture for: {}", texture_name);
            continue; // 尝试下一个索引
        }
        
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
        info.offset.m[0][0] = m.a1; info.offset.m[0][1] = m.b1; info.offset.m[0][2] = m.c1; info.offset.m[0][3] = m.d1;
        info.offset.m[1][0] = m.a2; info.offset.m[1][1] = m.b2; info.offset.m[1][2] = m.c2; info.offset.m[1][3] = m.d2;
        info.offset.m[2][0] = m.a3; info.offset.m[2][1] = m.b3; info.offset.m[2][2] = m.c3; info.offset.m[2][3] = m.d3;
        info.offset.m[3][0] = m.a4; info.offset.m[3][1] = m.b4; info.offset.m[3][2] = m.c4; info.offset.m[3][3] = m.d4;
        bones.push_back(info);
        
        // Process weights
        for (uint32_t w = 0; w < bone->mNumWeights; ++w) {
            uint32_t vertex_id = bone->mWeights[w].mVertexId;
            float weight = bone->mWeights[w].mWeight;
            
            // Find first empty slot
            for (int i = 0; i < 4; ++i) {
                if (bone_indices[vertex_id][i] < 0) {
                    bone_indices[vertex_id][i] = bone_idx;
                    bone_weights[vertex_id][i] = weight;
                    break;
                }
            }
        }
    }
    
    target_mesh->set_bones(bones);
}
