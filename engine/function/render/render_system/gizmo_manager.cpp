#include "gizmo_manager.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/entity.h"
#include "engine/core/log/Log.h"

#include <imgui_internal.h>

DEFINE_LOG_TAG(LogGizmo, "GizmoManager");

GizmoManager::GizmoManager() = default;

GizmoManager::~GizmoManager() {
    if (initialized_) {
        shutdown();
    }
}

void GizmoManager::init() {
    if (initialized_) return;
    
    ImGuizmo::Enable(true);
    
    initialized_ = true;
    INFO(LogGizmo, "GizmoManager initialized");
}

void GizmoManager::shutdown() {
    if (!initialized_) return;
    
    ImGuizmo::Enable(false);
    initialized_ = false;
    
    INFO(LogGizmo, "GizmoManager shutdown");
}

void GizmoManager::draw_gizmo(CameraComponent* camera, Entity* selected_entity,
                              ImVec2 viewport_pos, ImVec2 viewport_size) {
    if (!initialized_ || !enabled_ || !camera || !selected_entity) {
        return;
    }

    auto* transform = selected_entity->get_component<TransformComponent>();
    if (!transform) return;

    // Check if ImGui is in a valid frame
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx || ctx->FrameCountEnded == ctx->FrameCount) {
        return; // Not in a valid frame
    }
    
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    
    Mat4 view = camera->get_view_matrix();
    Mat4 proj = camera->get_projection_matrix();
    
    float view_matrix[16];
    float proj_matrix[16];
    float transform_matrix[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            view_matrix[i + j * 4] = view(i, j);
            proj_matrix[i + j * 4] = proj(i, j);
        }
    }

    ImGuizmo::SetOrthographic(false);

    ImGuizmo::SetRect(viewport_pos.x, viewport_pos.y, viewport_size.x, viewport_size.y);
    
    // Get current transform matrix
    Mat4 model = transform->transform.get_matrix();
    
    Vec3 local_offset = Vec3::Zero();
    if (current_anchor_ != Anchor::Pivot) {
        if (auto* mesh_renderer = selected_entity->get_component<MeshRendererComponent>()) {
            if (auto model_asset = mesh_renderer->get_model()) {
                BoundingBox box = model_asset->get_bounding_box();
                if (current_anchor_ == Anchor::Center) {
                    local_offset = (box.min + box.max) * 0.5f;
                } else if (current_anchor_ == Anchor::Bottom) {
                    local_offset = (box.min + box.max) * 0.5f;
                    local_offset.y = box.min.y;
                }
            }
        }
    }

    Mat4 gizmo_matrix = model;
    if (local_offset.norm() > 0.0001f) {
        Mat4 offset_mat = Mat4::Identity();
        offset_mat.m[0][3] = local_offset.x;
        offset_mat.m[1][3] = local_offset.y;
        offset_mat.m[2][3] = local_offset.z;
        gizmo_matrix = model * offset_mat;
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            transform_matrix[i + j * 4] = gizmo_matrix(i, j);
        }
    }

    // Enable gizmo
    ImGuizmo::Enable(true);
    
    // Draw gizmo - use window draw list since we're inside a window
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

    // Save original transform matrix to detect changes
    float original_transform[16];
    for (int i = 0; i < 16; ++i) original_transform[i] = transform_matrix[i];
    
    bool manipulated = ImGuizmo::Manipulate(view_matrix, proj_matrix, 
                         static_cast<ImGuizmo::OPERATION>(current_operation_),
                         static_cast<ImGuizmo::MODE>(current_mode_),
                         transform_matrix, nullptr, nullptr);
    
    // Apply transform if changed
    bool is_using = ImGuizmo::IsUsing();
    
    // Check if matrix actually changed
    bool matrix_changed = false;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(transform_matrix[i] - original_transform[i]) > 0.0001f) {
            matrix_changed = true;
            break;
        }
    }
    
    if (is_using && matrix_changed) {
        // Convert back from column-major
        Mat4 new_gizmo_matrix;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                new_gizmo_matrix(i, j) = transform_matrix[i + j * 4];
            }
        }

        // Revert offset to get the new pivot matrix
        Mat4 new_model = new_gizmo_matrix;
        if (local_offset.norm() > 0.0001f) {
            Mat4 inv_offset_mat = Mat4::Identity();
            inv_offset_mat.m[0][3] = -local_offset.x;
            inv_offset_mat.m[1][3] = -local_offset.y;
            inv_offset_mat.m[2][3] = -local_offset.z;
            new_model = new_gizmo_matrix * inv_offset_mat;
        }
        
        Vec3 position = Vec3(new_model.m[0][3], new_model.m[1][3], new_model.m[2][3]);
        
        Vec3 scale;
        scale.x = std::sqrt(new_model.m[0][0] * new_model.m[0][0] + new_model.m[1][0] * new_model.m[1][0] + new_model.m[2][0] * new_model.m[2][0]);
        scale.y = std::sqrt(new_model.m[0][1] * new_model.m[0][1] + new_model.m[1][1] * new_model.m[1][1] + new_model.m[2][1] * new_model.m[2][1]);
        scale.z = std::sqrt(new_model.m[0][2] * new_model.m[0][2] + new_model.m[1][2] * new_model.m[1][2] + new_model.m[2][2] * new_model.m[2][2]);

        Mat3 rotation_matrix;
        rotation_matrix.set_col(0, Vec3(new_model.m[0][0], new_model.m[1][0], new_model.m[2][0]) / scale.x);
        rotation_matrix.set_col(1, Vec3(new_model.m[0][1], new_model.m[1][1], new_model.m[2][1]) / scale.y);
        rotation_matrix.set_col(2, Vec3(new_model.m[0][2], new_model.m[1][2], new_model.m[2][2]) / scale.z);

        transform->transform.set_position(position);
        transform->transform.set_rotation(Math::extract_euler_angles(rotation_matrix));
        transform->transform.set_scale(scale);
    }
}

void GizmoManager::draw_controls() {
    if (!initialized_) return;

    ImGui::Separator();
    ImGui::Text("Gizmo");

    if (ImGui::RadioButton("Translate", current_operation_ == Operation::Translate)) {
        current_operation_ = Operation::Translate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", current_operation_ == Operation::Rotate)) {
        current_operation_ = Operation::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", current_operation_ == Operation::Scale)) {
        current_operation_ = Operation::Scale;
    }

    if (ImGui::RadioButton("Local", current_mode_ == Mode::Local)) {
        current_mode_ = Mode::Local;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", current_mode_ == Mode::World)) {
        current_mode_ = Mode::World;
    }

    ImGui::Text("Anchor:");
    if (ImGui::RadioButton("Pivot", current_anchor_ == Anchor::Pivot)) {
        current_anchor_ = Anchor::Pivot;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Center", current_anchor_ == Anchor::Center)) {
        current_anchor_ = Anchor::Center;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Bottom", current_anchor_ == Anchor::Bottom)) {
        current_anchor_ = Anchor::Bottom;
    }
}

bool GizmoManager::is_using() const {
    return initialized_ && ImGuizmo::IsUsing();
}

bool GizmoManager::is_over() const {
    return initialized_ && ImGuizmo::IsOver();
}

void GizmoManager::set_operation(Operation op) {
    current_operation_ = op;
}

void GizmoManager::set_mode(Mode mode) {
    current_mode_ = mode;
}
