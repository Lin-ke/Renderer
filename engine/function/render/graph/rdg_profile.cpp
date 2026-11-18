#include "rdg_builder.h"
#include "engine/core/utils/profile_session.h"
#include "engine/main/engine_context.h"
#include <cereal/archives/json.hpp>
#include <sstream>

void RDGBuilder::begin_profile() {
    profile_.reset();
    if (!ProfileSession::instance().generation()) return;
    profile_.emplace();
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
        profile_->resources.push_back({resource->ID(), uint32_t(resource->node_type()), first, last,
            resource->name(), resource->is_imported()});
    };
    black_board_.for_each_texture([&](auto resource) { add(resource); });
    black_board_.for_each_buffer([&](auto resource) { add(resource); });
}
void RDGBuilder::profile_resource(RDGResourceNodeRef node, RHIResourceRef resource) {
    if (!profile_ || !resource) return;
    for (auto& record : profile_->resources) {
        if (record.id == node->ID()) { record.allocation_id = EngineContext::rhi()->memory_id(resource); break; }
    }
}
void RDGBuilder::end_profile() {
    if (!profile_) return;
    std::ostringstream output;
    { cereal::JSONOutputArchive archive(output); profile_->serialize(archive); }
    ProfileSession::instance().add_graph(output.str());
    profile_.reset();
}
