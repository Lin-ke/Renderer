#pragma once

#include "engine/function/render/graph/rdg_edge.h"
#include "engine/function/render/graph/rdg_handle.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/core/dependency_graph/dependency_graph.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class RDGBuilder;

enum RDGPassNodeType {
    RDG_PASS_NODE_TYPE_RENDER = 0,
    RDG_PASS_NODE_TYPE_COMPUTE,
    RDG_PASS_NODE_TYPE_RAY_TRACING,
    RDG_PASS_NODE_TYPE_PRESENT,
    RDG_PASS_NODE_TYPE_COPY,

    RDG_PASS_NODE_TYPE_MAX_ENUM,
};

enum RDGResourceNodeType {
    RDG_RESOURCE_NODE_TYPE_TEXTURE = 0,
    RDG_RESOURCE_NODE_TYPE_BUFFER,

    RDG_RESOURCE_NODE_TYPE_MAX_ENUM,
};

/**
 * @brief Context passed to the execution lambda of a pass.
 * Contains the command list for recording, the builder (for resolving resources),
 * and the descriptor sets bound to the pass.
 */
struct RDGPassContext {
    RHICommandListRef command;
    RDGBuilder* builder;
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptors;

    uint32_t pass_index[3] = {0, 0, 0};
};

using RDGPassExecuteFunc = std::function<void(RDGPassContext)>;

/**
 * @brief Base class for all nodes in the RDG (Passes and Resources).
 */
class RDGNode : public DependencyGraph::Node {
public:
    RDGNode(std::string name) : name_(name) {}

    const std::string& name() { return name_; }

private:
    std::string name_;
};
using RDGNodeRef = RDGNode*;

// Resource Nodes

/**
 * @brief Base class for Resource Nodes (Texture/Buffer).
 */
class RDGResourceNode : public RDGNode {
public:
    RDGResourceNode(std::string name, RDGResourceNodeType node_type) : RDGNode(name), node_type_(node_type) {}

    inline bool is_imported() { return is_imported_; }

    RDGResourceNodeType node_type() { return node_type_; }

protected:
    RDGResourceNodeType node_type_;
    bool is_imported_ = false; ///< True if the resource is external/imported, not created by RDG.
};
using RDGResourceNodeRef = RDGResourceNode*;

class RDGPassNode; 

/**
 * @brief Node representing a Texture resource.
 */
class RDGTextureNode : public RDGResourceNode {
public:
    RDGTextureNode(std::string name) : RDGResourceNode(name, RDG_RESOURCE_NODE_TYPE_TEXTURE) {}

    void for_each_pass(const std::function<void(RDGTextureEdgeRef, RDGPassNode*)>& func);

    RDGTextureHandle get_handle() { return RDGTextureHandle(ID()); }

    const RHITextureInfo& get_info() { return info_; }

private:
    RHITextureInfo info_;
    RHIResourceState init_state_; 

    RHITextureRef texture_; // The actual RHI resource, resolved during execution.

    friend class RDGTextureBuilder;
    friend class RDGBuilder;
};
using RDGTextureNodeRef = RDGTextureNode*;

/**
 * @brief Node representing a Buffer resource.
 */
class RDGBufferNode : public RDGResourceNode {
public:
    RDGBufferNode(std::string name) : RDGResourceNode(name, RDG_RESOURCE_NODE_TYPE_BUFFER) {}

    void for_each_pass(const std::function<void(RDGBufferEdgeRef, RDGPassNode*)>& func);

    RDGBufferHandle get_handle() { return RDGBufferHandle(ID()); }

    const RHIBufferInfo& get_info() { return info_; }

private:
    RHIBufferInfo info_;
    RHIResourceState init_state_;

    RHIBufferRef buffer_; // The actual RHI resource, resolved during execution.

    friend class RDGBufferBuilder;
    friend class RDGBuilder;
};
using RDGBufferNodeRef = RDGBufferNode*;

// Pass Nodes

/**
 * @brief Base class for Pass Nodes.
 * A Pass represents a unit of work (Draw calls, Dispatch, Copy) executed on the GPU.
 */
