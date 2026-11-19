#include "engine/function/render/graph/rdg_builder.h"

#include "engine/core/dependency_graph/dependency_graph.h"
#include "engine/core/log/Log.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/graph/rdg_edge.h"
#include "engine/function/render/graph/rdg_handle.h"
#include "engine/function/render/graph/rdg_node.h"
#include "engine/function/render/graph/rdg_pool.h"
#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/rhi/rhi_resource.h"

#include "engine/core/utils/profiler.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <format>
#include <fstream>

DEFINE_LOG_TAG(LogRDGBuilder, "RDGBuilder");

RDGPassNodeRef RDGBlackBoard::pass(std::string name) {
    auto found = passes_.find(name);
    if (found != passes_.end()) {
        return found->second;
    }
    return nullptr;
}

RDGBufferNodeRef RDGBlackBoard::buffer(std::string name) {
    auto found = buffers_.find(name);
    if (found != buffers_.end()) {
        return found->second;
    }
    return nullptr;
}

RDGTextureNodeRef RDGBlackBoard::texture(std::string name) {
    auto found = textures_.find(name);
    if (found != textures_.end()) {
        return found->second;
    }
    return nullptr;
}

void RDGBlackBoard::add_pass(RDGPassNodeRef pass) {
    passes_[pass->name()] = pass;
}

void RDGBlackBoard::add_buffer(RDGBufferNodeRef buffer) {
    buffers_[buffer->name()] = buffer;
}

void RDGBlackBoard::add_texture(RDGTextureNodeRef texture) {
    textures_[texture->name()] = texture;
}

RDGTextureBuilder RDGBuilder::create_texture(std::string name) {
    RDGTextureNodeRef texture_node = graph_->CreateNode<RDGTextureNode>(name);
    black_board_.add_texture(texture_node);
    return RDGTextureBuilder(this, texture_node);
}

RDGBufferBuilder RDGBuilder::create_buffer(std::string name) {
    RDGBufferNodeRef buffer_node = graph_->CreateNode<RDGBufferNode>(name);
    black_board_.add_buffer(buffer_node);
    return RDGBufferBuilder(this, buffer_node);
}

RDGRenderPassBuilder RDGBuilder::create_render_pass(std::string name) {
    RDGRenderPassNodeRef pass_node = graph_->CreateNode<RDGRenderPassNode>(name);
    black_board_.add_pass(pass_node);
    passes_.push_back(pass_node);
    return RDGRenderPassBuilder(this, pass_node);
}

RDGComputePassBuilder RDGBuilder::create_compute_pass(std::string name) {
    RDGComputePassNodeRef pass_node = graph_->CreateNode<RDGComputePassNode>(name);
    black_board_.add_pass(pass_node);
    passes_.push_back(pass_node);
    return RDGComputePassBuilder(this, pass_node);
}

RDGRayTracingPassBuilder RDGBuilder::create_ray_tracing_pass(std::string name) {
    RDGRayTracingPassNodeRef pass_node = graph_->CreateNode<RDGRayTracingPassNode>(name);
    black_board_.add_pass(pass_node);
    passes_.push_back(pass_node);
    return RDGRayTracingPassBuilder(this, pass_node);
}

RDGPresentPassBuilder RDGBuilder::create_present_pass(std::string name) {
    RDGPresentPassNodeRef pass_node = graph_->CreateNode<RDGPresentPassNode>(name);
    black_board_.add_pass(pass_node);
    passes_.push_back(pass_node);
    return RDGPresentPassBuilder(this, pass_node);
}

RDGCopyPassBuilder RDGBuilder::create_copy_pass(std::string name) {
    RDGCopyPassNodeRef pass_node = graph_->CreateNode<RDGCopyPassNode>(name);
    black_board_.add_pass(pass_node);
    passes_.push_back(pass_node);
    return RDGCopyPassBuilder(this, pass_node);
}

