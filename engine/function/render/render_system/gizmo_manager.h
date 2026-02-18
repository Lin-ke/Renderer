#pragma once

#include "engine/core/math/math.h"
#include <imgui.h>
#include <ImGuizmo.h>

// Forward declarations
class Entity;
class CameraComponent;

/**
 * @brief Gizmo manager for object manipulation in viewport
 * 
 * Provides translation, rotation, and scaling gizmos using ImGuizmo
 */
class GizmoManager {
public:
    enum class Operation {
        Translate = ImGuizmo::TRANSLATE,
        Rotate = ImGuizmo::ROTATE,
        Scale = ImGuizmo::SCALE
    };

    enum class Mode {
        Local = ImGuizmo::LOCAL,
        World = ImGuizmo::WORLD
    };

    GizmoManager();
    ~GizmoManager();

    /**
     * @brief Initialize gizmo manager
     */
    void init();

    /**
     * @brief Shutdown gizmo manager
     */
    void shutdown();

    /**
     * @brief Draw gizmo for selected entity
     * @param camera Current active camera
     * @param selected_entity Entity to manipulate (nullptr to disable)
     * @param window_pos Viewport position in screen space
     * @param window_size Viewport size
     */
    void draw_gizmo(CameraComponent* camera, Entity* selected_entity, 
                    ImVec2 window_pos, ImVec2 window_size);

    /**
     * @brief Draw gizmo controls UI
     */
    void draw_controls();

    /**
     * @brief Check if gizmo is currently being used (dragging)
     */
    bool is_using() const;

    /**
     * @brief Check if gizmo is hovered
     */
    bool is_over() const;

    /**
     * @brief Set gizmo operation (Translate/Rotate/Scale)
     */
    void set_operation(Operation op);

    /**
     * @brief Get current operation
     */
    Operation get_operation() const { return current_operation_; }

    /**
     * @brief Set gizmo mode (Local/World)
     */
    void set_mode(Mode mode);

    /**
     * @brief Get current mode
     */
    Mode get_mode() const { return current_mode_; }

    /**
     * @brief Enable/disable gizmo
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

    bool is_enabled() const { return enabled_; }

private:
    Operation current_operation_ = Operation::Translate;
    Mode current_mode_ = Mode::Local;
    bool enabled_ = true;
    bool initialized_ = false;
};
