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

void RDGBuilder::execute() {
    for (auto& pass : passes_) {
        if (!pass) continue;
        if (pass->is_culled_) {
            continue;
        }

        switch (pass->node_type()) {
            case RDG_PASS_NODE_TYPE_RENDER: execute_pass(static_cast<RDGRenderPassNodeRef>(pass)); break;
            case RDG_PASS_NODE_TYPE_COMPUTE: execute_pass(static_cast<RDGComputePassNodeRef>(pass)); break;
            case RDG_PASS_NODE_TYPE_RAY_TRACING: execute_pass(static_cast<RDGRayTracingPassNodeRef>(pass)); break;
            case RDG_PASS_NODE_TYPE_PRESENT: execute_pass(static_cast<RDGPresentPassNodeRef>(pass)); break;
            case RDG_PASS_NODE_TYPE_COPY: execute_pass(static_cast<RDGCopyPassNodeRef>(pass)); break;
            default: FATAL(LogRDGBuilder, "Unsupported RDG pass type!");
        }
    }

    for (auto& pass : passes_) {
        for (auto& descriptor : pass->pooled_descriptor_sets_) {
            RDGDescriptorSetPool::get(EngineContext::current_frame_index())->release(
                {descriptor.first}, pass->root_signature_, descriptor.second);
        }
    }
    
    passes_.clear();
    graph_ = std::make_shared<DependencyGraph::DependencyGraph>();
    black_board_.clear();
}

void RDGBuilder::create_input_barriers(RDGPassNodeRef pass) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->is_output()) return;
        RHIResourceState previous_state_val = previous_state(texture, pass, edge->subresource, false);
        RHITextureBarrier barrier = {
            .texture = resolve(texture),
            .src_state = previous_state_val,
            .dst_state = edge->state,
            .subresource = edge->subresource
        };
        command_->texture_barrier(barrier);
    });

    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (edge->is_output()) return;
        RHIResourceState previous_state_val = previous_state(buffer, pass, 0, 0, false);
        RHIBufferBarrier barrier = {
            .buffer = resolve(buffer),
            .src_state = previous_state_val,
            .dst_state = edge->state,
            .offset = edge->offset,
            .size = edge->size
        };
        command_->buffer_barrier(barrier);
    });
}

void RDGBuilder::create_output_barriers(RDGPassNodeRef pass) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (!edge->is_output()) return;
        RHIResourceState previous_state_val = previous_state(texture, pass, edge->subresource, true);
        RHITextureBarrier barrier = {
            .texture = resolve(texture),
            .src_state = previous_state_val,
            .dst_state = edge->state,
            .subresource = edge->subresource
        };
        command_->texture_barrier(barrier);
    });

    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (!edge->is_output()) return;
        RHIResourceState previous_state_val = previous_state(buffer, pass, 0, 0, true);
        RHIBufferBarrier barrier = {
            .buffer = resolve(buffer),
            .src_state = previous_state_val,
            .dst_state = edge->state,
            .offset = edge->offset,
            .size = edge->size
        };
        command_->buffer_barrier(barrier);
    });
}

void RDGBuilder::prepare_descriptor_set(RDGPassNodeRef pass) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->is_output()) return;
        
        auto view_pool_alloc = RDGTextureViewPool::get()->allocate({
            .texture = resolve(texture),
            .format = texture->get_info().format,
            .view_type = edge->view_type,
            .subresource = edge->subresource
        });
        RHITextureViewRef view = view_pool_alloc.texture_view;
        pass->pooled_views_.push_back(view);

        if (pass->descriptor_sets_[edge->set] == nullptr && pass->root_signature_ != nullptr) {
            auto descriptor = RDGDescriptorSetPool::get(EngineContext::current_frame_index())
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
                .texture_view = view
            };
            pass->descriptor_sets_[edge->set]->update_descriptor(update_info);
        }
    });

    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (pass->descriptor_sets_[edge->set] == nullptr && pass->root_signature_ != nullptr) {
            auto descriptor = RDGDescriptorSetPool::get(EngineContext::current_frame_index())
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

void RDGBuilder::release_resource(RDGPassNodeRef pass) {
    pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (is_last_used_pass(texture, pass, edge->is_output())) release(texture, edge->state);
    });

    pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (is_last_used_pass(buffer, pass, edge->is_output())) release(buffer, edge->state);
    });

    for (auto& view : pass->pooled_views_) {
        RDGTextureViewPool::get()->release({view});
    }
    pass->pooled_views_.clear();
}

