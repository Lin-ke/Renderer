#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/utils/profiler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

DEFINE_LOG_TAG(LogModel, "Model");



Model::Model(std::string path, ModelProcessSetting process_setting)
    : path_(std::move(path)), process_setting_(std::move(process_setting)) {
}

Model::~Model() = default;

void Model::sync_slots_to_deps() {
    mesh_deps_.clear();
    material_deps_.clear();
    mesh_deps_.reserve(material_slots_.size());
    material_deps_.reserve(material_slots_.size());
    
    std::unordered_map<Mesh*, uint32_t> mesh_ptr_to_index;
    
    for (size_t i = 0; i < material_slots_.size(); ++i) {
        auto& slot = material_slots_[i];
        slot.slot_index_ = static_cast<uint32_t>(i);
        
        Mesh* mesh_ptr = slot.mesh_.get();
        auto mesh_it = mesh_ptr_to_index.find(mesh_ptr);
        if (mesh_it != mesh_ptr_to_index.end()) {
            slot.mesh_index_ = mesh_it->second;
        } else {
            slot.mesh_index_ = static_cast<uint32_t>(mesh_deps_.size());
            mesh_ptr_to_index[mesh_ptr] = slot.mesh_index_;
            mesh_deps_.push_back(slot.mesh_);
        }
        
        slot.material_index_ = static_cast<uint32_t>(material_deps_.size());
        material_deps_.push_back(slot.material_);
    }
}

void Model::sync_deps_to_slots() {
    size_t slot_count = std::max(mesh_deps_.size(), material_deps_.size());
    if (slot_count == 0) {
        material_slots_.clear();
        return;
    }
    
    material_slots_.resize(slot_count);
    for (size_t i = 0; i < slot_count; ++i) {
        auto& slot = material_slots_[i];
        
        if (i < mesh_deps_.size()) {
            slot.mesh_ = mesh_deps_[i];
            slot.mesh_index_ = static_cast<uint32_t>(i);
        }
        if (i < material_deps_.size()) {
            slot.material_ = material_deps_[i];
            slot.material_index_ = static_cast<uint32_t>(i);
        }
        slot.slot_index_ = static_cast<uint32_t>(i);
    }
}

void Model::on_load() {
    sync_deps_to_slots();  // Rebuild material_slots_ from deps
    
    total_vertex_ = 0;
    total_index_ = 0;
    for (const auto& slot : material_slots_) {
        if (slot.mesh_) {
            total_vertex_ += slot.mesh_->get_vertex_count();
            total_index_ += slot.mesh_->get_index_count();
        }
    }
}

void Model::on_save() {
    sync_slots_to_deps();
}

std::shared_ptr<Model> Model::Load(const std::string& virtual_path, 
                                    const ModelProcessSetting& process_setting,
                                    const UID& explicit_uid) {
    PROFILE_SCOPE("Model::Load");
    if (!EngineContext::asset()) {
        ERR(LogModel, "AssetManager not initialized");
        return nullptr;
    }

    std::string abs_path;
    
    auto physical_opt = EngineContext::asset()->get_physical_path(virtual_path);
    if (physical_opt) {
        abs_path = physical_opt.value().string();
    } else {
        abs_path = std::filesystem::absolute(virtual_path).string();
    }

    std::filesystem::path fs_path(abs_path);
    std::string ext = fs_path.extension().string();
    
    if (ext == ".asset" || ext == ".binasset") {
        UID uid = explicit_uid;
        if (uid.is_empty()) {
            uid = EngineContext::asset()->get_uid_by_path(virtual_path);
            if (uid.is_empty()) {
                 uid = EngineContext::asset()->get_uid_by_path(abs_path);
            }
        }
        
        if (!uid.is_empty()) {
            return EngineContext::asset()->load_asset<Model>(uid);
        }
        
        ERR(LogModel, "Failed to load native model asset: {} (UID not found)", virtual_path);
        return nullptr;
    }
    
    UID uid = explicit_uid.is_empty() ? EngineContext::asset()->get_uid_by_path(abs_path) : explicit_uid;
    if (uid.is_empty()) {
        uid = EngineContext::asset()->get_uid_by_path(virtual_path);
    }

    if (!uid.is_empty()) {
        auto cached = EngineContext::asset()->get_asset_immediate(uid);
        if (cached) {
            auto model = std::dynamic_pointer_cast<Model>(cached);
            if (model) {
                INFO(LogModel, "Model cache hit: {}", abs_path);
                return model;
            }
        }
    }

    // Check if already imported asset exists on disk
    std::string model_asset_path = virtual_path.empty() ? abs_path : virtual_path;
    if (!model_asset_path.ends_with(".asset") && !model_asset_path.ends_with(".binasset")) {
        model_asset_path += ".asset";
    }
    
    auto asset_physical_opt = EngineContext::asset()->get_physical_path(model_asset_path);
    if (asset_physical_opt && std::filesystem::exists(asset_physical_opt.value())) {
        // Already imported, load from asset file
        UID asset_uid = EngineContext::asset()->get_uid_by_path(model_asset_path);
        if (!asset_uid.is_empty()) {
            INFO(LogModel, "Loading existing asset: {}", model_asset_path);
            return EngineContext::asset()->load_asset<Model>(asset_uid);
        }
    }

    INFO(LogModel, "Importing model from source: {}", abs_path);
    ModelImporter importer;
    auto model = importer.import_model(abs_path, virtual_path, process_setting);
    
    if (model) {
        if (!explicit_uid.is_empty()) {
            model->set_uid(explicit_uid);
        } else if (model->get_uid().is_empty()) {
            model->set_uid(UID::from_hash(virtual_path.empty() ? abs_path : virtual_path));
        }
        
        model->sync_slots_to_deps();
        
        std::string model_asset_path = virtual_path.empty() ? abs_path : virtual_path;
        if (!model_asset_path.ends_with(".asset") && !model_asset_path.ends_with(".binasset")) {
            model_asset_path += ".asset";
        }
        EngineContext::asset()->save_asset(model, model_asset_path);
        
        INFO(LogModel, "Model imported successfully: {} slots", model->get_slot_count());
    } else {
        ERR(LogModel, "Failed to import model: {}", abs_path);
    }
    
    return model;
}

