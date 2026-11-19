#pragma once

#include "engine/core/dependency_graph/dependency_graph.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/graph/rdg_handle.h"
#include "engine/function/render/graph/rdg_node.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include "rdg_profile.h"
#include <optional>
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

struct RDGCompileInfo {
    std::vector<std::string> execution_order;
    std::vector<std::string> culled_passes;
    std::string error;
};

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
 * - Async Compute / Multi-queue support.
 * - Fine-grained subresource barriers.
 * - Multi-threaded command recording.
 */
class RDGBuilder {
public:
    RDGBuilder() = default;

    ~RDGBuilder(){};

    // --- Creation Methods ---
    // These methods create nodes in the graph and return a builder object for configuration.

    RDGTextureBuilder create_texture(std::string name);
    RDGBufferBuilder create_buffer(std::string name);
    
    // Dependencies determine execution order; independent passes stay stable.
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
     * 1. Culls unused passes and topologically sorts live dependencies.
     * 2. Allocates resources (if not imported).
     * 3. Generates barriers.
     * 4. Executes the pass callback (recording commands).
     * 5. Releases transient resources.
     */
    bool compile(); // Does not allocate GPU resources or execute callbacks.
    bool execute(const RHICommandListRef& command);
    bool execute_compiled(RHICommandListRef command, uint32_t frame_slot);
    bool prepare_pipeline(uint32_t frame_slot, const std::function<RHICommandListRef(uint32_t)>& command_for_pass,
        bool validate_access = true);
    bool set_texture_final_state(RDGTextureHandle texture, RHIResourceState state);
    bool bind_external_texture(RDGTextureHandle handle, const RHITextureRef& texture);
    bool bind_external_buffer(RDGBufferHandle handle, const RHIBufferRef& buffer);
    const RDGCompileInfo& get_compile_info() const { return compile_info_; }
    const std::vector<RDGPassNodeRef>& get_execution_passes() const { return execution_passes_; }
    bool keep_pass(RDGPassHandle pass, bool keep = true);
    bool add_dependency(RDGPassHandle pass, RDGPassHandle dependency);
    bool extract_texture(RDGTextureHandle texture, RHITextureRef& output);
    bool extract_buffer(RDGBufferHandle buffer, RHIBufferRef& output);

    /**
     * @brief Exports the graph structure to a GraphViz (.dot) file for visualization.
     */
    void export_graphviz(std::string path);

    /**
     * @brief Gets all passes in the graph for visualization.
     */
    const std::vector<RDGPassNodeRef>& get_passes() const { return passes_; }

    /**
     * @brief Gets the blackboard for accessing named resources.
     */
    RDGBlackBoard& get_blackboard() { return black_board_; }
    const RDGBlackBoard& get_blackboard() const { return black_board_; }

private:
    void begin_profile();
    void end_profile();
    void profile_resource(RDGResourceNodeRef node, RHIResourceRef resource);
    std::optional<RDGProfile> profile_;
    uint64_t profile_generation_ = 0;
    std::unordered_map<NodeID, size_t> profile_resource_indices_;
    std::unordered_map<NodeID, std::vector<NodeID>> compiled_dependencies_;
    struct TextureStates {
        RHITextureRef resource;
        std::vector<RHIResourceState> subresources;
        uint32_t mips;
        uint32_t layers;
        TextureAspectFlags aspects;
    };
    using TextureStateMap = std::unordered_map<RHITexture*, TextureStates>;
    using BufferStateMap = std::unordered_map<RHIBuffer*, RHIResourceState>;
    void plan_pass_barriers(RDGPassNodeRef pass, const RHICommandListRef& command,
        TextureStateMap& textures, BufferStateMap& buffers, bool after);
    void plan_texture_barriers(RDGTextureNodeRef node, TextureSubresourceRange range, RHIResourceState state,
        const RHICommandListRef& command, TextureStateMap& textures);
    void prepare_descriptor_set(RDGPassNodeRef pass);
    void validate_pass_accesses(RDGPassNodeRef pass, const RHICommandListRef& command);
    void prepare_render_target(RDGRenderPassNodeRef pass, RHIRenderPassInfo& render_pass_info);
    void release_pass_views(RDGPassNodeRef pass);
    void execute_pass(RDGRenderPassNodeRef pass, const RHICommandListRef& command);
    void execute_pass(RDGComputePassNodeRef pass, const RHICommandListRef& command);
    void execute_pass(RDGRayTracingPassNodeRef pass, const RHICommandListRef& command);
    void execute_pass(RDGPresentPassNodeRef pass, const RHICommandListRef& command);
    void execute_pass(RDGCopyPassNodeRef pass, const RHICommandListRef& command);

    template <typename Type, typename Handle>
    Handle get_pass(std::string name) {
        auto node = black_board_.pass(name);
        if (node == nullptr) {
            // INFO(LogRDG, "Unable to find RDG resource, please check name!");
            return Handle(UINT32_MAX);
        }
        return dynamic_cast<Type>(node)->get_handle();
    }

public:
    RHITextureRef resolve(RDGTextureNodeRef texture_node);
    RHITextureRef resolve(RDGTextureHandle handle);
    RHIBufferRef resolve(RDGBufferNodeRef buffer_node);
    RHIBufferRef resolve(RDGBufferHandle handle);
private:
    void release(RDGTextureNodeRef texture_node, RHIResourceState state);
    void release(RDGBufferNodeRef buffer_node, RHIResourceState state);