void RDGBuilder::execute_pass(RDGRenderPassNodeRef pass) {
    prepare_descriptor_set(pass);

    RHIRenderPassInfo render_pass_info = {};
    prepare_render_target(pass, render_pass_info);

    RHIRenderPassRef render_pass = EngineContext::rhi()->create_render_pass(render_pass_info);

    command_->push_event(pass->name(), {0.0f, 0.0f, 0.0f});

    create_input_barriers(pass);

    command_->begin_render_pass(render_pass);

    RDGPassContext context = {
        .command = command_,
        .builder = this,
        .descriptors = pass->descriptor_sets_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];
    
    if (pass->execute_) {
        pass->execute_(context);
    }

    command_->end_render_pass();

    create_output_barriers(pass);

    release_resource(pass);

    command_->pop_event();
    
    if (render_pass) {
        render_pass->destroy();
    }
}

void RDGBuilder::execute_pass(RDGComputePassNodeRef pass) {
    prepare_descriptor_set(pass);

    command_->push_event(pass->name(), {1.0f, 0.0f, 0.0f});

    create_input_barriers(pass);

    RDGPassContext context = {
        .command = command_,
        .builder = this,
        .descriptors = pass->descriptor_sets_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];

    if (pass->execute_) {
        pass->execute_(context);
    }

    create_output_barriers(pass);

    release_resource(pass);

    command_->pop_event();
}

void RDGBuilder::execute_pass(RDGRayTracingPassNodeRef pass) {
    prepare_descriptor_set(pass);

    command_->push_event(pass->name(), {0.0f, 1.0f, 0.0f});

    create_input_barriers(pass);

    RDGPassContext context = {
        .command = command_,
        .builder = this,
        .descriptors = pass->descriptor_sets_
    };
    context.pass_index[0] = pass->pass_index_[0];
    context.pass_index[1] = pass->pass_index_[1];
    context.pass_index[2] = pass->pass_index_[2];

    if (pass->execute_) {
        pass->execute_(context);
    }

    create_output_barriers(pass);

    release_resource(pass);

    command_->pop_event();
}

void RDGBuilder::execute_pass(RDGPresentPassNodeRef pass) {
    RDGTextureNodeRef present_texture = nullptr;
    RDGTextureNodeRef texture = nullptr;
    TextureSubresourceLayers subresource = {};

    auto edges = pass->InEdges<RDGTextureEdge>();
    if (edges.size() >= 2) {
        if (edges[0]->as_present) {
            present_texture = edges[0]->From<RDGTextureNode>();
            texture = edges[1]->From<RDGTextureNode>();
            subresource = edges[1]->subresource.aspect == TEXTURE_ASPECT_NONE ?
                resolve(texture)->get_default_subresource_layers() : edges[1]->subresource_layer;
        } else {
            present_texture = edges[1]->From<RDGTextureNode>();
            texture = edges[0]->From<RDGTextureNode>();
            subresource = edges[0]->subresource.aspect == TEXTURE_ASPECT_NONE ?
                resolve(texture)->get_default_subresource_layers() : edges[0]->subresource_layer;
        }
    }

    command_->push_event(pass->name(), {0.0f, 0.0f, 1.0f});

    create_input_barriers(pass);

    if (present_texture && texture) {
        RHITextureBarrier barrier_dst = {
            .texture = resolve(present_texture),
            .src_state = RESOURCE_STATE_PRESENT,
            .dst_state = RESOURCE_STATE_TRANSFER_DST
        };
        command_->texture_barrier(barrier_dst);
        
        command_->copy_texture(resolve(texture), subresource, resolve(present_texture), {TEXTURE_ASPECT_COLOR, 0, 0, 1});
        
        RHITextureBarrier barrier_present = {
            .texture = resolve(present_texture),
            .src_state = RESOURCE_STATE_TRANSFER_DST,
            .dst_state = RESOURCE_STATE_PRESENT
        };
        command_->texture_barrier(barrier_present);
    }

    create_output_barriers(pass);

    release_resource(pass);

    command_->pop_event();
}

