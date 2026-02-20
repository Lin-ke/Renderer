#include "engine/function/render/render_resource/model.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>

DECLARE_LOG_TAG(LogModel);
DEFINE_LOG_TAG(LogModel, "Model");

CEREAL_REGISTER_TYPE(Model)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Model)

CEREAL_REGISTER_TYPE(ModelCache)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, ModelCache)

void Mesh::merge(const Mesh& other) {
    // Merge vertex data
    position.insert(position.end(), other.position.begin(), other.position.end());
    normal.insert(normal.end(), other.normal.begin(), other.normal.end());
    tangent.insert(tangent.end(), other.tangent.begin(), other.tangent.end());
    tex_coord.insert(tex_coord.end(), other.tex_coord.begin(), other.tex_coord.end());
    color.insert(color.end(), other.color.begin(), other.color.end());
    bone_index.insert(bone_index.end(), other.bone_index.begin(), other.bone_index.end());
    bone_weight.insert(bone_weight.end(), other.bone_weight.begin(), other.bone_weight.end());
    
    // Merge index data with offset
    uint32_t vertex_offset = static_cast<uint32_t>(position.size());
    for (uint32_t idx : other.index) {
        index.push_back(idx + vertex_offset);
    }
}

Model::Model(std::string path, ModelProcessSetting process_setting)
    : path_(path), process_setting_(process_setting) {
    on_load_asset();
}

Model::~Model() {
    // Release allocated cluster IDs if engine is still running
    //####TODO#### Release mesh cluster IDs
}

void Model::on_load_asset() {
    // Load cache if exists
    //####TODO#### Load cache binding
    
    // Load model data from file
    if (!load_from_file(path_)) {
        ERR(LogModel, "Failed to load model from file: {}", path_);
        return;
    }
    
    // Create GPU buffers for each submesh
    for (uint32_t i = 0; i < submeshes_.size(); i++) {
        // Create vertex buffer
        VertexBufferRef vertex_buffer = std::make_shared<VertexBuffer>();
        vertex_buffer->set_position(submeshes_[i].mesh->position);
        vertex_buffer->set_normal(submeshes_[i].mesh->normal);
        vertex_buffer->set_tangent(submeshes_[i].mesh->tangent);
        vertex_buffer->set_tex_coord(submeshes_[i].mesh->tex_coord);
        vertex_buffer->set_color(submeshes_[i].mesh->color);
        vertex_buffer->set_bone_index(submeshes_[i].mesh->bone_index);
        vertex_buffer->set_bone_weight(submeshes_[i].mesh->bone_weight);
        submeshes_[i].vertex_buffer = vertex_buffer;
        
        // Create index buffer
        IndexBufferRef index_buffer = std::make_shared<IndexBuffer>();
        index_buffer->set_index(submeshes_[i].mesh->index);
        submeshes_[i].index_buffer = index_buffer;
    }
    
    // Process clusters if requested
    if (process_setting_.generate_cluster) {
        //####TODO#### Process cluster generation
        INFO(LogModel, "Cluster generation not fully implemented");
    }
    
    // Process virtual mesh if requested
    if (process_setting_.generate_virtual_mesh) {
        //####TODO#### Process virtual mesh generation
        INFO(LogModel, "Virtual mesh generation not fully implemented");
    }
    
    // Build BLAS for ray tracing if requested
    if (process_setting_.generate_bvh) {
        //####TODO#### Build BLAS
        INFO(LogModel, "BLAS generation not fully implemented");
    }
}

void Model::on_save_asset() {
    // Cache cluster data if requested
    if (process_setting_.cache_cluster) {
        if (!cache_) cache_ = std::make_shared<ModelCache>();
        
        //####TODO#### Save cluster/virtual mesh data when serialization is implemented
        INFO(LogModel, "Model cache serialization not fully implemented");
    }
}