RDGTextureHandle RDGBuilder::get_texture(std::string name) {
    auto node = black_board_.texture(name);
    if (node == nullptr) {
        WARN(LogRDGBuilder, "Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGTextureHandle(UINT32_MAX);
    }
    return node->get_handle();
}

RDGBufferHandle RDGBuilder::get_buffer(std::string name) {
    auto node = black_board_.buffer(name);
    if (node == nullptr) {
        WARN(LogRDGBuilder, "Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGBufferHandle(UINT32_MAX);
    }
    return node->get_handle();
}

bool RDGBuilder::execute(const RHICommandListRef& command) {
    return compile() && execute_compiled(command, EngineContext::current_frame_index());
}

bool RDGBuilder::execute_compiled(RHICommandListRef command, uint32_t frame_slot) {
    return prepare_pipeline(frame_slot, [&](uint32_t) { return command; }, false);
}

bool RDGBuilder::set_texture_final_state(RDGTextureHandle handle, RHIResourceState state) {
    auto* node = dynamic_cast<RDGTextureNode*>(graph_->GetNode(handle.id()));
    if (!node || (!node->is_imported() && !node->is_exported_)) return false;
    final_texture_states_[handle.id()] = state;
    return true;
}

bool RDGBuilder::prepare_pipeline(uint32_t frame_slot, const std::function<RHICommandListRef(uint32_t)>& command_for_pass,
    bool validate_access) {
    if (!compiled_) return false;
    compile_info_.error.clear();
    frame_slot_ = frame_slot;
    for (const auto& [id, lifetime] : resource_lifetimes_) {
        auto* texture = dynamic_cast<RDGTextureNode*>(graph_->GetNode(id));
        if (texture && texture->is_imported() && !texture->texture_) {
            compile_info_.error = "RDG external texture '" + texture->name() + "' is unbound before " +
                execution_passes_[lifetime.first_use]->name();
            ERR(LogRDGBuilder, "{}", compile_info_.error);
            return false;
        }
        auto* buffer = dynamic_cast<RDGBufferNode*>(graph_->GetNode(id));
        if (buffer && buffer->is_imported() && !buffer->buffer_) {
            compile_info_.error = "RDG external buffer '" + buffer->name() + "' is unbound before " +
                execution_passes_[lifetime.first_use]->name();
            ERR(LogRDGBuilder, "{}", compile_info_.error);
            return false;
        }
    }
    for (auto* pass : execution_passes_) {
        if (validate_access && pass->node_type() == RDG_PASS_NODE_TYPE_COPY && static_cast<RDGCopyPassNode*>(pass)->generate_mip_) {
            compile_info_.error = "RDG mip generation requires a compute pass per mip: " + pass->name();
            ERR(LogRDGBuilder, "{}", compile_info_.error);
            return false;
        }
    }
    TextureStateMap textures;
    BufferStateMap buffers;
    begin_profile();
    for (auto* pass : execution_passes_) {
        auto command = command_for_pass(pass->execution_index());
        if (!command) return false;

        auto profile_context = ProfileSession::context();
        profile_context.pass_id = pass->ID();
        ProfileContextScope profile_scope(profile_context);
        PROFILE_SCOPE("RDGPass");
        command->gpu_timestamp_begin(pass->name());
        command->push_event(pass->name());
        plan_pass_barriers(pass, command, textures, buffers, false);
        if (validate_access) validate_pass_accesses(pass, command);
        switch (pass->node_type()) {
            case RDG_PASS_NODE_TYPE_RENDER: execute_pass(static_cast<RDGRenderPassNodeRef>(pass), command); break;
            case RDG_PASS_NODE_TYPE_COMPUTE: execute_pass(static_cast<RDGComputePassNodeRef>(pass), command); break;
            case RDG_PASS_NODE_TYPE_RAY_TRACING: execute_pass(static_cast<RDGRayTracingPassNodeRef>(pass), command); break;
            case RDG_PASS_NODE_TYPE_PRESENT: execute_pass(static_cast<RDGPresentPassNodeRef>(pass), command); break;
            case RDG_PASS_NODE_TYPE_COPY: execute_pass(static_cast<RDGCopyPassNodeRef>(pass), command); break;
            default: FATAL(LogRDGBuilder, "Unsupported RDG pass type!");
        }
        command->set_access_checks({}, {});
        if (command->has_unplanned_commands())
            compile_info_.error = "RDG pass '" + pass->name() + "' issued a manual barrier or mip generation";
        if (!compile_info_.error.empty()) {
            ERR(LogRDGBuilder, "{}", compile_info_.error);
            return false;
        }
        plan_pass_barriers(pass, command, textures, buffers, true);
        command->pop_event();
        command->gpu_timestamp_end();
        release_pass_views(pass);
    }

    auto finish = command_for_pass(static_cast<uint32_t>(execution_passes_.size()));
    if (!finish) return false;
    for (const auto& [id, state] : final_texture_states_) {
        auto* node = static_cast<RDGTextureNode*>(graph_->GetNode(id));
        if (!resolve(node)) return false;
        plan_texture_barriers(node, {}, state, finish, textures);
    }
    for (auto* pass : execution_passes_) {
        pass->for_each_texture([&](RDGTextureEdgeRef, RDGTextureNodeRef node) {
            if (node->is_imported() || node->is_exported_ || !node->texture_) return;
            plan_texture_barriers(node, {}, RESOURCE_STATE_COMMON, finish, textures);
            release(node, RESOURCE_STATE_COMMON);
        });
        pass->for_each_buffer([&](RDGBufferEdgeRef, RDGBufferNodeRef node) {
            if (!node->buffer_) return;
            release(node, RESOURCE_STATE_COMMON);
        });
    }
    for (auto& pass : passes_) {
        for (auto& descriptor : pass->pooled_descriptor_sets_) {
            RDGDescriptorSetPool::get(frame_slot_)->release(
                {descriptor.first}, pass->root_signature_, descriptor.second);
        }
    }
    
    for (auto& [id, output] : extracted_textures_)
        *output = resolve(static_cast<RDGTextureNode*>(graph_->GetNode(id)));
    for (auto& [id, output] : extracted_buffers_)
        *output = resolve(static_cast<RDGBufferNode*>(graph_->GetNode(id)));
    extracted_textures_.clear(); extracted_buffers_.clear(); final_texture_states_.clear();
    end_profile();
    execution_passes_.clear(); resource_keys_.clear(); resource_lifetimes_.clear(); texture_usages_.clear(); buffer_usages_.clear();
    passes_.clear();
    compiled_dependencies_.clear();
    graph_ = std::make_shared<DependencyGraph::DependencyGraph>();
    black_board_.clear();
    compiled_ = false;
    return true;
}

bool RDGBuilder::bind_external_texture(RDGTextureHandle handle, const RHITextureRef& texture) {
    auto* node = dynamic_cast<RDGTextureNode*>(graph_->GetNode(handle.id()));
    if (!compiled_ || !node || !node->is_imported() || node->texture_ || !texture ||
        node->info_ != texture->get_info()) {
        return false;
    }
    for (const auto& [id, lifetime] : resource_lifetimes_) {
        auto* other = dynamic_cast<RDGTextureNode*>(graph_->GetNode(id));
        if (other && other != node && other->texture_ == texture && resource_key(other) != resource_key(node)) return false;
    }
    node->texture_ = texture;
    return true;
}

bool RDGBuilder::bind_external_buffer(RDGBufferHandle handle, const RHIBufferRef& buffer) {
    auto* node = dynamic_cast<RDGBufferNode*>(graph_->GetNode(handle.id()));
    if (!compiled_ || !node || !node->is_imported() || node->buffer_ || !buffer) return false;
    const auto& actual = buffer->get_info(); const auto& expected = node->info_;
    if (actual.size < expected.size || actual.stride != expected.stride || actual.memory_usage != expected.memory_usage ||
        (actual.type & expected.type) != expected.type) return false;
    for (const auto& [id, lifetime] : resource_lifetimes_) {
        auto* other = dynamic_cast<RDGBufferNode*>(graph_->GetNode(id));
        if (other && other != node && other->buffer_ == buffer && resource_key(other) != resource_key(node)) return false;
    }
    node->buffer_ = buffer;
    return true;
}

void RDGBuilder::plan_texture_barriers(RDGTextureNodeRef node, TextureSubresourceRange range, RHIResourceState state,
    const RHICommandListRef& command, TextureStateMap& textures) {
    auto resource = resolve(node);
    const auto& defaults = resource->get_default_subresource_range();
    auto [found, inserted] = textures.try_emplace(resource.get());
    auto& current = found->second;
    if (inserted) {
        current.resource = resource;
        current.mips = defaults.level_count;
        current.layers = defaults.layer_count;
        current.aspects = is_depth_stencil_format(resource->get_info().format) ? TEXTURE_ASPECT_DEPTH_STENCIL : defaults.aspect;
        current.subresources.assign(size_t(current.mips) * current.layers *
            (current.aspects == TEXTURE_ASPECT_DEPTH_STENCIL ? 2 : 1), node->init_state_);
    }
    const auto mip_end = range.level_count ? range.base_mip_level + range.level_count : current.mips;
    const auto layer_end = range.layer_count ? range.base_array_layer + range.layer_count : current.layers;
    const auto aspects = range.aspect ? range.aspect : current.aspects;
    bool uav_barrier = false;
    for (auto aspect : {TEXTURE_ASPECT_COLOR, TEXTURE_ASPECT_DEPTH, TEXTURE_ASPECT_STENCIL}) {
        if (!(aspects & current.aspects & aspect)) continue;
        const auto plane = aspect == TEXTURE_ASPECT_STENCIL && current.aspects == TEXTURE_ASPECT_DEPTH_STENCIL ? 1u : 0u;
        for (auto layer = range.base_array_layer; layer < layer_end; ++layer) {
            for (auto mip = range.base_mip_level; mip < mip_end; ++mip) {
                auto& previous = current.subresources[(plane * current.layers + layer) * current.mips + mip];
                if (previous != state) {
                    command->texture_barrier({resource, previous, state, {aspect, mip, 1, layer, 1}});
                    previous = state;
                } else if (state == RESOURCE_STATE_UNORDERED_ACCESS) {
                    uav_barrier = true;
                }
            }
        }
    }
    if (uav_barrier) command->texture_barrier({resource, state, state, range});
}

void RDGBuilder::plan_pass_barriers(RDGPassNodeRef pass, const RHICommandListRef& command,
    TextureStateMap& textures, BufferStateMap& buffers, bool after) {
    const bool present_copy = pass->node_type() == RDG_PASS_NODE_TYPE_PRESENT && pass->InEdges<RDGTextureEdge>().size() > 1;
    std::optional<NodeID> read_only_depth;
    if (!after && pass->node_type() == RDG_PASS_NODE_TYPE_RENDER) {
        for (auto* edge : pass->InEdges<RDGTextureEdge>()) {
            if (edge->read_only_depth) {
                read_only_depth = resource_key(edge->From<RDGTextureNode>());
                break;
            }
        }
    }
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef node) {
        auto state = edge->state;
        if (edge->as_present) {
            if (after && !present_copy) return;
            state = present_copy && !after ? RESOURCE_STATE_TRANSFER_DST : RESOURCE_STATE_PRESENT;
        } else if (edge->is_output() != after) {
            return;
        }
        auto range = edge->subresource;
        if (edge->as_color || edge->as_depth_stencil) range.level_count = 1;
        if (state == RESOURCE_STATE_SHADER_RESOURCE && read_only_depth == resource_key(node))
            state = RESOURCE_STATE_DEPTH_STENCIL_READ;
        plan_texture_barriers(node, range, state, command, textures);
    });
    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef node) {
        if ((edge->is_output() && !edge->as_buffer_write) != after) return;
        auto resource = resolve(node);
        if (resource->get_info().memory_usage != MEMORY_USAGE_GPU_ONLY) return;
        const auto state = edge->as_shader_read && edge->type == RESOURCE_TYPE_UNIFORM_BUFFER ?
            RESOURCE_STATE_VERTEX_BUFFER : edge->state;
        auto [iter, inserted] = buffers.try_emplace(resource.get(), node->init_state_);
        auto& previous = iter->second;
        if (previous != state || state == RESOURCE_STATE_UNORDERED_ACCESS) {
            command->buffer_barrier({resource, previous, state});
            previous = state;
        }
    });
}

