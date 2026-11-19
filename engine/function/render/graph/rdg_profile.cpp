#include "rdg_builder.h"
#include "engine/core/utils/profile_session.h"
#include "engine/main/engine_context.h"
#include "engine/core/utils/profiler.h"
#include <cereal/archives/json.hpp>
#include <sstream>

void RDGBuilder::begin_profile() {
    PROFILE_FUNCTION();
    profile_.reset();
    profile_resource_indices_.clear();
    profile_generation_ = ProfileSession::instance().generation();
    if (!profile_generation_) return;
    profile_.emplace();
    profile_->passes.reserve(passes_.size());
    profile_->frame_id = ProfileSession::context().frame_id;
    for (auto* pass : passes_) {
        RDGProfilePass record{pass->ID(), uint32_t(pass->node_type()), pass->execution_index(), pass->name(), pass->is_culled()};
        record.dependencies = compiled_dependencies_[pass->ID()];
        pass->for_each_texture([&](auto edge, auto texture) { record.accesses.push_back({texture->ID(), edge->is_output()}); });
        pass->for_each_buffer([&](auto edge, auto buffer) { record.accesses.push_back({buffer->ID(), edge->is_output()}); });
        profile_->passes.push_back(std::move(record));
    }
    auto add = [&](RDGResourceNodeRef resource) {
        auto lifetime = resource_lifetimes_.find(resource_key(resource));
        const uint32_t first = lifetime == resource_lifetimes_.end() ? UINT32_MAX : lifetime->second.first_use;
        const uint32_t last = lifetime == resource_lifetimes_.end() ? UINT32_MAX : lifetime->second.last_use;
        profile_resource_indices_[resource->ID()] = profile_->resources.size();
        profile_->resources.push_back({resource->ID(), uint32_t(resource->node_type()), first, last,
            resource->name(), resource->is_imported()});
    };
    black_board_.for_each_texture([&](auto resource) { add(resource); });
    black_board_.for_each_buffer([&](auto resource) { add(resource); });
}
void RDGBuilder::profile_resource(RDGResourceNodeRef node, RHIResourceRef resource) {
    if (!profile_ || !resource) return;
    const auto found = profile_resource_indices_.find(node->ID());
    if (found == profile_resource_indices_.end()) return;
    auto& record = profile_->resources[found->second];
    if (!record.allocation_id) record.allocation_id = EngineContext::rhi()->memory_id(resource);
}
void RDGBuilder::end_profile() {
    PROFILE_FUNCTION();
    if (!profile_) return;
    size_t bytes = sizeof(RDGProfile) + profile_->passes.capacity() * sizeof(RDGProfilePass) +
        profile_->resources.capacity() * sizeof(RDGProfileResource);
    for (const auto& pass : profile_->passes)
        bytes += pass.name.capacity() + pass.dependencies.capacity() * sizeof(uint32_t) + pass.accesses.capacity() * sizeof(RDGProfileAccess);
    for (const auto& resource : profile_->resources) bytes += resource.name.capacity();
    ProfileSession::instance().enqueue_graph(EngineContext::thread_pool(), profile_generation_, bytes,
        [snapshot = std::move(*profile_)]() mutable {
            std::ostringstream output;
            { cereal::JSONOutputArchive archive(output); snapshot.serialize(archive); }
            return output.str();
        });
    profile_.reset();
    profile_resource_indices_.clear();
}