bool Model::load_from_file(std::string path) {
    // Force smooth normal for virtual mesh generation (requires vertex deduplication)
    if (process_setting_.generate_virtual_mesh) {
        process_setting_.smooth_normal = true;
    }
    
    // Set up Assimp processing flags
    uint32_t process_steps = aiProcess_Triangulate | aiProcess_FixInfacingNormals;
    if (process_setting_.flip_uv) process_steps |= aiProcess_FlipUVs;
    if (process_setting_.smooth_normal) {
        process_steps |= aiProcess_DropNormals | aiProcess_GenSmoothNormals;
    } else {
        process_steps |= aiProcess_JoinIdenticalVertices | aiProcess_GenNormals;
    }
    
    // Get absolute file path
    std::filesystem::path fs_path(path);
    if (!fs_path.is_absolute()) {
        fs_path = std::filesystem::path(ENGINE_PATH) / path;
    }
    
    // Import scene with Assimp
    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    
    // First try direct path (works for ASCII paths)
    std::string absolute_path = fs_path.string();
    scene = importer.ReadFile(absolute_path, process_steps);
    
    // If direct path failed, try reading file to memory (supports Unicode paths)
    if (!scene) {
        // Read file into memory using filesystem path (supports Unicode)
        std::ifstream file(fs_path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            std::vector<char> buffer(size);
            if (file.read(buffer.data(), size)) {
                // Get file extension for format hint
                std::string ext = fs_path.extension().string();
                // Remove leading dot
                if (!ext.empty() && ext[0] == '.') {
                    ext = ext.substr(1);
                }
                
                // Read from memory
                scene = importer.ReadFileFromMemory(buffer.data(), size, process_steps, ext.c_str());
                INFO(LogModel, "Loaded model from memory (Unicode path support): {} bytes", size);
            }
        }
    }
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        ERR(LogModel, "Assimp load error: {}", importer.GetErrorString());
        return false;
    }
    
    // Collect all meshes from scene graph
    std::vector<aiMesh*> process_meshes;
    process_node(scene->mRootNode, scene, process_meshes);
    
    // Resize submeshes and materials
    submeshes_.resize(process_meshes.size());
    if (process_setting_.load_materials) {
        materials_.resize(process_meshes.size());
    }
    
    // Process each mesh
    for (int i = 0; i < process_meshes.size(); i++) {
        aiMesh* mesh = process_meshes[i];
        INFO(LogModel, "[{}/{}] Processing mesh [{}]", i, scene->mNumMeshes, mesh->mName.C_Str());
        process_mesh(mesh, scene, i);
    }
    
    // Clear texture map after loading
    texture_map_.clear();
    
    // Calculate statistics
    total_index_ = 0;
    total_vertex_ = 0;
    for (const auto& submesh : submeshes_) {
        total_index_ += submesh.mesh->index.size();
        total_vertex_ += submesh.mesh->position.size();
    }
    
    INFO(LogModel, "Model loaded successfully: {} vertices, {} indices", total_vertex_, total_index_);
    return true;
}

void Model::process_node(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& process_meshes) {
    // Process all meshes in this node
    for (uint32_t i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        process_meshes.push_back(mesh);
    }
    
    // Recursively process children
    for (uint32_t i = 0; i < node->mNumChildren; i++) {
        process_node(node->mChildren[i], scene, process_meshes);
    }
}

