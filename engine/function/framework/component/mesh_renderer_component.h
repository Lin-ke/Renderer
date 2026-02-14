#pragma once

#include "engine/function/framework/component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_pass/mesh_pass.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/class_db.h"
#include <vector>
#include <memory>

// Forward declarations
struct RHIAccelerationStructureInstanceInfo;

/**
 * @brief Component for rendering meshes with materials
 * 
 * MeshRendererComponent connects a Model (mesh data) with Materials
 * to provide visible geometry in the scene. It supports multi-submesh
 * rendering where each submesh can have its own material.
 */
class MeshRendererComponent : public Component {
    CLASS_DEF(MeshRendererComponent, Component)
public:
    MeshRendererComponent() = default;
    virtual ~MeshRendererComponent();

    virtual void load_asset_deps() override;
    virtual void save_asset_deps() override;
    
    /**
     * @brief Initialize the component (call after setting model)
     */
    void on_init();
    
    /**
     * @brief Update component (called each frame)
     * @param delta_time Time since last frame
     */
    void on_update(float delta_time);

    /**
     * @brief Set the model for this renderer
     * @param model The model containing mesh data
     */
    void set_model(ModelRef model);
    
    /**
     * @brief Get the current model
     */
    ModelRef get_model() const { return model_; }

    /**
     * @brief Set material for a specific submesh (or all if index is default)
     * @param material The material to apply
     * @param index Submesh index, or -1 for all submeshes
     */
    void set_material(MaterialRef material, int32_t index = -1);
    
    /**
     * @brief Get material for a submesh
     * @param index Submesh index
     * @return The material, or nullptr if not set
     */
    MaterialRef get_material(uint32_t index = 0) const;

    /**
     * @brief Collect draw batches for rendering
     * @param batches Output vector to append draw batches to
     */
    void collect_draw_batch(std::vector<render::DrawBatch>& batches);

    /**
     * @brief Collect ray tracing acceleration structure instances
     * @param instances Output vector for RTAS instances
     */
    void collect_acceleration_structure_instance(
        std::vector<RHIAccelerationStructureInstanceInfo>& instances);

    /**
     * @brief Get the number of submeshes
     */
    uint32_t get_submesh_count() const;

    bool get_cast_shadow() const { return cast_shadow_; }
    void set_cast_shadow(bool cast) { cast_shadow_ = cast; }

    static void register_class();

    // Asset dependency declarations for serialization
    // Asset dependencies - serialized manually in cpp

private:
    void update_object_info();
    void allocate_object_ids();
    void release_object_ids();

    ModelRef model_;
    std::vector<MaterialRef> materials_;
    
    // Per-submesh object info for GPU
    std::vector<ObjectInfo> object_infos_;
    std::vector<uint32_t> object_ids_;
    std::vector<uint32_t> mesh_card_ids_;

    Mat4 prev_model_ = Mat4::Identity();

    bool cast_shadow_ = true;
    bool initialized_ = false;
};

CEREAL_REGISTER_TYPE(MeshRendererComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, MeshRendererComponent);