void RDGBuilder::validate_pass_accesses(RDGPassNodeRef pass, const RHICommandListRef& command) {
    std::unordered_multimap<RHIBuffer*, RHIResourceState> buffer_accesses;
    std::unordered_multimap<RHITexture*, std::pair<RHIResourceState, TextureSubresourceRange>> texture_accesses;
    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef node) {
        if (edge->is_output() && !edge->as_buffer_write) return;
        buffer_accesses.emplace(resolve(node).get(), edge->as_shader_read && edge->type == RESOURCE_TYPE_UNIFORM_BUFFER ?
            RESOURCE_STATE_VERTEX_BUFFER : edge->state);
    });
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef node) {
        if (edge->is_output()) return;
        auto range = edge->subresource;
        if (edge->as_color || edge->as_depth_stencil) range.level_count = 1;
        auto state = edge->state;
        if (edge->as_present && pass->InEdges<RDGTextureEdge>().size() > 1)
            state = RESOURCE_STATE_TRANSFER_DST;
        texture_accesses.emplace(resolve(node).get(), std::pair{state, range});
    });
    auto fail = [this, pass](const RHIResource& resource) {
        compile_info_.error = "RDG pass '" + pass->name() + "' used an undeclared resource: " + resource.get_name();
    };
    command->set_access_checks([buffer_accesses = std::move(buffer_accesses), fail](const RHIBufferRef& resource, RHIResourceState state) {
        if (!resource) return;
        if (state == RESOURCE_STATE_VERTEX_BUFFER &&
            (resource->get_info().type & RESOURCE_TYPE_UNIFORM_BUFFER) &&
            resource->get_info().memory_usage != MEMORY_USAGE_GPU_ONLY) return;
        const auto [begin, end] = buffer_accesses.equal_range(resource.get());
        if (std::none_of(begin, end, [state](const auto& entry) { return entry.second == state; })) fail(*resource);
    }, [texture_accesses = std::move(texture_accesses), fail](const RHITextureRef& resource, RHIResourceState state, TextureSubresourceRange range) {
        if (!resource) return;
        const auto& defaults = resource->get_default_subresource_range();
        const auto mip_end = range.level_count ? range.base_mip_level + range.level_count : defaults.level_count;
        const auto layer_end = range.layer_count ? range.base_array_layer + range.layer_count : defaults.layer_count;
        const auto [begin, end] = texture_accesses.equal_range(resource.get());
        for (auto layer = range.base_array_layer; layer < layer_end; ++layer) {
            for (auto mip = range.base_mip_level; mip < mip_end; ++mip) {
                if (std::none_of(begin, end, [&](const auto& entry) {
                    const auto& [usage, declared] = entry.second;
                    return usage == state && mip >= declared.base_mip_level && layer >= declared.base_array_layer &&
                        (!declared.level_count || mip - declared.base_mip_level < declared.level_count) &&
                        (!declared.layer_count || layer - declared.base_array_layer < declared.layer_count);
                })) return fail(*resource);
            }
        }
    });
}

