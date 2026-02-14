#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/core/math/math.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/render_resource_manager.h"
#include "engine/core/log/Log.h"

DEFINE_LOG_TAG(LogMeshRenderer, "MeshRenderer");

MeshRendererComponent::~MeshRendererComponent() {
    // Unregister from mesh manager
    if (initialized_) {
        auto render_system = EngineContext::render_system();
        if (render_system && render_system->get_mesh_manager()) {
            render_system->get_mesh_manager()->unregister_mesh_renderer(this);
        }
    }
    release_object_ids();
}

void MeshRendererComponent::load_asset_deps() {
    
    // Ensure materials vector matches submesh count
    if (model_) {
        uint32_t submesh_count = model_->get_submesh_count();
        if (materials_.size() < submesh_count) {
            materials_.resize(submesh_count);
        }
    }
}

void MeshRendererComponent::save_asset_deps() {
    //####TODO####: Serialize model and materials
}

void MeshRendererComponent::on_init() {
    allocate_object_ids();
    
    // Initialize object infos
    if (model_) {
        uint32_t submesh_count = model_->get_submesh_count();
        object_infos_.resize(submesh_count);
        for (uint32_t i = 0; i < submesh_count; i++) {
            object_infos_[i] = {};
            object_infos_[i].material_id = 0;
            object_infos_[i].vertex_id = 0;
            object_infos_[i].index_id = 0;
        }
    }
    
    // Register with mesh manager
    auto render_system = EngineContext::render_system();
    if (render_system && render_system->get_mesh_manager()) {
        render_system->get_mesh_manager()->register_mesh_renderer(this);
    }
    
    initialized_ = true;
}

void MeshRendererComponent::on_update(float delta_time) {
    (void)delta_time;
    if (!initialized_ || !model_) return;
    
    update_object_info();
}

void MeshRendererComponent::allocate_object_ids() {
    if (!EngineContext::render_resource()) return;
    
    uint32_t submesh_count = model_ ? model_->get_submesh_count() : 0;
    if (submesh_count == 0) return;
    
    object_ids_.resize(submesh_count);
    for (uint32_t i = 0; i < submesh_count; i++) {
        object_ids_[i] = EngineContext::render_resource()->allocate_object_id();
    }
    
    INFO(LogMeshRenderer, "Allocated {} object IDs", submesh_count);
}

void MeshRendererComponent::release_object_ids() {
    if (!EngineContext::render_resource()) return;
    
    for (uint32_t id : object_ids_) {
        if (id != 0) {
            EngineContext::render_resource()->release_object_id(id);
        }
    }
    object_ids_.clear();
}

void MeshRendererComponent::update_object_info() {
    auto transform = get_owner()->get_component<TransformComponent>();
    if (!transform) return;
    
    Mat4 model_mat = transform->transform.get_matrix();
    
    for (uint32_t i = 0; i < object_infos_.size() && i < object_ids_.size(); i++) {
        object_infos_[i].model = model_mat;
        object_infos_[i].prev_model = prev_model_;
        object_infos_[i].inv_model = model_mat.inverse();
        
        // Get material ID if available
        if (i < materials_.size() && materials_[i]) {
            object_infos_[i].material_id = materials_[i]->get_material_id();
        }
        
        // Set vertex/index IDs from model
        if (model_ && i < model_->get_submesh_count()) {
            const auto& submesh = model_->submesh(i);
            if (submesh.vertex_buffer) {
                object_infos_[i].vertex_id = submesh.vertex_buffer->vertex_id_;
            }
            if (submesh.index_buffer) {
                object_infos_[i].index_id = submesh.index_buffer->index_id_;
            }
        }
        
        // Submit to render resource manager
        if (object_ids_[i] != 0 && EngineContext::render_resource()) {
            EngineContext::render_resource()->set_object_info(object_infos_[i], object_ids_[i]);
        }
    }
    
    prev_model_ = model_mat;
}

void MeshRendererComponent::set_model(ModelRef model) {
    release_object_ids();
    model_ = model;
    
    if (model_) {
        // Resize materials to match submesh count
        uint32_t submesh_count = model_->get_submesh_count();
        materials_.resize(submesh_count);
        
        if (initialized_) {
            allocate_object_ids();
            object_infos_.resize(submesh_count);
        }
    }
}

void MeshRendererComponent::set_material(MaterialRef material, int32_t index) {
    if (index < 0) {
        // Set for all submeshes
        for (auto& mat : materials_) {
            mat = material;
        }
    } else if (static_cast<uint32_t>(index) < materials_.size()) {
        materials_[index] = material;
    }
}

MaterialRef MeshRendererComponent::get_material(uint32_t index) const {
    if (index < materials_.size()) {
        return materials_[index];
    }
    return nullptr;
}

void MeshRendererComponent::collect_draw_batch(std::vector<render::DrawBatch>& batches) {
    if (!model_) return;
    
    auto transform = get_owner()->get_component<TransformComponent>();
    if (!transform) return;
    
    Mat4 model_mat = transform->transform.get_matrix();
    
    for (uint32_t i = 0; i < model_->get_submesh_count(); i++) {
        const auto& submesh = model_->submesh(i);
        
        render::DrawBatch batch;
        batch.object_id = (i < object_ids_.size()) ? object_ids_[i] : 0;
        
        // Get GPU buffers from model submesh
        if (submesh.vertex_buffer) {
            batch.vertex_buffer = submesh.vertex_buffer->position_buffer_;
            batch.normal_buffer = submesh.vertex_buffer->normal_buffer_;
        }
        if (submesh.index_buffer) {
            batch.index_buffer = submesh.index_buffer->buffer_;
            batch.index_count = submesh.index_buffer->index_num();
        }
        
        batch.model_matrix = model_mat;
        batch.inv_model_matrix = model_mat.inverse();
        
        batches.push_back(batch);
    }
}

void MeshRendererComponent::collect_acceleration_structure_instance(
    std::vector<RHIAccelerationStructureInstanceInfo>& instances) {
    //####TODO####: Ray tracing support
    // This requires BLAS/TLAS implementation
}

uint32_t MeshRendererComponent::get_submesh_count() const {
    return model_ ? model_->get_submesh_count() : 0;
}

void MeshRendererComponent::register_class() {
    Registry::add<MeshRendererComponent>("MeshRendererComponent");
}