class RDGPassNode : public RDGNode {
public:
    RDGPassNode(std::string name, RDGPassNodeType node_type) : RDGNode(name), node_type_(node_type) {}

    inline bool before(RDGPassNode* other) { return ID() < other->ID(); }
    inline bool after(RDGPassNode* other) { return ID() > other->ID(); }

    void for_each_texture(const std::function<void(RDGTextureEdgeRef, RDGTextureNodeRef)>& func);
    void for_each_buffer(const std::function<void(RDGBufferEdgeRef, RDGBufferNodeRef)>& func);

    RDGPassNodeType node_type() { return node_type_; }

protected:
    RDGPassNodeType node_type_;
    bool is_culled_ = false;

    RHIRootSignatureRef root_signature_;
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptor_sets_;

    // Transient resources managed by the pass for its duration
    std::vector<RHITextureViewRef> pooled_views_;
    std::vector<std::pair<RHIDescriptorSetRef, uint32_t>> pooled_descriptor_sets_;

    friend class RDGBuilder;
};
using RDGPassNodeRef = RDGPassNode*;

/**
 * @brief Node representing a Graphics Render Pass.
 */
class RDGRenderPassNode : public RDGPassNode {
public:
    RDGRenderPassNode(std::string name) : RDGPassNode(name, RDG_PASS_NODE_TYPE_RENDER) {}

    RDGRenderPassHandle get_handle() { return RDGRenderPassHandle(ID()); }

private:
    uint32_t pass_index_[3] = {0, 0, 0};
    RDGPassExecuteFunc execute_;

    friend class RDGRenderPassBuilder;
    friend class RDGBuilder;
};
using RDGRenderPassNodeRef = RDGRenderPassNode*;

/**
 * @brief Node representing a Compute Pass (Dispatch).
 */
class RDGComputePassNode : public RDGPassNode {
public:
    RDGComputePassNode(std::string name) : RDGPassNode(name, RDG_PASS_NODE_TYPE_COMPUTE) {}

    RDGComputePassHandle get_handle() { return RDGComputePassHandle(ID()); }

private:
    uint32_t pass_index_[3] = {0, 0, 0};
    RDGPassExecuteFunc execute_;

    friend class RDGComputePassBuilder;
    friend class RDGBuilder;
};
using RDGComputePassNodeRef = RDGComputePassNode*;

/**
 * @brief Node representing a Ray Tracing Pass.
 */
class RDGRayTracingPassNode : public RDGPassNode {
public:
    RDGRayTracingPassNode(std::string name) : RDGPassNode(name, RDG_PASS_NODE_TYPE_RAY_TRACING) {}

    RDGRayTracingPassHandle get_handle() { return RDGRayTracingPassHandle(ID()); }

private:
    uint32_t pass_index_[3] = {0, 0, 0};
    RDGPassExecuteFunc execute_;

    friend class RDGRayTracingPassBuilder;
    friend class RDGBuilder;
};
using RDGRayTracingPassNodeRef = RDGRayTracingPassNode*;

/**
 * @brief Node representing a Present Pass (Swapchain).
 */
class RDGPresentPassNode : public RDGPassNode {
public:
    RDGPresentPassNode(std::string name) : RDGPassNode(name, RDG_PASS_NODE_TYPE_PRESENT) {}

    RDGPresentPassHandle get_handle() { return RDGPresentPassHandle(ID()); }

private:
    friend class RDGPresentPassBuilder;
    friend class RDGBuilder;
};
using RDGPresentPassNodeRef = RDGPresentPassNode*;

/**
 * @brief Node representing a Copy/Transfer Pass.
 */
class RDGCopyPassNode : public RDGPassNode {
public:
    RDGCopyPassNode(std::string name) : RDGPassNode(name, RDG_PASS_NODE_TYPE_COPY) {}

    RDGCopyPassHandle get_handle() { return RDGCopyPassHandle(ID()); }

private:
    bool generate_mip_ = false;

    friend class RDGCopyPassBuilder;
    friend class RDGBuilder;
};
using RDGCopyPassNodeRef = RDGCopyPassNode*;