    std::vector<RDGPassNodeRef> passes_;
    std::vector<RDGPassNodeRef> execution_passes_;
    RDGCompileInfo compile_info_;
    std::unordered_map<NodeID, NodeID> resource_keys_;
    struct ResourceLifetime {
        uint32_t first_use = UINT32_MAX;
        uint32_t last_use = 0;
        bool last_use_has_output = false;
    };
    std::unordered_map<NodeID, ResourceLifetime> resource_lifetimes_;
    NodeID resource_key(RDGResourceNodeRef resource) const;
    std::unordered_map<NodeID, std::vector<std::pair<RDGPassNodeRef, RDGTextureEdgeRef>>> texture_usages_;
    std::unordered_map<NodeID, std::vector<std::pair<RDGPassNodeRef, RDGBufferEdgeRef>>> buffer_usages_;
    std::unordered_map<NodeID, RHITextureRef*> extracted_textures_;
    std::unordered_map<NodeID, RHIBufferRef*> extracted_buffers_;
    std::unordered_map<NodeID, RHIResourceState> final_texture_states_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_ = std::make_shared<DependencyGraph::DependencyGraph>();
    RDGBlackBoard black_board_;

    uint32_t frame_slot_ = 0;
    bool compiled_ = false;
};

/**
 * @brief Builder for configuring a Texture resource.
 */
class RDGTextureBuilder {
public:
    RDGTextureBuilder(RDGBuilder* builder, RDGTextureNodeRef texture) : builder_(builder), texture_(texture){};

    RDGTextureBuilder& import(RHITextureRef texture, RHIResourceState init_state);
    RDGTextureBuilder& import(const RHITextureInfo& info, RHIResourceState init_state);
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
    RDGBufferBuilder& import(const RHIBufferInfo& info, RHIResourceState init_state);
    RDGBufferBuilder& size(uint64_t size);
    RDGBufferBuilder& stride(uint32_t stride);
    RDGBufferBuilder& memory_usage(MemoryUsage memory_usage);
    RDGBufferBuilder& allow_vertex_buffer();
    RDGBufferBuilder& allow_index_buffer();
    RDGBufferBuilder& allow_read_write();
    RDGBufferBuilder& allow_read();
    RDGBufferBuilder& allow_indirect();

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

    RDGRenderPassBuilder& never_cull(bool keep = true) { builder_->keep_pass(pass_->get_handle(), keep); return *this; }
    RDGRenderPassBuilder& depends_on(RDGPassHandle dependency) { builder_->add_dependency(pass_->get_handle(), dependency); return *this; }

    RDGRenderPassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGRenderPassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGRenderPassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    RDGRenderPassBuilder& indirect_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& vertex_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& index_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& vertex_read(const RHIBufferRef& buffer);
    RDGRenderPassBuilder& index_read(const RHIBufferRef& buffer);
    RDGRenderPassBuilder& sample(RDGTextureHandle texture, TextureSubresourceRange range = {});
    RDGRenderPassBuilder& sample(const RHITextureRef& texture);
    
    // --- Resource Binding ---
    // These methods declare dependencies. The graph will ensure barriers are inserted.

    RDGRenderPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER);
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
                                        uint32_t clear_stencil = 0, TextureSubresourceRange subresource = {},
                                        bool read_only_depth = false);
    
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

    RDGComputePassBuilder& never_cull(bool keep = true) { builder_->keep_pass(pass_->get_handle(), keep); return *this; }
    RDGComputePassBuilder& depends_on(RDGPassHandle dependency) { builder_->add_dependency(pass_->get_handle(), dependency); return *this; }

    RDGComputePassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGComputePassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGComputePassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    RDGComputePassBuilder& indirect_read(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    
    RDGComputePassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER);
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

    RDGRayTracingPassBuilder& never_cull(bool keep = true) { builder_->keep_pass(pass_->get_handle(), keep); return *this; }
    RDGRayTracingPassBuilder& depends_on(RDGPassHandle dependency) { builder_->add_dependency(pass_->get_handle(), dependency); return *this; }

    RDGRayTracingPassBuilder& pass_index(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);
    RDGRayTracingPassBuilder& root_signature(RHIRootSignatureRef root_signature);
    RDGRayTracingPassBuilder& descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set);
    
    RDGRayTracingPassBuilder& read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER);
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

    RDGPresentPassBuilder& never_cull(bool keep = true) { builder_->keep_pass(pass_->get_handle(), keep); return *this; }
    RDGPresentPassBuilder& depends_on(RDGPassHandle dependency) { builder_->add_dependency(pass_->get_handle(), dependency); return *this; }

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

    RDGCopyPassBuilder& never_cull(bool keep = true) { builder_->keep_pass(pass_->get_handle(), keep); return *this; }
    RDGCopyPassBuilder& depends_on(RDGPassHandle dependency) { builder_->add_dependency(pass_->get_handle(), dependency); return *this; }

    RDGCopyPassHandle finish() { return pass_->get_handle(); }

    RDGCopyPassBuilder& from(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& to(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& generate_mips();
    RDGCopyPassBuilder& read_buffer(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& write_buffer(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& execute(const RDGPassExecuteFunc& execute);
    RDGCopyPassBuilder& output_read(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& output_read_write(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});

private:
    RDGBuilder* builder_;
    RDGCopyPassNodeRef pass_;

    std::shared_ptr<DependencyGraph::DependencyGraph> graph_;
};
