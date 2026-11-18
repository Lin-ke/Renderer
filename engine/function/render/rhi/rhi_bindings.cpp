#include "rhi.h"
#include "engine/core/utils/profile_probe.h"
#include <algorithm>

DEFINE_LOG_TAG(LogBindings, "RHIBindings");

namespace {
uint32_t register_namespace(ResourceType type) {
    if (type == RESOURCE_TYPE_UNIFORM_BUFFER) return 0;
    if (type == RESOURCE_TYPE_SAMPLER) return 3;
    if (type == RESOURCE_TYPE_RW_TEXTURE || type == RESOURCE_TYPE_RW_BUFFER || type == RESOURCE_TYPE_RW_TEXEL_BUFFER) return 2;
    return 1;
}
}

const ShaderResourceEntry* RHIRootSignatureInfo::find_entry(uint32_t set, uint32_t binding, ResourceType type) const {
    for (const auto& entry : entries)
        if (entry.set == set && entry.binding == binding && register_namespace(entry.type) == register_namespace(type)) return &entry;
    return nullptr;
}

RHIRootSignatureInfo& RHIRootSignatureInfo::add_entry(const ShaderResourceEntry& entry) {
    for (auto& existing : entries) {
        if (existing.set == entry.set && existing.binding == entry.binding && existing.type == entry.type && existing.size == entry.size) {
            existing.frequency |= entry.frequency;
            return *this;
        }
    }
    entries.push_back(entry);
    return *this;
}

RHIRootSignatureInfo& RHIRootSignatureInfo::add_entry(const RHIRootSignatureInfo& other) {
    for (const auto& entry : other.entries) add_entry(entry);
    return *this;
}

RHIRootSignatureInfo& RHIRootSignatureInfo::add_entry_from_reflect(RHIShaderRef shader) {
    if (shader) for (const auto& entry : shader->get_reflect_info().resources) add_entry(entry);
    return *this;
}

bool RHIRootSignatureInfo::is_valid() const {
    uint32_t root_cost = uint32_t(entries.size());
    for (size_t i = 0; i < push_constants.size(); ++i) {
        const auto& constant = push_constants[i];
        if (!constant.size || constant.size % 4 || constant.size > 256 || !constant.frequency || constant.set >= MAX_DESCRIPTOR_SETS) return false;
        if (find_entry(constant.set, constant.binding, RESOURCE_TYPE_UNIFORM_BUFFER)) return false;
        for (size_t j = 0; j < i; ++j)
            if (constant.set == push_constants[j].set && constant.binding == push_constants[j].binding) return false;
        root_cost += constant.size / 4;
    }
    if (root_cost > 64) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& a = entries[i];
        if (a.set >= MAX_DESCRIPTOR_SETS || !a.size || !a.frequency || a.type == RESOURCE_TYPE_NONE ||
            uint64_t(a.binding) + a.size > UINT32_MAX) return false;
        for (size_t j = 0; j < i; ++j) {
            const auto& b = entries[j];
            if (a.set == b.set && register_namespace(a.type) == register_namespace(b.type) &&
                a.binding < uint64_t(b.binding) + b.size && b.binding < uint64_t(a.binding) + a.size) return false;
        }
    }
    return true;
}

RHIDescriptorSet& RHIDescriptorSet::update_descriptor(const RHIDescriptorUpdateInfo& info) {
    auto entry = root_signature_ ? root_signature_->get_info().find_entry(set_, info.binding, info.resource_type) : nullptr;
    bool compatible = entry && (entry->type == info.resource_type ||
        ((entry->type == RESOURCE_TYPE_TEXTURE || entry->type == RESOURCE_TYPE_TEXTURE_CUBE) && info.resource_type == RESOURCE_TYPE_TEXTURE));
    if (!compatible || info.index >= entry->size) {
        ERR(LogBindings, "Invalid descriptor set {}, binding {}, type {}, index {}", set_, info.binding, info.resource_type, info.index);
        return *this;
    }
    if (info.buffer && (info.buffer_offset > info.buffer->get_info().size ||
        info.buffer_range > info.buffer->get_info().size - info.buffer_offset)) {
        ERR(LogBindings, "Descriptor buffer range exceeds allocation");
        return *this;
    }
    auto iter = std::find_if(descriptors_.begin(), descriptors_.end(), [&](const auto& old) {
        return old.binding == info.binding && old.index == info.index && register_namespace(old.resource_type) == register_namespace(info.resource_type);
    });
    if (iter == descriptors_.end()) descriptors_.push_back(info);
    else *iter = info;
    return *this;
}

RHIDescriptorSet& RHIDescriptorSet::write_buffer(uint32_t binding, RHIBufferRef buffer, ResourceType type, uint64_t offset, uint64_t range, uint32_t index) {
    return update_descriptor({.binding = binding, .index = index, .resource_type = type,
                              .buffer = buffer, .buffer_offset = offset, .buffer_range = range});
}

RHIDescriptorSet& RHIDescriptorSet::write_texture(uint32_t binding, RHITextureRef texture, ResourceType type, uint32_t mip, uint32_t index) {
    RHITextureViewRef view;
    if (texture) {
        auto range = texture->get_default_subresource_range();
        TextureViewType view_type = (texture->get_info().type & RESOURCE_TYPE_TEXTURE_CUBE) ? VIEW_TYPE_CUBE : VIEW_TYPE_2D;
        if (type == RESOURCE_TYPE_RW_TEXTURE) {
            range.base_mip_level = mip; range.level_count = 1;
            view_type = texture->get_info().array_layers > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;
        }
        view = RHIBackend::get()->create_texture_view({.texture = texture, .view_type = view_type, .subresource = range});
    }
    return update_descriptor({.binding = binding, .index = index, .resource_type = type, .texture_view = view});
}

RHIDescriptorSet& RHIDescriptorSet::write_sampler(uint32_t binding, RHISamplerRef sampler, uint32_t index) {
    return update_descriptor({.binding = binding, .index = index, .resource_type = RESOURCE_TYPE_SAMPLER, .sampler = sampler});
}

RHIDescriptorSetRef RHIDescriptorSet::snapshot() const {
    ProfileProbe probe("RHI_DescriptorSnapshot");
    auto copy = std::make_shared<RHIDescriptorSet>(root_signature_, set_);
    copy->descriptors_ = descriptors_;
    for (auto& descriptor : copy->descriptors_)
        if (descriptor.buffer && descriptor.resource_type == RESOURCE_TYPE_UNIFORM_BUFFER && descriptor.constant_data.empty())
            descriptor.constant_data = descriptor.buffer->snapshot_data();
    return copy;
}
