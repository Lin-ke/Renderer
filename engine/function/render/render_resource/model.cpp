#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/model_importer.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

DEFINE_LOG_TAG(LogModel, "Model");

CEREAL_REGISTER_TYPE(Model)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Model)

Model::Model(std::string path, ModelProcessSetting process_setting)
    : path_(std::move(path)), process_setting_(std::move(process_setting)) {
}

Model::~Model() = default;

void Model::load_asset_deps() {
    auto* am = EngineContext::asset();
    if (!am) return;

    for (auto& slot : material_slots_) {
        if (!slot.mesh_uid.is_empty()) {
            slot.mesh = am->load_asset<Mesh>(slot.mesh_uid);
        }
        if (!slot.material_uid.is_empty()) {
            slot.material = am->load_asset<Material>(slot.material_uid);
        }
    }
}

void Model::save_asset_deps() {
    for (auto& slot : material_slots_) {
        if (slot.mesh) {
            slot.mesh_uid = slot.mesh->get_uid();
        }
        if (slot.material) {
            slot.material_uid = slot.material->get_uid();
        }
    }
}

void Model::traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const {
    for (const auto& slot : material_slots_) {
        if (slot.mesh) callback(slot.mesh);
        if (slot.material) callback(slot.material);
    }
}

void Model::on_load_asset() {
    load_asset_deps();
    
    // Recalculate statistics
    total_vertex_ = 0;
    total_index_ = 0;
    for (const auto& slot : material_slots_) {
        if (slot.mesh) {
            total_vertex_ += slot.mesh->get_vertex_count();
            total_index_ += slot.mesh->get_index_count();
        }
    }
    clear_dirty();
}

void Model::on_save_asset() {
    save_asset_deps();
    clear_dirty();
}

std::shared_ptr<Model> Model::Load(const std::string& virtual_path, 
                                    const ModelProcessSetting& process_setting,
                                    const UID& explicit_uid) {
    if (!EngineContext::asset()) {
        ERR(LogModel, "AssetManager not initialized");
        return nullptr;
    }

    // Resolve virtual path to physical path or relative path
    std::string abs_path;
    
    auto physical_opt = EngineContext::asset()->get_physical_path(virtual_path);
    if (physical_opt) {
        abs_path = physical_opt.value().string();
    } else {
        // If not found in AssetManager, treat as relative path from executable
        abs_path = std::filesystem::absolute(virtual_path).string();
    }

    // Determine extension
    std::filesystem::path fs_path(abs_path);
    std::string ext = fs_path.extension().string();
    
    // Case 1: Engine Asset (.asset, .binasset)
    if (ext == ".asset" || ext == ".binasset") {
        // Try to load as native asset
        // If explicit_uid is provided, use it, otherwise try to resolve from path
        UID uid = explicit_uid;
        if (uid.is_empty()) {
            uid = EngineContext::asset()->get_uid_by_path(virtual_path); // Try virtual path first
            if (uid.is_empty()) {
                 uid = EngineContext::asset()->get_uid_by_path(abs_path); // Then physical
            }
        }
        
        if (!uid.is_empty()) {
            return EngineContext::asset()->load_asset<Model>(uid);
        }
        
        ERR(LogModel, "Failed to load native model asset: {} (UID not found)", virtual_path);
        return nullptr;
    }

    // Case 2: Import from Source (FBX, OBJ, GLTF, etc.)
    
    // Check cache by path/UID first
    UID uid = explicit_uid.is_empty() ? EngineContext::asset()->get_uid_by_path(abs_path) : explicit_uid;
    if (uid.is_empty()) {
        // Try getting UID from virtual path as fallback
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

    // Import using ModelImporter
    INFO(LogModel, "Importing model from source: {}", abs_path);
    ModelImporter importer;
    auto model = importer.import_model(abs_path, virtual_path, process_setting);
    
    if (model) {
        if (!explicit_uid.is_empty()) {
            model->set_uid(explicit_uid);
        } else if (model->get_uid().is_empty()) {
            // Importer might have set a deterministic UID based on path, if not, generate one
            model->set_uid(UID::from_hash(virtual_path.empty() ? abs_path : virtual_path));
        }
        
        // Register with AssetManager (cache it)
        EngineContext::asset()->register_asset(model, virtual_path.empty() ? abs_path : virtual_path);
        
        // Ensure dependencies are loaded (they should be already by importer)
        model->on_load_asset();
        
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
                                    const UID& explicit_uid) {
    ModelProcessSetting setting;
    setting.smooth_normal = smooth_normal;
    setting.load_materials = load_materials;
    setting.flip_uv = flip_uv;
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
    slot.mesh = mesh;
    slot.material = material;
    if (mesh) slot.mesh_uid = mesh->get_uid();
    if (material) slot.material_uid = material->get_uid();
    
    slot.mesh_index = static_cast<uint32_t>(material_slots_.size());
    material_slots_.push_back(slot);
    
    if (mesh) {
        total_vertex_ += mesh->get_vertex_count();
        total_index_ += mesh->get_index_count();
    }
    mark_dirty();
}

void Model::set_material(uint32_t slot_index, MaterialRef material) {
    if (slot_index < material_slots_.size()) {
        material_slots_[slot_index].material = material;
        if (material) {
            material_slots_[slot_index].material_uid = material->get_uid();
        } else {
            material_slots_[slot_index].material_uid = UID::empty();
        }
        mark_dirty();
    }
}

MaterialRef Model::get_material(uint32_t slot_index) const {
    if (slot_index < material_slots_.size()) {
        return material_slots_[slot_index].material;
    }
    return nullptr;
}

MeshRef Model::get_mesh(uint32_t slot_index) const {
    if (slot_index < material_slots_.size()) {
        return material_slots_[slot_index].mesh;
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
        if (slot.mesh) {
            const auto& box = slot.mesh->get_bounding_box();
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
