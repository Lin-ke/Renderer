#include "rdg_builder.h"
#include "engine/core/utils/profile_session.h"
#include "engine/core/utils/profiler.h"
#include <algorithm>
#include <map>
#include <optional>
#include <queue>
#include <set>

DEFINE_LOG_TAG(LogRDGCompile, "RDGCompile");

namespace {
struct ResourceAccess {
    size_t pass;
    bool read;
    bool write;
    bool needs_initial_value;
    uint64_t x_begin, x_end;
    uint64_t y_begin = 0, y_end = 1;
    uint32_t aspects = TEXTURE_ASPECT_COLOR;
};
struct ResourceUses {
    std::string name;
    bool imported = false;
    bool exported = false;
    std::vector<ResourceAccess> accesses;
};
struct CellAccess {
    bool read = false;
    bool write = false;
    bool needs_initial_value = false;
};
}

NodeID RDGBuilder::resource_key(RDGResourceNodeRef resource) const {
    auto found = resource_keys_.find(resource->ID());
    return found == resource_keys_.end() ? resource->ID() : found->second;
}

bool RDGBuilder::keep_pass(RDGPassHandle handle, bool keep) {
    auto* pass = dynamic_cast<RDGPassNode*>(graph_->GetNode(handle.id()));
    if (!pass) return false;
    pass->has_side_effects_ = keep;
    return true;
}

bool RDGBuilder::add_dependency(RDGPassHandle handle, RDGPassHandle dependency) {
    auto* pass = dynamic_cast<RDGPassNode*>(graph_->GetNode(handle.id()));
    auto* predecessor = dynamic_cast<RDGPassNode*>(graph_->GetNode(dependency.id()));
    if (!pass || !predecessor) return false;
    pass->dependencies_.push_back(predecessor);
    return true;
}

bool RDGBuilder::extract_texture(RDGTextureHandle handle, RHITextureRef& output) {
    auto* texture = dynamic_cast<RDGTextureNode*>(graph_->GetNode(handle.id()));
    if (!texture) return false;
    output.reset();
    texture->is_exported_ = true;
    extracted_textures_[texture->ID()] = &output;
    return true;
}

bool RDGBuilder::extract_buffer(RDGBufferHandle handle, RHIBufferRef& output) {
    auto* buffer = dynamic_cast<RDGBufferNode*>(graph_->GetNode(handle.id()));
    if (!buffer) return false;
    output.reset();
    buffer->is_exported_ = true;
    extracted_buffers_[buffer->ID()] = &output;
    return true;
}

