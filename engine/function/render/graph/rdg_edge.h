#pragma once

#include "engine/core/dependency_graph/dependency_graph.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include <cstdint>

enum RDGEdgeType {
    RDG_EDGE_TYPE_TEXTURE = 0,
    RDG_EDGE_TYPE_BUFFER,

    RDG_EDGE_TYPE_MAX_ENUM,
};

/**
 * @brief Base class for edges in the RDG.
 * 
 * An edge connects a Resource Node and a Pass Node.
 * - Resource -> Pass: The pass reads/consumes the resource (Input).
 * - Pass -> Resource: The pass writes/produces the resource (Output).
 * 
 * The edge carries information about the *state* the resource needs to be in
 * for this specific usage (e.g., Shader Resource, Render Target, Unordered Access).
 */
class RDGEdge : public DependencyGraph::Edge {
public:
    RDGEdge(RDGEdgeType edge_type, RHIResourceState state = RESOURCE_STATE_UNDEFINED) 
        : state(state), edge_type_(edge_type) {}

    virtual bool is_output() { return false; }

    RDGEdgeType edge_type() { return edge_type_; }

    RHIResourceState state;

protected:
    RDGEdgeType edge_type_;
};
using RDGEdgeRef = RDGEdge*;

/**
 * @brief Edge representing a Texture usage.
 * 
 * Contains texture-specific details like subresource ranges, binding slots,
 * and view types.
 */
class RDGTextureEdge : public RDGEdge {
public:
    RDGTextureEdge() : RDGEdge(RDG_EDGE_TYPE_TEXTURE) {}

    TextureSubresourceRange subresource = {};
    TextureSubresourceLayers subresource_layer = {};
    
    // Usage flags
    bool as_color = false;              ///< Used as a Color Attachment
    bool as_depth_stencil = false;      ///< Used as a Depth/Stencil Attachment
    bool as_shader_read = false;        ///< Used as a Shader Resource (SRV)
    bool as_shader_read_write = false;  ///< Used as a Storage Image (UAV)
    bool as_output_read = false;        ///< Output resource that will be read (used for barrier generation)
    bool as_output_read_write = false;  ///< Output resource that will be read/written (UAV)
    bool as_present = false;            ///< Used for Presentation
    bool as_transfer_src = false;       ///< Source of a transfer (copy) operation
    bool as_transfer_dst = false;       ///< Destination of a transfer (copy) operation

    virtual bool is_output() override { return as_output_read || as_output_read_write; }

    // Binding info
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t index = 0;
    ResourceType type = RESOURCE_TYPE_TEXTURE;
    TextureViewType view_type = VIEW_TYPE_2D;

    // Render Target ops
    AttachmentLoadOp load_op = ATTACHMENT_LOAD_OP_DONT_CARE;
    AttachmentStoreOp store_op = ATTACHMENT_STORE_OP_DONT_CARE;

    Color4 clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    float clear_depth = 1.0f;
    uint32_t clear_stencil = 0;
};
using RDGTextureEdgeRef = RDGTextureEdge*;

/**
 * @brief Edge representing a Buffer usage.
 * 
 * Contains buffer-specific details like offsets, sizes, and binding slots.
 */
class RDGBufferEdge : public RDGEdge {
public:
    RDGBufferEdge() : RDGEdge(RDG_EDGE_TYPE_BUFFER) {}

    uint32_t offset = 0;
    uint32_t size = 0;
    
    // Usage flags
    bool as_shader_read = false;        ///< Used as a Uniform Buffer or SRV
    bool as_shader_read_write = false;  ///< Used as a Storage Buffer (UAV)
    bool as_output_read = false;        ///< Output resource that will be read
    bool as_output_read_write = false;  ///< Output resource that will be read/written (UAV)
    bool as_output_indirect_draw = false; ///< Output buffer used for Indirect Draw arguments

    virtual bool is_output() override { return as_output_read || as_output_read_write || as_output_indirect_draw; }

    // Binding info
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t index = 0;
    ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER;
};
using RDGBufferEdgeRef = RDGBufferEdge*;