void RDGBuilder::prepare_descriptor_set(RDGPassNodeRef pass) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        // For compute passes, read_write textures are output edges but still need descriptors
        // For render passes, pure output edges (color/depth) don't need descriptor set binding
        bool needs_descriptor = edge->as_shader_read || edge->as_shader_read_write;
        if (!needs_descriptor) return;
        
        auto view_pool_alloc = RDGTextureViewPool::get()->allocate({
            .texture = resolve(texture),
            .format = texture->get_info().format,
            .view_type = edge->view_type,
            .subresource = edge->subresource
        });
        RHITextureViewRef view = view_pool_alloc.texture_view;
        pass->pooled_views_.push_back(view);

        if (pass->descriptor_sets_[edge->set] == nullptr && pass->root_signature_ != nullptr) {
            auto descriptor = RDGDescriptorSetPool::get(frame_slot_)
                                  ->allocate(pass->root_signature_, edge->set)
                                  .descriptor;
            pass->descriptor_sets_[edge->set] = descriptor;
            pass->pooled_descriptor_sets_.push_back({descriptor, edge->set});
        }

        if (needs_descriptor && pass->descriptor_sets_[edge->set] != nullptr) {
            RHIDescriptorUpdateInfo update_info = {
                .binding = edge->binding,
                .index = edge->index,
                .resource_type = edge->type,
                .texture_view = view
            };
            pass->descriptor_sets_[edge->set]->update_descriptor(update_info);
        }
    });

    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (!edge->as_shader_read && !edge->as_shader_read_write) return;
        if (pass->descriptor_sets_[edge->set] == nullptr && pass->root_signature_ != nullptr) {
            auto descriptor = RDGDescriptorSetPool::get(frame_slot_)
                                  ->allocate(pass->root_signature_, edge->set)
                                  .descriptor;
            pass->descriptor_sets_[edge->set] = descriptor;
            pass->pooled_descriptor_sets_.push_back({descriptor, edge->set});
        }

        if ((edge->as_shader_read || edge->as_shader_read_write) && pass->descriptor_sets_[edge->set] != nullptr) {
            RHIDescriptorUpdateInfo update_info = {
                .binding = edge->binding,
                .index = edge->index,
                .resource_type = edge->type,
                .buffer = resolve(buffer),
                .buffer_offset = edge->offset,
                .buffer_range = edge->size
            };
            pass->descriptor_sets_[edge->set]->update_descriptor(update_info);
        }
    });
}

