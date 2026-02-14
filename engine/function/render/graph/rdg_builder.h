#pragma once

#include "engine/core/dependency_graph/dependency_graph.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/graph/rdg_handle.h"
#include "engine/function/render/graph/rdg_node.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

/**
 * @brief Blackboard for storing named RDG resources and passes.
 * Allows looking up nodes by their string name.
 */
class RDGBlackBoard {
public:
    RDGPassNodeRef pass(std::string name);
    RDGBufferNodeRef buffer(std::string name);
    RDGTextureNodeRef texture(std::string name);

    void add_pass(RDGPassNodeRef pass);
    void add_buffer(RDGBufferNodeRef buffer);
    void add_texture(RDGTextureNodeRef texture);

    void clear() {
        passes_.clear();
        buffers_.clear();
        textures_.clear();
    }

    void for_each_pass(std::function<void(RDGPassNodeRef)> func) {
        for (auto& [name, pass] : passes_) func(pass);
    }

    void for_each_buffer(std::function<void(RDGBufferNodeRef)> func) {
        for (auto& [name, buffer] : buffers_) func(buffer);
    }

    void for_each_texture(std::function<void(RDGTextureNodeRef)> func) {
        for (auto& [name, texture] : textures_) func(texture);
    }

private:
    std::unordered_map<std::string, RDGPassNodeRef> passes_;
    std::unordered_map<std::string, RDGBufferNodeRef> buffers_;
    std::unordered_map<std::string, RDGTextureNodeRef> textures_;
};

class RDGTextureBuilder;
class RDGBufferBuilder;
class RDGRenderPassBuilder;
class RDGComputePassBuilder;
class RDGRayTracingPassBuilder;
class RDGPresentPassBuilder;
class RDGCopyPassBuilder;

/**
 * @brief The main entry point for building and executing the Render Dependency Graph.
 * 
 * **Design Concepts (referenced from UE's RDG):**
 * - RDG has a single-frame lifecycle. Resources allocated (except imported ones) are transient.
 * - Resource handles are returned instead of raw pointers, ensuring safety.
 * - Resources are allocated from a pool to minimize overhead.
 * 
 * **Current Implementation Status:**
 * - Basic graph construction and execution.
 * - Automatic barrier generation (resource state tracking).
 * - Automatic transient resource allocation and aliasing (pooling).
 * 
 * **TODOs:**
 * - Pass Culling (stripping unused passes).
 * - Async Compute / Multi-queue support.
 * - Fine-grained subresource barriers.
 * - Multi-threaded command recording.
 */
class RDGBuilder {
public:
    RDGBuilder() = default;
    RDGBuilder(RHICommandListRef command) : command_(command) {}

    ~RDGBuilder(){};

    // --- Creation Methods ---
    // These methods create nodes in the graph and return a builder object for configuration.

    RDGTextureBuilder create_texture(std::string name);
    RDGBufferBuilder create_buffer(std::string name);
    
    // Note: Passes are currently executed in the order they are created. 
    // Future versions should support topological sort based on dependencies.
    RDGRenderPassBuilder create_render_pass(std::string name);
    RDGComputePassBuilder create_compute_pass(std::string name);
    RDGRayTracingPassBuilder create_ray_tracing_pass(std::string name);
    RDGPresentPassBuilder create_present_pass(std::string name);
    RDGCopyPassBuilder create_copy_pass(std::string name);

    // --- Retrieval Methods ---
    
    RDGTextureHandle get_texture(std::string name);
    RDGBufferHandle get_buffer(std::string name);
    RDGRenderPassHandle get_render_pass(std::string name) { return get_pass<RDGRenderPassNodeRef, RDGRenderPassHandle>(name); }
    RDGComputePassHandle get_compute_pass(std::string name) { return get_pass<RDGComputePassNodeRef, RDGComputePassHandle>(name); }
    RDGRayTracingPassHandle get_ray_tracing_pass(std::string name) { return get_pass<RDGRayTracingPassNodeRef, RDGRayTracingPassHandle>(name); }
    RDGPresentPassHandle get_present_pass(std::string name) { return get_pass<RDGPresentPassNodeRef, RDGPresentPassHandle>(name); }
    RDGCopyPassHandle get_copy_pass(std::string name) { return get_pass<RDGCopyPassNodeRef, RDGCopyPassHandle>(name); }

    std::shared_ptr<DependencyGraph::DependencyGraph> get_graph() { return graph_; }

    /**
     * @brief Compiles and executes the graph.
     * 1. Traverses the passes in order.
     * 2. Allocates resources (if not imported).
     * 3. Generates barriers.
     * 4. Executes the pass callback (recording commands).
     * 5. Releases transient resources.
     */
    void execute();