void RDGBuilder::execute_pass(RDGCopyPassNodeRef pass) {
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

    command_->push_event(pass->name(), {1.0f, 1.0f, 0.0f});

    create_input_barriers(pass);

    if (from && to) {
        command_->copy_texture(resolve(from), from_subresource, resolve(to), to_subresource);

        if (pass->generate_mip_) {
            RHITextureBarrier barrier = {
                .texture = resolve(to),
                .src_state = RESOURCE_STATE_TRANSFER_DST,
                .dst_state = RESOURCE_STATE_TRANSFER_SRC,
                .subresource = {}
            };
            command_->texture_barrier(barrier);
            command_->generate_mips(resolve(to));

            barrier = {
                .texture = resolve(to),
                .src_state = RESOURCE_STATE_TRANSFER_SRC,
                .dst_state = RESOURCE_STATE_TRANSFER_DST,
                .subresource = {}
            };
            command_->texture_barrier(barrier);
        }
    }

    create_output_barriers(pass);

    release_resource(pass);

    command_->pop_event();
}

RHITextureRef RDGBuilder::resolve(RDGTextureNodeRef texture_node) {
    if (texture_node->texture_ == nullptr) {
        auto pooled_texture = RDGTexturePool::get()->allocate(texture_node->info_);
        texture_node->texture_ = pooled_texture.texture;
        texture_node->init_state_ = pooled_texture.state;
        
        if (texture_node->texture_ && EngineContext::rhi()) {
            EngineContext::rhi()->set_name(texture_node->texture_, texture_node->name());
        }
    }
    return texture_node->texture_;
}

RHIBufferRef RDGBuilder::resolve(RDGBufferNodeRef buffer_node) {
    if (buffer_node->buffer_ == nullptr) {
        auto pooled_buffer = RDGBufferPool::get()->allocate(buffer_node->info_);
        buffer_node->buffer_ = pooled_buffer.buffer;
        buffer_node->init_state_ = pooled_buffer.state;

        if (buffer_node->buffer_ && EngineContext::rhi()) {
            EngineContext::rhi()->set_name(buffer_node->buffer_, buffer_node->name());
        }
    }
    return buffer_node->buffer_;
}

void RDGBuilder::release(RDGTextureNodeRef texture_node, RHIResourceState state) {
    if (texture_node->is_imported()) return;
    if (texture_node->texture_) {
        RDGTexturePool::get()->release({texture_node->texture_, state});
        texture_node->texture_ = nullptr;
        texture_node->init_state_ = RESOURCE_STATE_UNDEFINED;
    }
}

void RDGBuilder::release(RDGBufferNodeRef buffer_node, RHIResourceState state) {
    if (buffer_node->is_imported()) return;
    if (buffer_node->buffer_) {
        RDGBufferPool::get()->release({buffer_node->buffer_, state});
        buffer_node->buffer_ = nullptr;
        buffer_node->init_state_ = RESOURCE_STATE_UNDEFINED;
    }
}

RHIResourceState RDGBuilder::previous_state(RDGTextureNodeRef texture_node, RDGPassNodeRef pass_node,
                                            TextureSubresourceRange subresource, bool output) {
    resolve(texture_node);
    NodeID current_id = pass_node->ID();
    NodeID previous_id = UINT32_MAX;

    RHIResourceState previous_state = texture_node->init_state_;

    texture_node->for_each_pass([&](RDGTextureEdgeRef edge, RDGPassNodeRef pass) {
        bool is_output_first = output ? !edge->is_output() : edge->is_output();
        bool is_previous_pass = output ? pass->ID() <= current_id : pass->ID() < current_id;
        bool is_subresource_covered = subresource.is_default() ||
                                      edge->subresource.is_default() ||
                                      subresource == edge->subresource;

        if (!(is_previous_pass && is_subresource_covered)) return;
        if (pass->ID() > previous_id || previous_id == UINT32_MAX) {
            previous_state = edge->state;
            previous_id = pass->ID();
        } else if (pass->ID() == previous_id && is_output_first) {
            previous_state = edge->state;
            previous_id = pass->ID();
        }
    });

    return previous_state;
}