void RDGBuilder::prepare_render_target(RDGRenderPassNodeRef pass, RHIRenderPassInfo& render_pass_info) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->is_output()) return;
        if (!(edge->as_color || edge->as_depth_stencil)) return;

        auto view_pool_alloc = RDGTextureViewPool::get()->allocate({
            .texture = resolve(texture),
            .format = texture->get_info().format,
            .view_type = edge->view_type,
            .subresource = edge->subresource
        });
        RHITextureViewRef view = view_pool_alloc.texture_view;
        pass->pooled_views_.push_back(view);

        if (edge->as_color) {
            render_pass_info.extent = {texture->get_info().extent.width, texture->get_info().extent.height};
            render_pass_info.layers = edge->subresource.layer_count > 0 ? edge->subresource.layer_count : 1;

            render_pass_info.color_attachments[edge->binding] = {
                .texture_view = view,
                .load_op = edge->load_op,
                .store_op = edge->store_op,
                .clear_color = edge->clear_color
            };
        } else if (edge->as_depth_stencil) {
            render_pass_info.extent = {texture->get_info().extent.width, texture->get_info().extent.height};
            render_pass_info.layers = edge->subresource.layer_count > 0 ? edge->subresource.layer_count : 1;

            render_pass_info.depth_stencil_attachment = {
                .texture_view = view,
                .load_op = edge->load_op,
                .store_op = edge->store_op,
                .clear_depth = edge->clear_depth,
                .clear_stencil = edge->clear_stencil,
                .read_only = edge->read_only_depth
            };
        }
    });
}

void RDGBuilder::release_pass_views(RDGPassNodeRef pass) {
    for (auto& view : pass->pooled_views_) {
        RDGTextureViewPool::get()->release({view});
    }
    pass->pooled_views_.clear();
}

void RDGBuilder::execute_pass(RDGRenderPassNodeRef pass, const RHICommandListRef& command) {
    prepare_descriptor_set(pass);

    RHIRenderPassInfo render_pass_info = {};
    prepare_render_target(pass, render_pass_info);

    RHIRenderPassRef render_pass = EngineContext::rhi()->create_render_pass(render_pass_info);

    command->begin_render_pass(render_pass);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptor_sets_,
        .frame_slot = frame_slot_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];
    
    if (pass->execute_) {
        pass->execute_(context);
    }

    command->end_render_pass();
}

void RDGBuilder::execute_pass(RDGComputePassNodeRef pass, const RHICommandListRef& command) {
    prepare_descriptor_set(pass);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptor_sets_,
        .frame_slot = frame_slot_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];

    if (pass->execute_) {
        pass->execute_(context);
    }
}

void RDGBuilder::execute_pass(RDGRayTracingPassNodeRef pass, const RHICommandListRef& command) {
    prepare_descriptor_set(pass);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptor_sets_,
        .frame_slot = frame_slot_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];

    if (pass->execute_) {
        pass->execute_(context);
    }
}

void RDGBuilder::execute_pass(RDGPresentPassNodeRef pass, const RHICommandListRef& command) {
    RDGTextureNodeRef present_texture = nullptr;
    RDGTextureNodeRef texture = nullptr;
    TextureSubresourceLayers subresource = {};

    auto edges = pass->InEdges<RDGTextureEdge>();
    if (edges.size() >= 2) {
        if (edges[0]->as_present) {
            present_texture = edges[0]->From<RDGTextureNode>();
            texture = edges[1]->From<RDGTextureNode>();
            subresource = edges[1]->subresource_layer.is_default() ?
                resolve(texture)->get_default_subresource_layers() : edges[1]->subresource_layer;
        } else {
            present_texture = edges[1]->From<RDGTextureNode>();
            texture = edges[0]->From<RDGTextureNode>();
            subresource = edges[0]->subresource_layer.is_default() ?
                resolve(texture)->get_default_subresource_layers() : edges[0]->subresource_layer;
        }
    }

    if (present_texture && texture) {
        command->copy_texture(resolve(texture), subresource, resolve(present_texture), {TEXTURE_ASPECT_COLOR, 0, 0, 1});
    }
}

void RDGBuilder::execute_pass(RDGCopyPassNodeRef pass, const RHICommandListRef& command) {
    RDGTextureNodeRef from = nullptr;
    RDGTextureNodeRef to = nullptr;
    TextureSubresourceLayers from_subresource = {};
    TextureSubresourceLayers to_subresource = {};

    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->as_transfer_src) {
            from = texture;
            from_subresource = edge->subresource_layer;
        } else if (edge->as_transfer_dst) {
            to = texture;
            to_subresource = edge->subresource_layer;
        }
    });

    if (pass->execute_) {
        pass->execute_({command, this, pass->descriptor_sets_, frame_slot_});
    } else if (from && to) {
        command->copy_texture(resolve(from), from_subresource, resolve(to), to_subresource);
        if (pass->generate_mip_) command->generate_mips(resolve(to));
    }
}