void Model::process_mesh(aiMesh* mesh, const aiScene* scene, int index) {
    std::shared_ptr<Mesh> submesh = std::make_shared<Mesh>();
    
    // Process vertex positions
    submesh->position.resize(mesh->mNumVertices);
    for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
        submesh->position[i](0) = mesh->mVertices[i].x;
        submesh->position[i](1) = mesh->mVertices[i].y;
        submesh->position[i](2) = mesh->mVertices[i].z;
    }
    
    // Process vertex normals
    if (mesh->mNormals) {
        submesh->normal.resize(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
            submesh->normal[i](0) = mesh->mNormals[i].x;
            submesh->normal[i](1) = mesh->mNormals[i].y;
            submesh->normal[i](2) = mesh->mNormals[i].z;
        }
    }
    
    // Process vertex colors (use first color set)
    if (mesh->mColors[0]) {
        submesh->color.resize(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
            submesh->color[i](0) = mesh->mColors[0][i].r;
            submesh->color[i](1) = mesh->mColors[0][i].g;
            submesh->color[i](2) = mesh->mColors[0][i].b;
        }
    }
    
    // Process texture coordinates (use first UV set)
    if (mesh->mTextureCoords[0]) {
        submesh->tex_coord.resize(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
            submesh->tex_coord[i](0) = mesh->mTextureCoords[0][i].x;
            submesh->tex_coord[i](1) = mesh->mTextureCoords[0][i].y;
        }
    }
    
    // Process indices (triangulated)
    submesh->index.resize(mesh->mNumFaces * 3);
    uint32_t index_offset = 0;
    for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (uint32_t j = 0; j < face.mNumIndices; j++) {
            submesh->index[index_offset++] = face.mIndices[j];
        }
    }
    
    // Process tangents if available
    if (mesh->mTangents) {
        submesh->tangent.resize(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
            submesh->tangent[i](0) = mesh->mTangents[i].x;
            submesh->tangent[i](1) = mesh->mTangents[i].y;
            submesh->tangent[i](2) = mesh->mTangents[i].z;
            submesh->tangent[i](3) = 1.0f; // Handedness
        }
    } else if (process_setting_.tangent_space) {
        //####TODO#### Generate tangent space
        INFO(LogModel, "Tangent space generation not fully implemented");
    }
    
    // Process materials
    if (process_setting_.load_materials && mesh->mMaterialIndex >= 0) {
        aiMaterial* ai_mat = scene->mMaterials[mesh->mMaterialIndex];
        if (materials_[index] == nullptr) {
            materials_[index] = std::make_shared<Material>();
            
            // Load PBR textures from FBX
            // Base color / Diffuse
            TextureRef diffuse = load_material_texture(ai_mat, aiTextureType_DIFFUSE);
            if (!diffuse) diffuse = load_material_texture(ai_mat, aiTextureType_BASE_COLOR);
            if (diffuse) materials_[index]->set_diffuse_texture(diffuse);
            
            // Normal map
            TextureRef normal = load_material_texture(ai_mat, aiTextureType_NORMALS);
            if (!normal) normal = load_material_texture(ai_mat, aiTextureType_NORMAL_CAMERA);
            if (!normal) normal = load_material_texture(ai_mat, aiTextureType_HEIGHT);
            if (normal) materials_[index]->set_normal_texture(normal);
            
            // Metallic/Roughness/AO - try ARM combined texture first
            TextureRef arm = load_material_texture(ai_mat, aiTextureType_SPECULAR);
            if (!arm) arm = load_material_texture(ai_mat, aiTextureType_UNKNOWN);
            if (arm) {
                materials_[index]->set_arm_texture(arm);
            } else {
                // Try separate textures
                TextureRef metallic = load_material_texture(ai_mat, aiTextureType_METALNESS);
                TextureRef roughness = load_material_texture(ai_mat, aiTextureType_DIFFUSE_ROUGHNESS);
                TextureRef ao = load_material_texture(ai_mat, aiTextureType_AMBIENT_OCCLUSION);
                TextureRef ambient = load_material_texture(ai_mat, aiTextureType_AMBIENT);
                
                // Set metallic texture if found
                if (metallic) materials_[index]->set_texture_2d(metallic, 0);
                if (roughness) materials_[index]->set_texture_2d(roughness, 1);
                if (ao) materials_[index]->set_texture_2d(ao, 2);
                else if (ambient) materials_[index]->set_texture_2d(ambient, 2);
            }
            
            // Emissive
            TextureRef emissive = load_material_texture(ai_mat, aiTextureType_EMISSIVE);
            if (!emissive) emissive = load_material_texture(ai_mat, aiTextureType_EMISSION_COLOR);
            if (emissive) materials_[index]->set_texture_2d(emissive, 3);
            
            // Load PBR material properties from FBX
            float metallic_factor = 0.0f;
            float roughness_factor = 0.5f;
            aiColor4D base_color;
            aiColor4D emissive_color;
            
            // Get AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR
            if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic_factor)) {
                materials_[index]->set_metallic(metallic_factor);
            }
            
            // Get AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR
            if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness_factor)) {
                materials_[index]->set_roughness(roughness_factor);
            }
            
            // Get base color / diffuse color
            if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, base_color)) {
                Vec4 color(base_color.r, base_color.g, base_color.b, base_color.a);
                materials_[index]->set_diffuse(color);
            } else if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_BASE_COLOR, base_color)) {
                Vec4 color(base_color.r, base_color.g, base_color.b, base_color.a);
                materials_[index]->set_diffuse(color);
            }
            
            // Get emissive color
            if (AI_SUCCESS == ai_mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive_color)) {
                Vec4 emission(emissive_color.r, emissive_color.g, emissive_color.b, emissive_color.a);
                materials_[index]->set_emission(emission);
            }
            
            INFO(LogModel, "Material loaded: metallic={}, roughness={}, hasDiffuse={}, hasNormal={}, hasARM={}",
                 metallic_factor, roughness_factor, 
                 diffuse ? "yes" : "no",
                 normal ? "yes" : "no", 
                 arm ? "yes" : "no");
        }
    }
    
    // Process bone weights
    if (mesh->HasBones()) {
        extract_bone_weights(submesh.get(), mesh, scene);
    }
    
    // Calculate bounding volumes
    if (!submesh->position.empty()) {
        submesh->box.min = submesh->position[0];
        submesh->box.max = submesh->position[0];
        for (const auto& pos : submesh->position) {
            submesh->box.min = submesh->box.min.cwiseMin(pos);
            submesh->box.max = submesh->box.max.cwiseMax(pos);
        }
        Vec3 center = (submesh->box.min + submesh->box.max) * 0.5f;
        float radius = (submesh->box.max - submesh->box.min).norm() * 0.5f;
        submesh->sphere = BoundingSphere{center, radius};
    }
    
    // Store mesh name
    submesh->name = std::string(mesh->mName.C_Str());
    
    // Store mesh in submesh data
    submeshes_[index].mesh = submesh;
    
    // Generate clusters if requested
    if (process_setting_.generate_cluster) {
        //####TODO#### Cluster generation
    }
    
    // Generate virtual mesh if requested
    if (process_setting_.generate_virtual_mesh) {
        //####TODO#### Virtual mesh generation
    }
}