    /**
     * @brief Exports the graph structure to a GraphViz (.dot) file for visualization.
     */
    void export_graphviz(std::string path);

private:
    void create_input_barriers(RDGPassNodeRef pass);
    void create_output_barriers(RDGPassNodeRef pass);
    void prepare_descriptor_set(RDGPassNodeRef pass);
    void prepare_render_target(RDGRenderPassNodeRef pass, RHIRenderPassInfo& render_pass_info);
    void release_resource(RDGPassNodeRef pass);
    void execute_pass(RDGRenderPassNodeRef pass);
    void execute_pass(RDGComputePassNodeRef pass);
    void execute_pass(RDGRayTracingPassNodeRef pass);
    void execute_pass(RDGPresentPassNodeRef pass);
    void execute_pass(RDGCopyPassNodeRef pass);

    template <typename Type, typename Handle>
    Handle get_pass(std::string name) {
        auto node = black_board_.pass(name);
        if (node == nullptr) {
            // INFO(LogRDG, "Unable to find RDG resource, please check name!");
            return Handle(UINT32_MAX);
        }
        return dynamic_cast<Type>(node)->get_handle();
    }

    RHITextureRef resolve(RDGTextureNodeRef texture_node);
    RHIBufferRef resolve(RDGBufferNodeRef buffer_node);
    void release(RDGTextureNodeRef texture_node, RHIResourceState state);
    void release(RDGBufferNodeRef buffer_node, RHIResourceState state);

    RHIResourceState previous_state(RDGTextureNodeRef texture_node, RDGPassNodeRef pass_node, TextureSubresourceRange subresource = {},
                                    bool output = false);
    RHIResourceState previous_state(RDGBufferNodeRef buffer_node, RDGPassNodeRef pass_node, uint32_t offset = 0, uint32_t size = 0,
                                    bool output = false);
    bool is_last_used_pass(RDGTextureNodeRef texture_node, RDGPassNodeRef pass_node, bool output = false);
    bool is_last_used_pass(RDGBufferNodeRef buffer_node, RDGPassNodeRef pass_node, bool output = false);

    std::vector<RDGPassNodeRef> passes_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_ = std::make_shared<DependencyGraph::DependencyGraph>();
    RDGBlackBoard black_board_;

    RHICommandListRef command_;
};

/**
 * @brief Builder for configuring a Texture resource.
 */
class RDGTextureBuilder {
public:
    RDGTextureBuilder(RDGBuilder* builder, RDGTextureNodeRef texture) : builder_(builder), texture_(texture){};

    RDGTextureBuilder& import(RHITextureRef texture, RHIResourceState init_state);
    RDGTextureBuilder& extent(Extent3D extent);
    RDGTextureBuilder& format(RHIFormat format);
    RDGTextureBuilder& memory_usage(MemoryUsage memory_usage);
    RDGTextureBuilder& allow_read_write();
    RDGTextureBuilder& allow_render_target();
    RDGTextureBuilder& allow_depth_stencil();
    RDGTextureBuilder& mip_levels(uint32_t mip_levels);
    RDGTextureBuilder& array_layers(uint32_t array_layers);

    RDGTextureHandle finish() { return texture_->get_handle(); }

private:
    RDGBuilder* builder_;
    RDGTextureNodeRef texture_;
};

/**
 * @brief Builder for configuring a Buffer resource.
 */
class RDGBufferBuilder {
public:
    RDGBufferBuilder(RDGBuilder* builder, RDGBufferNodeRef buffer) : builder_(builder), buffer_(buffer){};

    RDGBufferBuilder& import(RHIBufferRef buffer, RHIResourceState init_state);
    RDGBufferBuilder& size(uint32_t size);
    RDGBufferBuilder& memory_usage(MemoryUsage memory_usage);
    RDGBufferBuilder& allow_vertex_buffer();
    RDGBufferBuilder& allow_index_buffer();
    RDGBufferBuilder& allow_read_write();
    RDGBufferBuilder& allow_read();

    RDGBufferHandle finish() { return buffer_->get_handle(); }

private:
    RDGBuilder* builder_;
    RDGBufferNodeRef buffer_;
};

/**
 * @brief Builder for configuring a Graphics Render Pass.
 */
class RDGRenderPassBuilder {
public:
    RDGRenderPassBuilder(RDGBuilder* builder, RDGRenderPassNodeRef pass)
        : builder_(builder), pass_(pass), graph_(builder->get_graph()){};

    RDGRenderPassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGRenderPassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGRenderPassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    
    // --- Resource Binding ---
    // These methods declare dependencies. The graph will ensure barriers are inserted.

    RDGRenderPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType view_type = VIEW_TYPE_2D,
                               TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0,
                                     uint32_t size = 0);
    RDGRenderPassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                     TextureViewType view_type = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    
    // --- Attachments ---
    
    RDGRenderPassBuilder& color(uint32_t binding, RDGTextureHandle texture, AttachmentLoadOp load = ATTACHMENT_LOAD_OP_DONT_CARE,
                                AttachmentStoreOp store = ATTACHMENT_STORE_OP_DONT_CARE, Color4 clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                                TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& depth_stencil(RDGTextureHandle texture, AttachmentLoadOp load = ATTACHMENT_LOAD_OP_DONT_CARE,
                                        AttachmentStoreOp store = ATTACHMENT_STORE_OP_DONT_CARE, float clear_depth = 1.0f,
                                        uint32_t clear_stencil = 0, TextureSubresourceRange subresource = {});
    
    // --- Output declarations (for future passes) ---
    // These declare that a resource is modified (or read) by this pass and effectively exported for subsequent passes.

    RDGRenderPassBuilder& output_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& output_read(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& output_read_write(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    
    /**
     * @brief Sets the execution callback for this pass.
     */
    RDGRenderPassBuilder& execute(const RDGPassExecuteFunc& execute);

    RDGRenderPassHandle finish() { return pass_->get_handle(); }

private:
    RDGBuilder* builder_;
    RDGRenderPassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};

/**
 * @brief Builder for configuring a Compute Pass (Dispatch).
 */
class RDGComputePassBuilder {
public:
    RDGComputePassBuilder(RDGBuilder* builder, RDGComputePassNodeRef pass)
        : builder_(builder), pass_(pass), graph_(builder->get_graph()){};

    RDGComputePassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGComputePassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGComputePassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    
    RDGComputePassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType view_type = VIEW_TYPE_2D,
                                TextureSubresourceRange subresource = {});
    RDGComputePassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0,
                                      uint32_t size = 0);
    RDGComputePassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                      TextureViewType view_type = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    
    RDGComputePassBuilder& output_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& output_read(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    RDGComputePassBuilder& output_read_write(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    RDGComputePassBuilder& output_indirect_draw(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);

    RDGComputePassBuilder& execute(const RDGPassExecuteFunc& execute);

    RDGComputePassHandle finish() { return pass_->get_handle(); }

private:
    RDGBuilder* builder_;
    RDGComputePassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};

/**
 * @brief Builder for configuring a Ray Tracing Pass.
 */
class RDGRayTracingPassBuilder {
public:
    RDGRayTracingPassBuilder(RDGBuilder* builder, RDGRayTracingPassNodeRef pass)
        : builder_(builder), pass_(pass), graph_(builder->get_graph()){};

    RDGRayTracingPassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGRayTracingPassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGRayTracingPassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    
    RDGRayTracingPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType view_type = VIEW_TYPE_2D,
                                   TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0,
                                         uint32_t size = 0);
    RDGRayTracingPassBuilder& read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                         TextureViewType view_type = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& output_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& output_read(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& output_read_write(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource = {});

    RDGRayTracingPassBuilder& execute(const RDGPassExecuteFunc& execute);

    RDGRayTracingPassHandle finish() { return pass_->get_handle(); }

private:
    RDGBuilder* builder_;
    RDGRayTracingPassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};

/**
 * @brief Builder for configuring a Present Pass.
 * Handles the transition of the swapchain image to the PRESENT state.
 */
class RDGPresentPassBuilder {
public:
    RDGPresentPassBuilder(RDGBuilder* builder, RDGPresentPassNodeRef pass)
        : builder_(builder), pass_(pass), graph_(builder->get_graph()){};

    RDGPresentPassHandle finish() { return pass_->get_handle(); }

    RDGPresentPassBuilder& texture(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGPresentPassBuilder& present_texture(RDGTextureHandle texture);

private:
    RDGBuilder* builder_;
    RDGPresentPassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};

/**
 * @brief Builder for configuring a Copy/Transfer Pass.
 */
class RDGCopyPassBuilder {
public:
    RDGCopyPassBuilder(RDGBuilder* builder, RDGCopyPassNodeRef pass)
        : builder_(builder), pass_(pass), graph_(builder->get_graph()){};

    RDGCopyPassHandle finish() { return pass_->get_handle(); }

    RDGCopyPassBuilder& from(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& to(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& generate_mips();
    RDGCopyPassBuilder& output_read(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& output_read_write(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});

private:
    RDGBuilder* builder_;
    RDGCopyPassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};