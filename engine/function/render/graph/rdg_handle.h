#pragma once

#include "engine/core/dependency_graph/dependency_graph.h"
#include <cstdint>

using NodeID = DependencyGraph::NodeID;

/**
 * @brief Base class for all Render Dependency Graph (RDG) resource handles.
 * 
 * Wraps a NodeID to provide weak typing and safety, preventing accidental mixing
 * of different resource types (e.g., passing a buffer handle where a texture handle is expected).
 * These handles are lightweight and should be passed by value.
 */
class RDGResourceHandle {
public:
    RDGResourceHandle(NodeID id) : id_(id) {}

    bool operator<(const RDGResourceHandle& other) const noexcept { return id_ < other.id_; }

    bool operator==(const RDGResourceHandle& other) const noexcept { return (id_ == other.id_); }

    bool operator!=(const RDGResourceHandle& other) const noexcept { return !operator==(other); }

    inline NodeID id() { return id_; }

protected:
    NodeID id_ = UINT32_MAX;
};

/**
 * @brief Handle to a Pass Node in the RDG.
 */
class RDGPassHandle : public RDGResourceHandle {
public:
    RDGPassHandle(NodeID id) : RDGResourceHandle(id){};
};

/**
 * @brief Specific handle for a Render Pass Node.
 */
class RDGRenderPassHandle : public RDGPassHandle {
public:
    RDGRenderPassHandle(NodeID id) : RDGPassHandle(id){};
};

/**
 * @brief Specific handle for a Compute Pass Node.
 */
class RDGComputePassHandle : public RDGPassHandle {
public:
    RDGComputePassHandle(NodeID id) : RDGPassHandle(id){};
};

/**
 * @brief Specific handle for a Ray Tracing Pass Node.
 */
class RDGRayTracingPassHandle : public RDGPassHandle {
public:
    RDGRayTracingPassHandle(NodeID id) : RDGPassHandle(id){};
};

/**
 * @brief Specific handle for a Present Pass Node (swapchain presentation).
 */
class RDGPresentPassHandle : public RDGPassHandle {
public:
    RDGPresentPassHandle(NodeID id) : RDGPassHandle(id){};
};

/**
 * @brief Specific handle for a Copy Pass Node (transfer operations).
 */
class RDGCopyPassHandle : public RDGPassHandle {
public:
    RDGCopyPassHandle(NodeID id) : RDGPassHandle(id){};
};

/**
 * @brief Handle to a Texture Resource Node in the RDG.
 */
class RDGTextureHandle : public RDGResourceHandle {
public:
    RDGTextureHandle(NodeID id) : RDGResourceHandle(id){};
};

/**
 * @brief Handle to a Buffer Resource Node in the RDG.
 */
class RDGBufferHandle : public RDGResourceHandle {
public:
    RDGBufferHandle(NodeID id) : RDGResourceHandle(id){};
};
