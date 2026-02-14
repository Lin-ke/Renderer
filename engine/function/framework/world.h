#pragma once

#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include <memory>
#include <vector>

/**
 * @brief World manages the active scene and provides global game state
 * 
 * World is a singleton that manages:
 * - Active scene reference
 * - Global component queries (all cameras, all mesh renderers, etc.)
 * - Time and tick management
 */
class World {
public:
    World();
    ~World();

    /**
     * @brief Initialize the world system
     */
    void init();

    /**
     * @brief Shutdown and cleanup
     */
    void destroy();

    /**
     * @brief Set the currently active scene
     * @param scene The scene to activate
     */
    void set_active_scene(std::shared_ptr<Scene> scene);

    /**
     * @brief Get the currently active scene
     * @return Pointer to active scene, or nullptr if none
     */
    Scene* get_active_scene() const { return active_scene_.get(); }

    /**
     * @brief Update all systems (called each frame)
     * @param delta_time Time since last frame in seconds
     */
    void tick(float delta_time);

    /**
     * @brief Get all mesh renderer components in the active scene
     * @return Vector of mesh renderer components
     */
    std::vector<MeshRendererComponent*> get_mesh_renderers() const;

    /**
     * @brief Get the active camera component
     * @return Pointer to active camera, or nullptr if none
     */
    CameraComponent* get_active_camera() const;

    /**
     * @brief Singleton accessor
     */
    static World& get();

private:
    std::shared_ptr<Scene> active_scene_;
    static std::unique_ptr<World> instance_;

    bool initialized_ = false;
};