bool RDGBuilder::compile() {
    PROFILE_FUNCTION();
    compiled_ = false;
    compile_info_ = {};
    execution_passes_.clear();
    resource_keys_.clear(); resource_lifetimes_.clear(); texture_usages_.clear(); buffer_usages_.clear();
    const size_t count = passes_.size();
    std::unordered_map<RDGPassNodeRef, size_t> indices;
    for (size_t i = 0; i < count; ++i) {
        indices[passes_[i]] = i;
        passes_[i]->is_culled_ = true;
        passes_[i]->execution_index_ = UINT32_MAX;
    }
    // A physical imported resource can have several graph names. Its hazards
    // must still be tracked as one resource.
    std::unordered_map<RHIResource*, NodeID> physical_resources;
    std::map<NodeID, ResourceUses> resources;
    auto register_resource = [&](RDGResourceNodeRef node, RHIResource* physical) -> ResourceUses& {
        NodeID key = node->ID();
        if (physical) {
            auto [iter, inserted] = physical_resources.emplace(physical, key);
            key = iter->second;
        }
        resource_keys_[node->ID()] = key;
        auto& uses = resources[key];
        uses.name = node->name();
        uses.imported |= node->is_imported();
        uses.exported |= node->is_exported_;
        return uses;
    };
    std::vector<bool> roots(count, false);
    std::vector<std::string> missing_inputs(count);
    std::vector<std::set<size_t>> data_dependencies(count), ordering_dependencies(count);
    auto fail = [&](const std::string& message) {
        compile_info_.error = message;
        ERR(LogRDGCompile, "{}", message);
        return false;
    };

    // Register exports even when they have no usages, so undefined outputs are
    // diagnosed without accidentally allocating a GPU resource.
    for (const auto& [id, output] : extracted_textures_) {
        auto* node = static_cast<RDGTextureNode*>(graph_->GetNode(id));
        register_resource(node, node->texture_.get());
    }
    for (const auto& [id, output] : extracted_buffers_) {
        auto* node = static_cast<RDGBufferNode*>(graph_->GetNode(id));
        register_resource(node, node->buffer_.get());
    }

    for (size_t i = 0; i < count; ++i) {
        auto* pass = passes_[i];
        roots[i] = pass->has_side_effects_ || pass->node_type() == RDG_PASS_NODE_TYPE_PRESENT;
        for (auto* dependency : pass->dependencies_) {
            auto found = indices.find(dependency);
            if (found == indices.end()) return fail("Pass dependency is outside this graph");
            data_dependencies[i].insert(found->second);
        }
        pass->for_each_texture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
            auto& uses = register_resource(texture, texture->texture_.get());
            texture_usages_[resource_key(texture)].push_back({pass, edge});
            const auto& info = texture->get_info();
            uint32_t mips = info.mip_levels ? info.mip_levels : info.extent.mip_size();
            auto range = edge->subresource;
            if (edge->as_transfer_src || edge->as_transfer_dst ||
                (pass->node_type() == RDG_PASS_NODE_TYPE_PRESENT && !edge->as_present) ||
                (pass->node_type() == RDG_PASS_NODE_TYPE_COPY && edge->is_output())) {
                auto layers = edge->subresource_layer;
                range = {layers.aspect, layers.mip_level, 1, layers.base_array_layer, layers.layer_count};
            }
            if (edge->as_transfer_dst && pass->node_type() == RDG_PASS_NODE_TYPE_COPY && static_cast<RDGCopyPassNode*>(pass)->generate_mip_)
                range = {};
            if (edge->as_color || edge->as_depth_stencil) range.level_count = 1;
            if (!range.aspect) range.aspect = is_depth_format(info.format) ?
                (is_stencil_format(info.format) ? TEXTURE_ASPECT_DEPTH_STENCIL : TEXTURE_ASPECT_DEPTH) : TEXTURE_ASPECT_COLOR;
            if (range.base_mip_level >= mips || range.base_array_layer >= info.array_layers) {
                compile_info_.error = "Invalid texture range in pass " + pass->name(); return;
            }
            if (!range.level_count) range.level_count = mips - range.base_mip_level;
            if (!range.layer_count) range.layer_count = info.array_layers - range.base_array_layer;
            if (range.level_count > mips - range.base_mip_level || range.layer_count > info.array_layers - range.base_array_layer) {
                compile_info_.error = "Texture range exceeds resource in pass " + pass->name(); return;
            }
            bool attachment = edge->as_color || edge->as_depth_stencil;
            bool write = edge->as_color || (edge->as_depth_stencil && !edge->read_only_depth) ||
                edge->as_shader_read_write || edge->as_transfer_dst || edge->is_output();
            bool load = attachment && edge->load_op == ATTACHMENT_LOAD_OP_LOAD;
            bool read = edge->as_shader_read || edge->as_shader_read_write || edge->as_transfer_src ||
                edge->read_only_depth || edge->as_output_read_write || load ||
                (edge->state == RESOURCE_STATE_SHADER_RESOURCE && !edge->is_output());
            if (pass->node_type() == RDG_PASS_NODE_TYPE_PRESENT) {
                if (!edge->as_present) read = true;
                else if (pass->InEdges<RDGTextureEdge>().size() > 1) write = true;
                else read = true;
            }
            uses.accesses.push_back({i, read, write, load || !write,
                range.base_mip_level, uint64_t(range.base_mip_level) + range.level_count,
                range.base_array_layer, uint64_t(range.base_array_layer) + range.layer_count, range.aspect});
            if (uses.imported && write) roots[i] = true;
        });
        pass->for_each_buffer([&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
            auto& uses = register_resource(buffer, buffer->buffer_.get());
            buffer_usages_[resource_key(buffer)].push_back({pass, edge});
            const auto& info = buffer->get_info();
            if (edge->offset >= info.size || (edge->size && edge->size > info.size - edge->offset)) {
                compile_info_.error = "Invalid buffer range in pass " + pass->name(); return;
            }
            bool write = edge->as_shader_read_write || edge->is_output();
            bool read = edge->as_shader_read || edge->as_shader_read_write || edge->as_output_read_write || edge->as_buffer_read;
            uses.accesses.push_back({i, read, write, !write, edge->offset,
                edge->size ? uint64_t(edge->offset) + edge->size : info.size});
            if (uses.imported && write) roots[i] = true;
        });
        if (!compile_info_.error.empty()) return fail(compile_info_.error);
    }

    // Split at usage boundaries rather than enumerating texels or buffer bytes.
    // Each cell has a sequence of resource versions and independent readers.
    for (auto& [key, resource] : resources) {
        if (resource.accesses.empty()) {
            if (resource.exported && !resource.imported) return fail("Output has no producer: " + resource.name);
            continue;
        }
        std::set<uint64_t> xs, ys;
        uint32_t aspects = 0;
        for (const auto& access : resource.accesses) {
            xs.insert(access.x_begin); xs.insert(access.x_end);
            ys.insert(access.y_begin); ys.insert(access.y_end);
            aspects |= access.aspects;
        }
        std::vector<uint64_t> x_bounds(xs.begin(), xs.end()), y_bounds(ys.begin(), ys.end());
        for (uint32_t aspect = TEXTURE_ASPECT_COLOR; aspect <= TEXTURE_ASPECT_STENCIL; aspect <<= 1) {
            if (!(aspects & aspect)) continue;
            for (size_t x = 1; x < x_bounds.size(); ++x) for (size_t y = 1; y < y_bounds.size(); ++y) {
                std::map<size_t, CellAccess> cells;
                for (const auto& access : resource.accesses) {
                    if (!(access.aspects & aspect) || access.x_begin > x_bounds[x-1] || access.x_end < x_bounds[x] ||
                        access.y_begin > y_bounds[y-1] || access.y_end < y_bounds[y]) continue;
                    auto& cell = cells[access.pass];
                    cell.read |= access.read; cell.write |= access.write; cell.needs_initial_value |= access.needs_initial_value;
                }
                if (cells.empty()) continue;
                std::vector<size_t> writers;
                for (const auto& [pass, cell] : cells) if (cell.write) writers.push_back(pass);
                for (size_t i = 1; i < writers.size(); ++i)
                    for (size_t j = 0; j < i; ++j) ordering_dependencies[writers[i]].insert(writers[j]);
                if (resource.exported) {
                    if (writers.empty() && !resource.imported) return fail("Output range has no producer: " + resource.name);
                    if (!writers.empty()) roots[writers.back()] = true;
                }
                for (const auto& [pass, cell] : cells) {
                    if (!cell.read) continue;
                    std::optional<size_t> producer;
                    for (auto writer : writers) if (writer < pass) producer = writer;
                    // Forward references are unambiguous for a pure transient
                    // read: bind its first future producer. Imported reads see
                    // the external initial version instead.
                    if (!producer && !resource.imported && !cell.write) {
                        auto found = std::find_if(writers.begin(), writers.end(), [&](size_t writer) { return writer > pass; });
                        if (found != writers.end()) producer = *found;
                    }
                    if (producer) data_dependencies[pass].insert(*producer);
                    else if (!resource.imported && cell.needs_initial_value)
                        missing_inputs[pass] = "Pass " + passes_[pass]->name() + " reads an undefined version of " + resource.name;
                    size_t after = producer ? std::max(pass, *producer) : pass;
                    for (auto writer : writers) if (writer > after) ordering_dependencies[writer].insert(pass);
                }
            }
        }
    }

    std::vector<bool> live(count, false);
    std::vector<size_t> work;
    for (size_t i = 0; i < count; ++i) if (roots[i]) { live[i] = true; work.push_back(i); }
    while (!work.empty()) {
        auto pass = work.back(); work.pop_back();
        for (auto dependency : data_dependencies[pass]) if (!live[dependency]) {
            live[dependency] = true; work.push_back(dependency);
        }
    }
    std::vector<std::vector<size_t>> successors(count);
    std::vector<size_t> indegree(count, 0);
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    size_t live_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!live[i]) { compile_info_.culled_passes.push_back(passes_[i]->name()); continue; }
        ++live_count;
        if (!missing_inputs[i].empty()) return fail(missing_inputs[i]);
        ordering_dependencies[i].insert(data_dependencies[i].begin(), data_dependencies[i].end());
        for (auto dependency : ordering_dependencies[i]) if (live[dependency]) {
            ++indegree[i]; successors[dependency].push_back(i);
        }
        if (!indegree[i]) ready.push(i);
    }
    compiled_dependencies_.clear();
    if (ProfileSession::instance().generation()) {
        for (size_t i = 0; i < count; ++i) if (live[i]) {
            for (auto dependency : ordering_dependencies[i]) if (live[dependency])
                compiled_dependencies_[passes_[i]->ID()].push_back(passes_[dependency]->ID());
        }
    }
    while (!ready.empty()) {
        auto index = ready.top(); ready.pop();
        execution_passes_.push_back(passes_[index]);
        for (auto next : successors[index]) if (--indegree[next] == 0) ready.push(next);
    }
    if (execution_passes_.size() != live_count) {
        std::string names;
        for (size_t i = 0; i < count; ++i) if (live[i] && indegree[i]) names += (names.empty() ? "" : ", ") + passes_[i]->name();
        execution_passes_.clear();
        return fail("RDG dependency cycle: " + names);
    }
    for (uint32_t i = 0; i < execution_passes_.size(); ++i) {
        auto* pass = execution_passes_[i];
        pass->is_culled_ = false; pass->execution_index_ = i;
        compile_info_.execution_order.push_back(pass->name());
    }
    auto record_lifetimes = [this](const auto& resource_usages) {
        for (const auto& [id, uses] : resource_usages) {
            for (const auto& [pass, edge] : uses) {
                if (pass->is_culled()) continue;
                auto& lifetime = resource_lifetimes_[id];
                const auto index = pass->execution_index();
                lifetime.first_use = std::min(lifetime.first_use, index);
                if (index > lifetime.last_use) {
                    lifetime.last_use = index;
                    lifetime.last_use_has_output = false;
                }
                if (index == lifetime.last_use && edge->is_output()) lifetime.last_use_has_output = true;
            }
        }
    };
    record_lifetimes(texture_usages_);
    record_lifetimes(buffer_usages_);
    compiled_ = true;
    return true;
}