void Model::extract_bone_weights(Mesh* submesh, aiMesh* mesh, const aiScene* scene) {
    //####TODO#### Full bone weight extraction
    // Initialize bone data
    submesh->bone_index.resize(mesh->mNumVertices);
    submesh->bone_weight.resize(mesh->mNumVertices);
    
    for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
        for (int j = 0; j < 4; j++) {
            submesh->bone_index[i](j) = -1;
            submesh->bone_weight[i](j) = 0.0f;
        }
    }
    
    // Simple bone weight extraction (full implementation needs proper bone index management)
    for (uint32_t bone_index = 0; bone_index < mesh->mNumBones; ++bone_index) {
        aiBone* bone = mesh->mBones[bone_index];
        std::string bone_name = bone->mName.C_Str();
        
        // Process bone weights
        for (uint32_t weight_index = 0; weight_index < bone->mNumWeights; ++weight_index) {
            uint32_t vertex_id = bone->mWeights[weight_index].mVertexId;
            float weight = bone->mWeights[weight_index].mWeight;
            
            // Find first empty slot
            for (int i = 0; i < 4; ++i) {
                if (submesh->bone_index[vertex_id](i) < 0) {
                    submesh->bone_index[vertex_id](i) = bone_index;
                    submesh->bone_weight[vertex_id](i) = weight;
                    break;
                }
            }
        }
    }
}

std::shared_ptr<Texture> Model::load_material_texture(aiMaterial* mat, aiTextureType type) {
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
        aiString str;
        mat->GetTexture(type, i, &str);
        
        // Construct full texture path
        std::filesystem::path model_dir = std::filesystem::path(path_).parent_path();
        std::string texture_path = (model_dir / str.C_Str()).string();
        
        // Force PNG extension if requested (for unsupported texture formats)
        if (process_setting_.force_png_texture) {
            texture_path = std::filesystem::path(texture_path).replace_extension(".png").string();
        }
        
        // Check texture cache
        auto iter = texture_map_.find(texture_path);
        if (iter != texture_map_.end()) {
            return iter->second;
        } else {
            // Load new texture
            INFO(LogModel, "Loading texture [{}] from FBX material...", texture_path);
            auto texture = std::make_shared<Texture>(texture_path);
            texture_map_[texture_path] = texture;
            return texture;
        }
    }
    return nullptr;
}
