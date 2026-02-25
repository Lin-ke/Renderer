#include "gizmo_manager.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/entity.h"
#include "engine/core/log/Log.h"

#include <imgui_internal.h>

DEFINE_LOG_TAG(LogGizmo, "GizmoManager");

void GizmoManager::to_row_major_array(const Mat4& matrix, float out_matrix[16]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out_matrix[i * 4 + j] = matrix(i, j);
        }
    }
}

Mat4 GizmoManager::from_row_major_array(const float in_matrix[16]) {
    Mat4 matrix;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            matrix(i, j) = in_matrix[i * 4 + j];
        }
    }
    return matrix;
}

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
                              ImVec2 viewport_pos, ImVec2 viewport_size,
                              ImDrawList* draw_list) {
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
    
    Mat4 view = camera->get_view_matrix();
    Mat4 proj = camera->get_projection_matrix();
    
    float view_matrix[16];
    float proj_matrix[16];
    float transform_matrix[16];
    to_row_major_array(view, view_matrix);
    to_row_major_array(proj, proj_matrix);
    
    ImGuizmo::SetOrthographic(false);

    ImGuizmo::SetRect(viewport_pos.x, viewport_pos.y, viewport_size.x, viewport_size.y);
    
    // Debug: check mouse position relative to viewport
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_in_viewport = mouse_pos.x >= viewport_pos.x && mouse_pos.x <= viewport_pos.x + viewport_size.x &&
                             mouse_pos.y >= viewport_pos.y && mouse_pos.y <= viewport_pos.y + viewport_size.y;
    static int debug_counter = 0;
    if (++debug_counter % 60 == 0) {
        INFO(LogGizmo, "Viewport: pos=({:.1f},{:.1f}), size=({:.1f},{:.1f}), mouse=({:.1f},{:.1f}), in_viewport={}",
             viewport_pos.x, viewport_pos.y, viewport_size.x, viewport_size.y,
             mouse_pos.x, mouse_pos.y, mouse_in_viewport);
    }
    
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
        offset_mat.m[3][0] = local_offset.x;
        offset_mat.m[3][1] = local_offset.y;
        offset_mat.m[3][2] = local_offset.z;
        gizmo_matrix = offset_mat * model;
    }
    to_row_major_array(gizmo_matrix, transform_matrix);

    // Enable gizmo
    ImGuizmo::Enable(true);
    
    // Use provided draw list or get window draw list
    if (!draw_list) {
        draw_list = ImGui::GetWindowDrawList();
    }
    ImGuizmo::SetDrawlist(draw_list);

    // Save original transform matrix to detect changes
    float original_transform[16];
    for (int i = 0; i < 16; ++i) original_transform[i] = transform_matrix[i];
    
    ImGuizmo::Manipulate(view_matrix, proj_matrix,
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
        Mat4 new_gizmo_matrix = from_row_major_array(transform_matrix);

        // Revert offset to get the new pivot matrix
        Mat4 new_model = new_gizmo_matrix;
        if (local_offset.norm() > 0.0001f) {
            Mat4 inv_offset_mat = Mat4::Identity();
            inv_offset_mat.m[3][0] = -local_offset.x;
            inv_offset_mat.m[3][1] = -local_offset.y;
            inv_offset_mat.m[3][2] = -local_offset.z;
            new_model = inv_offset_mat * new_gizmo_matrix;
        }
        
        Vec3 position = Vec3(new_model.m[3][0], new_model.m[3][1], new_model.m[3][2]);
        
        Vec3 scale;
        scale.x = std::sqrt(new_model.m[0][0] * new_model.m[0][0] + new_model.m[0][1] * new_model.m[0][1] + new_model.m[0][2] * new_model.m[0][2]);
        scale.y = std::sqrt(new_model.m[1][0] * new_model.m[1][0] + new_model.m[1][1] * new_model.m[1][1] + new_model.m[1][2] * new_model.m[1][2]);
        scale.z = std::sqrt(new_model.m[2][0] * new_model.m[2][0] + new_model.m[2][1] * new_model.m[2][1] + new_model.m[2][2] * new_model.m[2][2]);

        Mat3 rotation_matrix;
        rotation_matrix.set_row(0, Vec3(new_model.m[0][0], new_model.m[0][1], new_model.m[0][2]) / scale.x);
        rotation_matrix.set_row(1, Vec3(new_model.m[1][0], new_model.m[1][1], new_model.m[1][2]) / scale.y);
        rotation_matrix.set_row(2, Vec3(new_model.m[2][0], new_model.m[2][1], new_model.m[2][2]) / scale.z);

        switch (current_operation_) {
        case Operation::Translate:
            transform->transform.set_position(position);
            break;
        case Operation::Rotate:
            transform->transform.set_position(position);
            transform->transform.set_rotation(Math::extract_euler_angles(rotation_matrix));
            break;
        case Operation::Scale:
            transform->transform.set_position(position);
            transform->transform.set_scale(scale);
            break;
        }
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