std::shared_ptr<Model> Model::Load(const std::string& path, 
                                    bool smooth_normal,
                                    bool load_materials,
                                    bool flip_uv,
                                    ModelMaterialType material_type,
                                    const UID& explicit_uid) {
    ModelProcessSetting setting;
    setting.smooth_normal = smooth_normal;
    setting.load_materials = load_materials;
    setting.flip_uv = flip_uv;
    setting.material_type = material_type;
    return Load(path, setting, explicit_uid);
}

std::shared_ptr<Model> Model::Create(MeshRef mesh, MaterialRef material) {
    auto model = std::make_shared<Model>();
    model->add_slot(mesh, material);
    return model;
}

std::shared_ptr<Model> Model::Create(const std::vector<std::pair<MeshRef, MaterialRef>>& slots) {
    auto model = std::make_shared<Model>();
    for (const auto& [mesh, material] : slots) {
        model->add_slot(mesh, material);
    }
    return model;
}

void Model::add_slot(MeshRef mesh, MaterialRef material) {
    MaterialSlot slot;
    slot.mesh_ = mesh;
    slot.material_ = material;
    slot.mesh_index_ = static_cast<uint32_t>(mesh_deps_.size());
    slot.material_index_ = static_cast<uint32_t>(material_deps_.size());
    slot.slot_index_ = static_cast<uint32_t>(material_slots_.size());
    
    material_slots_.push_back(slot);
    mesh_deps_.push_back(mesh);
    material_deps_.push_back(material);
    
    if (mesh) {
        total_vertex_ += mesh->get_vertex_count();
        total_index_ += mesh->get_index_count();
    }
    mark_dirty();
}

void Model::set_material(uint32_t slot_index, MaterialRef material) {
    if (slot_index < material_slots_.size()) {
        material_slots_[slot_index].material_ = material;
        if (slot_index < material_deps_.size()) {
            material_deps_[slot_index] = material;
        }
        mark_dirty();
    }
}

MaterialRef Model::get_material(uint32_t slot_index) const {
    if (slot_index < material_slots_.size()) {
        return material_slots_[slot_index].material_;
    }
    return nullptr;
}

MeshRef Model::get_mesh(uint32_t slot_index) const {
    if (slot_index < material_slots_.size()) {
        return material_slots_[slot_index].mesh_;
    }
    return nullptr;
}

VertexBufferRef Model::get_vertex_buffer(uint32_t index) const {
    auto mesh = get_mesh(index);
    return mesh ? mesh->get_vertex_buffer() : nullptr;
}

IndexBufferRef Model::get_index_buffer(uint32_t index) const {
    auto mesh = get_mesh(index);
    return mesh ? mesh->get_index_buffer() : nullptr;
}

BoundingBox Model::get_bounding_box() const {
    BoundingBox result;
    bool first = true;
    
    for (const auto& slot : material_slots_) {
        if (slot.mesh_) {
            const auto& box = slot.mesh_->get_bounding_box();
            if (first) {
                result = box;
                first = false;
            } else {
                result.min = result.min.cwiseMin(box.min);
                result.max = result.max.cwiseMax(box.max);
            }
        }
    }
    
    return result;
}