RHITextureRef RDGBuilder::resolve(RDGTextureNodeRef texture_node) {
    if (texture_node->texture_ == nullptr) {
        if (texture_node->is_imported()) return nullptr;
        auto pooled_texture = RDGTexturePool::get()->allocate(texture_node->info_);
        texture_node->texture_ = pooled_texture.texture;
        texture_node->init_state_ = pooled_texture.state;
        
        if (texture_node->texture_ && EngineContext::rhi()) {
            EngineContext::rhi()->set_name(texture_node->texture_, texture_node->name());
        }
    }
    profile_resource(texture_node, texture_node->texture_);
    return texture_node->texture_;
}

RHITextureRef RDGBuilder::resolve(RDGTextureHandle handle) {
    auto* node = dynamic_cast<RDGTextureNode*>(graph_->GetNode(handle.id()));
    if (!node) return nullptr;
    return resolve(node);
}

RHIBufferRef RDGBuilder::resolve(RDGBufferHandle handle) {
    auto* node = dynamic_cast<RDGBufferNode*>(graph_->GetNode(handle.id()));
    if (!node) return nullptr;
    return resolve(node);
}

RHIBufferRef RDGBuilder::resolve(RDGBufferNodeRef buffer_node) {
    if (buffer_node->buffer_ == nullptr) {
        if (buffer_node->is_imported()) return nullptr;
        auto pooled_buffer = RDGBufferPool::get()->allocate(buffer_node->info_);
        buffer_node->buffer_ = pooled_buffer.buffer;
        buffer_node->init_state_ = pooled_buffer.state;

        if (buffer_node->buffer_ && EngineContext::rhi()) {
            EngineContext::rhi()->set_name(buffer_node->buffer_, buffer_node->name());
        }
    }
    profile_resource(buffer_node, buffer_node->buffer_);
    return buffer_node->buffer_;
}

void RDGBuilder::release(RDGTextureNodeRef texture_node, RHIResourceState state) {
    if (texture_node->is_imported() || texture_node->is_exported_) return;
    if (texture_node->texture_) {
        RDGTexturePool::get()->release({texture_node->texture_, state});
        texture_node->texture_ = nullptr;
        texture_node->init_state_ = RESOURCE_STATE_UNDEFINED;
    }
}

void RDGBuilder::release(RDGBufferNodeRef buffer_node, RHIResourceState state) {
    if (buffer_node->is_imported() || buffer_node->is_exported_) return;
    if (buffer_node->buffer_) {
        RDGBufferPool::get()->release({buffer_node->buffer_, state});
        buffer_node->buffer_ = nullptr;
        buffer_node->init_state_ = RESOURCE_STATE_UNDEFINED;
    }
}