RHIResourceState RDGBuilder::previous_state(RDGBufferNodeRef buffer_node, RDGPassNodeRef pass_node, uint32_t offset,
                                            uint32_t size, bool output) {
    resolve(buffer_node);
    NodeID current_id = pass_node->ID();
    NodeID previous_id = UINT32_MAX;

    RHIResourceState previous_state = buffer_node->init_state_;

    buffer_node->for_each_pass([&](RDGBufferEdgeRef edge, RDGPassNodeRef pass) {
        bool is_output_first = output ? !edge->is_output() : edge->is_output();
        bool is_previous_pass = output ? pass->ID() <= current_id : pass->ID() < current_id;
        bool is_subresource_covered = (offset == 0 && size == 0) ||
                                      (edge->offset == 0 && edge->size == 0) ||
                                      (offset == edge->offset && size == edge->size);

        if (!(is_previous_pass && is_subresource_covered)) return;
        if (pass->ID() > previous_id || previous_id == UINT32_MAX) {
            previous_state = edge->state;
            previous_id = pass->ID();
        } else if (pass->ID() == previous_id && is_output_first) {
            previous_state = edge->state;
            previous_id = pass->ID();
        }
    });

    return previous_state;
}

bool RDGBuilder::is_last_used_pass(RDGTextureNodeRef texture_node, RDGPassNodeRef pass_node, bool output) {
    NodeID current_id = pass_node->ID();
    bool last = true;

    texture_node->for_each_pass([&](RDGTextureEdgeRef edge, RDGPassNodeRef pass) {
        if (pass->ID() > current_id) last = false;
        if (!output && pass->ID() == current_id && edge->is_output()) last = false;
    });

    return last;
}

bool RDGBuilder::is_last_used_pass(RDGBufferNodeRef buffer_node, RDGPassNodeRef pass_node, bool output) {
    NodeID current_id = pass_node->ID();
    bool last = true;

    buffer_node->for_each_pass([&](RDGBufferEdgeRef edge, RDGPassNodeRef pass) {
        if (pass->ID() > current_id) last = false;
        if (!output && pass->ID() == current_id && edge->is_output()) last = false;
    });

    return last;
}

RDGTextureBuilder& RDGTextureBuilder::import(RHITextureRef texture, RHIResourceState init_state) {
    this->texture_->is_imported_ = true;
    this->texture_->texture_ = texture;
    this->texture_->info_ = texture->get_info();
    this->texture_->init_state_ = init_state;
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

RDGBufferBuilder& RDGBufferBuilder::size(uint32_t size) {
    buffer_->info_.size = size;
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
                                                 uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

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

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

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
                                                   uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

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
    edge->as_output_read_write = true;
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
                                                         uint32_t offset, uint32_t size) {
    RDGBufferEdgeRef edge = graph_->CreateEdge<RDGBufferEdge>();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->as_shader_read = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

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
    edge->as_output_read_write = true;
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
    edge->as_transfer_src = true;

    graph_->Link(graph_->GetNode(texture.id()), pass_, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::to(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->subresource_layer = subresource;
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
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource_layer = subresource;
    edge->as_output_read = true;
    edge->type = RESOURCE_TYPE_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::output_read_write(RDGTextureHandle texture, TextureSubresourceLayers subresource) {
    RDGTextureEdgeRef edge = graph_->CreateEdge<RDGTextureEdge>();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource_layer = subresource;
    edge->as_output_read_write = true;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;

    graph_->Link(pass_, graph_->GetNode(texture.id()), edge);

    return *this;
}

void RDGBuilder::export_graphviz(std::string path) {
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

        out << std::format("    \"{}\" [shape=rectangle, style=filled, fillcolor={}, label=\"{}\"];\n", pass->name(), color, pass->name());

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