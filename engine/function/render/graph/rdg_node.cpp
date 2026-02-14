#include "engine/function/render/graph/rdg_node.h"

void RDGTextureNode::for_each_pass(const std::function<void(RDGTextureEdgeRef, RDGPassNodeRef)>& func) {
    for (auto& edge : InEdges<RDGTextureEdge>()) func(edge, edge->From<RDGPassNode>());
    for (auto& edge : OutEdges<RDGTextureEdge>()) func(edge, edge->To<RDGPassNode>());
}

void RDGBufferNode::for_each_pass(const std::function<void(RDGBufferEdgeRef, RDGPassNode*)>& func) {
    for (auto& edge : InEdges<RDGBufferEdge>()) func(edge, edge->From<RDGPassNode>());
    for (auto& edge : OutEdges<RDGBufferEdge>()) func(edge, edge->To<RDGPassNode>());
}

void RDGPassNode::for_each_texture(const std::function<void(RDGTextureEdgeRef, RDGTextureNodeRef)>& func) {
    std::vector<RDGTextureEdgeRef> in_textures = InEdges<RDGTextureEdge>();
    std::vector<RDGTextureEdgeRef> out_textures = OutEdges<RDGTextureEdge>();

    for (auto& texture_edge : in_textures) func(texture_edge, texture_edge->From<RDGTextureNode>());
    for (auto& texture_edge : out_textures) func(texture_edge, texture_edge->To<RDGTextureNode>());
}

void RDGPassNode::for_each_buffer(const std::function<void(RDGBufferEdgeRef, RDGBufferNodeRef)>& func) {
    std::vector<RDGBufferEdgeRef> in_buffers = InEdges<RDGBufferEdge>();
    std::vector<RDGBufferEdgeRef> out_buffers = OutEdges<RDGBufferEdge>();

    for (auto& buffer_edge : in_buffers) func(buffer_edge, buffer_edge->From<RDGBufferNode>());
    for (auto& buffer_edge : out_buffers) func(buffer_edge, buffer_edge->To<RDGBufferNode>());
}