RDGTextureBuilder& RDGTextureBuilder::import(RHITextureRef texture, RHIResourceState init_state) {
    import(texture->get_info(), init_state);
    this->texture_->texture_ = texture;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::import(const RHITextureInfo& info, RHIResourceState init_state) {
    texture_->is_imported_ = true;
    texture_->info_ = info;
    texture_->init_state_ = init_state;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::extent(Extent3D extent) {
    texture_->info_.extent = extent;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::format(RHIFormat format) {
    texture_->info_.format = format;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::memory_usage(enum MemoryUsage memory_usage) {
    texture_->info_.memory_usage = memory_usage;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::allow_read_write() {
    texture_->info_.type |= RESOURCE_TYPE_RW_TEXTURE;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::allow_render_target() {
    texture_->info_.type |= RESOURCE_TYPE_RENDER_TARGET;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::allow_depth_stencil() {
    texture_->info_.type |= RESOURCE_TYPE_DEPTH_STENCIL;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::mip_levels(uint32_t mip_levels) {
    texture_->info_.mip_levels = mip_levels;
    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::array_layers(uint32_t array_layers) {
    texture_->info_.array_layers = array_layers;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::import(RHIBufferRef buffer, RHIResourceState init_state) {
    this->buffer_->is_imported_ = true;
    this->buffer_->buffer_ = buffer;
    this->buffer_->info_ = buffer->get_info();
    this->buffer_->init_state_ = init_state;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::import(const RHIBufferInfo& info, RHIResourceState init_state) {
    buffer_->is_imported_ = true; buffer_->info_ = info; buffer_->init_state_ = init_state;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::size(uint64_t size) {
    buffer_->info_.size = size;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::stride(uint32_t stride) {
    buffer_->info_.stride = stride;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::allow_indirect() {
    buffer_->info_.type |= RESOURCE_TYPE_INDIRECT_BUFFER;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::memory_usage(enum MemoryUsage memory_usage) {
    buffer_->info_.memory_usage = memory_usage;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::allow_vertex_buffer() {
    buffer_->info_.type |= RESOURCE_TYPE_VERTEX_BUFFER;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::allow_index_buffer() {
    buffer_->info_.type |= RESOURCE_TYPE_INDEX_BUFFER;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::allow_read_write() {
    buffer_->info_.type |= RESOURCE_TYPE_RW_BUFFER;
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::allow_read() {
    buffer_->info_.type |= RESOURCE_TYPE_UNIFORM_BUFFER;
    return *this;
}

namespace {
void add_buffer_access(const std::shared_ptr<DependencyGraph::DependencyGraph>& graph, RDGPassNode* pass,
    RDGBufferHandle buffer, RHIResourceState state, bool write, uint32_t offset, uint32_t size) {
    auto* edge = graph->CreateEdge<RDGBufferEdge>();
    edge->state = state; edge->offset = offset; edge->size = size;
    edge->as_buffer_read = !write; edge->as_buffer_write = write;
    if (write) graph->Link(pass, graph->GetNode(buffer.id()), edge);
    else graph->Link(graph->GetNode(buffer.id()), pass, edge);
}
}

RDGRenderPassBuilder& RDGRenderPassBuilder::indirect_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_INDIRECT_ARGUMENT, false, offset, size);
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::vertex_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_VERTEX_BUFFER, false, offset, size);
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::index_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_INDEX_BUFFER, false, offset, size);
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::vertex_read(const RHIBufferRef& buffer) {
    if (buffer) vertex_read(builder_->create_buffer("ExternalVertex").import(buffer, RESOURCE_STATE_COMMON).finish());
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::index_read(const RHIBufferRef& buffer) {
    if (buffer) index_read(builder_->create_buffer("ExternalIndex").import(buffer, RESOURCE_STATE_COMMON).finish());
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::sample(RDGTextureHandle texture, TextureSubresourceRange range) {
    auto* edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = range;
    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);
    return *this;
}
RDGRenderPassBuilder& RDGRenderPassBuilder::sample(const RHITextureRef& texture) {
    if (texture) sample(builder_->create_texture("ExternalSample").import(texture, RESOURCE_STATE_SHADER_RESOURCE).finish());
    return *this;
}
RDGComputePassBuilder& RDGComputePassBuilder::indirect_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_INDIRECT_ARGUMENT, false, offset, size);
    return *this;
}
RDGCopyPassBuilder& RDGCopyPassBuilder::read_buffer(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_TRANSFER_SRC, false, offset, size);
    return *this;
}
RDGCopyPassBuilder& RDGCopyPassBuilder::write_buffer(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    add_buffer_access(graph_, pass_, buffer, RESOURCE_STATE_TRANSFER_DST, true, offset, size);
    return *this;
}
RDGCopyPassBuilder& RDGCopyPassBuilder::execute(const RDGPassExecuteFunc& execute) {
    pass_->execute_ = execute;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::pass_index(uint32_t x, uint32_t y, uint32_t z) {
    pass_->pass_index_[0] = x;
    pass_->pass_index_[1] = y;
    pass_->pass_index_[2] = z;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::root_signature(RHIRootSignatureRef root_signature) {
    pass_->root_signature_ = root_signature;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set) {
    pass_->descriptor_sets_[set] = descriptor_set;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset,
                                                 uint32_t size, ResourceType type) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = type;

    graph_->Link(graph_->GetNode(buffer.id()), pass_, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                 TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset,
                                                       uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                       TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::color(uint32_t binding, RDGTextureHandle texture, AttachmentLoadOp load, AttachmentStoreOp store,
                                                  Color4 clear_color, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_COLOR_ATTACHMENT;
    edge->load_op = load;
    edge->store_op = store;
    edge->clear_color = clear_color;
    edge->subresource = subresource;
    edge->as_color = true;
    edge->binding = binding;
    edge->view_type = subresource.layer_count > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::depth_stencil(RDGTextureHandle texture, AttachmentLoadOp load, AttachmentStoreOp store,
                                                          float clear_depth, uint32_t clear_stencil, TextureSubresourceRange subresource,
                                                          bool read_only_depth) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = read_only_depth ? RESOURCE_STATE_SHADER_RESOURCE : RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT;
    edge->load_op = load;
    edge->store_op = store;
    edge->clear_depth = clear_depth;
    edge->clear_stencil = clear_stencil;
    edge->subresource = subresource;
    edge->as_depth_stencil = true;
    edge->read_only_depth = read_only_depth;
    edge->view_type = subresource.layer_count > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    // For read-only depth, create edge from resource to pass (like read())
    // For writable depth, create edge from pass to resource (write)
    if (read_only_depth) {
        graph_->Link(graph_->GetNode(texture.id()), pass_, edge);
    } else {
        graph_->Link(pass_, graph_->GetNode(texture.id()), edge);
    }

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::output_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::output_read(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::output_read_write(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::execute(const RDGPassExecuteFunc& execute) {
    pass_->execute_ = execute;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::pass_index(uint32_t x, uint32_t y, uint32_t z) {
    pass_->pass_index_[0] = x;
    pass_->pass_index_[1] = y;
    pass_->pass_index_[2] = z;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::root_signature(RHIRootSignatureRef root_signature) {
    pass_->root_signature_ = root_signature;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set) {
    pass_->descriptor_sets_[set] = descriptor_set;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset,
                                                  uint32_t size, ResourceType type) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = type;

    graph_->Link(graph_->GetNode(buffer.id()), pass_, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                   TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer,
                                                         uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                         TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::output_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::output_read(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::output_read_write(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::output_indirect_draw(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_indirect_draw = true;
    edge->type = RESOURCE_TYPE_INDIRECT_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::execute(const RDGPassExecuteFunc& execute) {
    pass_->execute_ = execute;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::pass_index(uint32_t x, uint32_t y, uint32_t z) {
    pass_->pass_index_[0] = x;
    pass_->pass_index_[1] = y;
    pass_->pass_index_[2] = z;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::root_signature(RHIRootSignatureRef root_signature) {
    pass_->root_signature_ = root_signature;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::descriptor_set(uint32_t set, RHIDescriptorSetRef descriptor_set) {
    pass_->descriptor_sets_[set] = descriptor_set;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer,
                                                         uint32_t offset, uint32_t size, ResourceType type) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = type;

    graph_->Link(graph_->GetNode(buffer.id()), pass_, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                         TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer,
                                                               uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::read_write(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture,
                                                               TextureViewType view_type, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_shader_read_write = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->view_type = view_type;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::output_read(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::output_read(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::output_read_write(RDGBufferHandle buffer, uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph_->Link(pass_, graph_->GetNode(buffer.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::output_read_write(RDGTextureHandle texture, TextureSubresourceRange subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::execute(const RDGPassExecuteFunc& execute) {
    pass_->execute_ = execute;
    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::texture(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->subresource_layer = subresource;
    edge->subresource = {subresource.aspect, subresource.mip_level, 1, subresource.base_array_layer, subresource.layer_count};

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::present_texture(RDGTextureHandle texture) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_PRESENT;
    edge->as_present = true;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::from(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->subresource_layer = subresource;
    edge->subresource = {subresource.aspect, subresource.mip_level, 1, subresource.base_array_layer, subresource.layer_count};
    edge->as_transfer_src = true;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::to(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->subresource_layer = subresource;
    edge->subresource = {subresource.aspect, subresource.mip_level, 1, subresource.base_array_layer, subresource.layer_count};
    edge->as_transfer_dst = true;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::generate_mips() {
    pass_->generate_mip_ = true;
    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::output_read(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource_layer = subresource;
    edge->subresource = {subresource.aspect, subresource.mip_level, 1, subresource.base_array_layer, subresource.layer_count};
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::output_read_write(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource_layer = subresource;
    edge->subresource = {subresource.aspect, subresource.mip_level, 1, subresource.base_array_layer, subresource.layer_count};
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

void RDGBuilder::export_graphviz(std::string path) {
    compile();
    std::ofstream out(path);
    if (!out.is_open()) {
        ERR(LogRDGBuilder, "Failed to open file for graphviz export: {}", path.c_str());
        return;
    }

    out << "digraph RDG {\n";
    out << "    rankdir=LR;\n";
    out << "    node [fontname=\"Arial\"];\n";
    out << "    edge [fontname=\"Arial\", fontsize=10];\n";

    // Passes and their edges
    black_board_.for_each_pass([&](RDGPassNodeRef pass) {
        std::string color = "orange";
        if (pass->node_type() == RDG_PASS_NODE_TYPE_COMPUTE) color = "yellow";
        else if (pass->node_type() == RDG_PASS_NODE_TYPE_COPY) color = "lightgrey";
        else if (pass->node_type() == RDG_PASS_NODE_TYPE_PRESENT) color = "lightblue";
        else if (pass->node_type() == RDG_PASS_NODE_TYPE_RAY_TRACING) color = "violet";

        std::string label = pass->name();
        if (pass->is_culled()) { color = "grey"; label += " (culled)"; }
        else label = std::to_string(pass->execution_index()) + ": " + label;
        out << std::format("    \"{}\" [shape=rectangle, style=filled, fillcolor={}, label=\"{}\"];\n", pass->name(), color, label);

        // Writes (Pass -> Resource)
        for (auto* edge : pass->OutEdges<RDGTextureEdge>()) {
            auto* texture = edge->To<RDGTextureNode>();
            std::string label = "Write";
            if (edge->as_color) label = "Color";
            else if (edge->as_depth_stencil) label = "Depth";
            else if (edge->as_shader_read_write) label = "UAV";
            else if (edge->as_transfer_dst) label = "Transfer";
            
            out << std::format("    \"{}\" -> \"{}\" [label=\"{}\", color=red];\n", pass->name(), texture->name(), label);
        }
        for (auto* edge : pass->OutEdges<RDGBufferEdge>()) {
            auto* buffer = edge->To<RDGBufferNode>();
            out << std::format("    \"{}\" -> \"{}\" [label=\"Write\", color=red];\n", pass->name(), buffer->name());
        }

        // Reads (Resource -> Pass)
        for (auto* edge : pass->InEdges<RDGTextureEdge>()) {
            auto* texture = edge->From<RDGTextureNode>();
            std::string label = "Read";
            if (edge->as_shader_read) label = "SRV";
            else if (edge->as_transfer_src) label = "Transfer";
            else if (edge->as_present) label = "Present";

            out << std::format("    \"{}\" -> \"{}\" [label=\"{}\", color=blue];\n", texture->name(), pass->name(), label);
        }
        for (auto* edge : pass->InEdges<RDGBufferEdge>()) {
            auto* buffer = edge->From<RDGBufferNode>();
            out << std::format("    \"{}\" -> \"{}\" [label=\"Read\", color=blue];\n", buffer->name(), pass->name());
        }
    });

    // Textures (Nodes only)
    black_board_.for_each_texture([&](RDGTextureNodeRef texture) {
        out << std::format("    \"{}\" [shape=box, style=filled, fillcolor=lightgreen, label=\"{}\\n{}\"];\n", 
            texture->name(), texture->name(), "Texture");
    });

    // Buffers (Nodes only)
    black_board_.for_each_buffer([&](RDGBufferNodeRef buffer) {
        out << std::format("    \"{}\" [shape=cylinder, style=filled, fillcolor=lightcyan, label=\"{}\\n{}\"];\n", 
            buffer->name(), buffer->name(), "Buffer");
    });

    out << "}\n";
    out.close();
    INFO(LogRDGBuilder, "Exported RDG to {}", path.c_str());
}
