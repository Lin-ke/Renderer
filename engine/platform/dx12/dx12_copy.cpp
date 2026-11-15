#include "platform_rhi.h"
#include <algorithm>
#include <cstring>

DEFINE_LOG_TAG(LogDX12Copy, "DX12Copy");
namespace {
ComPtr<ID3D12Resource> staging_buffer(DX12Backend& backend, uint64_t size, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    auto state = type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
    if (FAILED(backend.get_device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource))))
        ERR(LogDX12Copy, "Cannot allocate transfer buffer");
    return resource;
}
struct Footprints {
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
    std::vector<UINT> rows;
    std::vector<UINT64> row_sizes;
    UINT64 total = 0;
};
Footprints footprints(DX12Backend& backend, DX12Texture& texture, TextureSubresourceLayers layers) {
    Footprints result;
    uint32_t count = layers.layer_count ? layers.layer_count : texture.get_array_layers() - layers.base_array_layer;
    result.layouts.resize(count); result.rows.resize(count); result.row_sizes.resize(count);
    auto desc = texture.get_resource()->GetDesc();
    for (uint32_t i = 0; i < count; ++i) {
        auto offset = (result.total + 511) & ~511ull;
        UINT64 size = 0;
        backend.get_device()->GetCopyableFootprints(&desc, texture.get_subresource_index(layers.mip_level, layers.base_array_layer + i),
            1, offset, &result.layouts[i], &result.rows[i], &result.row_sizes[i], &size);
        result.total = offset + size;
    }
    return result;
}
}

RHIBufferRef DX12Backend::create_static_buffer(const RHIBufferInfo& info, const void* data) {
    if (!data || !info.size || !device_) return nullptr;
    auto upload = staging_buffer(*this, info.size, D3D12_HEAP_TYPE_UPLOAD);
    if (!upload) return nullptr;
    void* mapped = nullptr;
    const D3D12_RANGE read = {0, 0};
    if (FAILED(upload->Map(0, &read, &mapped))) return nullptr;
    std::memcpy(mapped, data, info.size);
    upload->Unmap(0, nullptr);

    auto local_info = info;
    local_info.memory_usage = MEMORY_USAGE_GPU_ONLY;
    local_info.creation_flag = BUFFER_CREATION_NONE;
    auto buffer = create_buffer(local_info);
    if (!buffer) return nullptr;

    ensure_recording();
    if (!is_recording()) return nullptr;
    auto* destination = static_cast<DX12Buffer*>(buffer.get());
    destination->transition(get_command_list(), D3D12_RESOURCE_STATE_COPY_DEST);
    get_command_list()->CopyBufferRegion(destination->get_resource(), 0, upload.Get(), 0, info.size);
    retain(buffer);
    retain_upload(std::move(upload));
    return buffer;
}

void DX12CommandContext::copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) {
    auto backend = backend_.lock(); if (!backend || !src || !dst) return;
    if (src_offset > src->get_info().size || size > src->get_info().size - src_offset || dst_offset > dst->get_info().size || size > dst->get_info().size - dst_offset) return;
    backend->ensure_recording(); backend->retain(src); backend->retain(dst);
    auto* source = static_cast<DX12Buffer*>(src.get()); auto* destination = static_cast<DX12Buffer*>(dst.get());
    source->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    destination->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_COPY_DEST);
    backend->get_command_list()->CopyBufferRegion(destination->get_resource(), dst_offset, source->get_resource(), src_offset, size);
}
void DX12CommandContext::copy_buffer_to_texture(RHIBufferRef src, uint64_t offset, RHITextureRef dst, TextureSubresourceLayers layers) {
    auto backend = backend_.lock(); if (!backend || !src || !dst) return;
    backend->ensure_recording(); backend->retain(src); backend->retain(dst);
    auto* texture = static_cast<DX12Texture*>(dst.get());
    auto layout = footprints(*backend, *texture, layers);
    auto bytes = src->snapshot_data();
    uint64_t required = 0;
    for (size_t i = 0; i < layout.layouts.size(); ++i) required += layout.row_sizes[i] * layout.rows[i] * layout.layouts[i].Footprint.Depth;
    if (offset > bytes.size() || required > bytes.size() - offset) { ERR(LogDX12Copy, "Texture uploads require a CPU-visible source buffer with enough packed data"); return; }
    auto upload = staging_buffer(*backend, layout.total, D3D12_HEAP_TYPE_UPLOAD); if (!upload) return;
    uint8_t* cpu = nullptr; D3D12_RANGE read = {0, 0};
    if (FAILED(upload->Map(0, &read, reinterpret_cast<void**>(&cpu)))) return;
    for (size_t i = 0; i < layout.layouts.size(); ++i) {
        auto& placed = layout.layouts[i];
        for (uint32_t z = 0; z < placed.Footprint.Depth; ++z) for (uint32_t row = 0; row < layout.rows[i]; ++row) {
            memcpy(cpu + placed.Offset + (uint64_t(z) * layout.rows[i] + row) * placed.Footprint.RowPitch, bytes.data() + offset, size_t(layout.row_sizes[i]));
            offset += layout.row_sizes[i];
        }
    }
    upload->Unmap(0, nullptr);
    auto* command = backend->get_command_list();
    texture->transition(command, D3D12_RESOURCE_STATE_COPY_DEST, layers.mip_level, 1, layers.base_array_layer, UINT(layout.layouts.size()));
    for (uint32_t i = 0; i < layout.layouts.size(); ++i) {
        D3D12_TEXTURE_COPY_LOCATION source = {}; source.pResource = upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = layout.layouts[i];
        D3D12_TEXTURE_COPY_LOCATION destination = {}; destination.pResource = texture->get_resource(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = texture->get_subresource_index(layers.mip_level, layers.base_array_layer + i);
        command->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    backend->retain_upload(upload);
}
void DX12CommandContext::copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers layers, RHIBufferRef dst, uint64_t offset) {
    auto backend = backend_.lock(); if (!backend || !src || !dst || dst->get_info().memory_usage != MEMORY_USAGE_GPU_TO_CPU) return;
    backend->ensure_recording(); backend->retain(src); backend->retain(dst);
    auto* texture = static_cast<DX12Texture*>(src.get());
    auto layout = footprints(*backend, *texture, layers);
    uint64_t size = 0;
    for (size_t i = 0; i < layout.layouts.size(); ++i) size += layout.row_sizes[i] * layout.rows[i] * layout.layouts[i].Footprint.Depth;
    if (offset > dst->get_info().size || size > dst->get_info().size - offset) return;
    auto readback = staging_buffer(*backend, layout.total, D3D12_HEAP_TYPE_READBACK); if (!readback) return;
    auto* command = backend->get_command_list();
    texture->transition(command, D3D12_RESOURCE_STATE_COPY_SOURCE, layers.mip_level, 1, layers.base_array_layer, UINT(layout.layouts.size()));
    for (uint32_t i = 0; i < layout.layouts.size(); ++i) {
        D3D12_TEXTURE_COPY_LOCATION source = {}; source.pResource = texture->get_resource(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = texture->get_subresource_index(layers.mip_level, layers.base_array_layer + i);
        D3D12_TEXTURE_COPY_LOCATION destination = {}; destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = layout.layouts[i];
        command->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    backend->on_complete([readback, dst, layout, offset]() mutable {
        uint8_t* cpu = nullptr; D3D12_RANGE range = {0, SIZE_T(layout.total)};
        if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&cpu)))) return;
        auto bytes = dst->snapshot_data(); bytes.resize(size_t(dst->get_info().size));
        for (size_t i = 0; i < layout.layouts.size(); ++i) {
            auto& placed = layout.layouts[i];
            for (uint32_t z = 0; z < placed.Footprint.Depth; ++z) for (uint32_t row = 0; row < layout.rows[i]; ++row) {
                memcpy(bytes.data() + offset, cpu + placed.Offset + (uint64_t(z) * layout.rows[i] + row) * placed.Footprint.RowPitch, size_t(layout.row_sizes[i]));
                offset += layout.row_sizes[i];
            }
        }
        D3D12_RANGE written = {0, 0}; readback->Unmap(0, &written);
        static_cast<DX12Buffer*>(dst.get())->complete_readback(std::move(bytes));
    });
}
void DX12CommandContext::copy_texture(RHITextureRef src, TextureSubresourceLayers source_layers, RHITextureRef dst, TextureSubresourceLayers destination_layers) {
    auto backend = backend_.lock(); if (!backend || !src || !dst) return;
    backend->ensure_recording(); backend->retain(src); backend->retain(dst);
    auto* source = static_cast<DX12Texture*>(src.get()); auto* destination = static_cast<DX12Texture*>(dst.get());
    uint32_t count = source_layers.layer_count ? source_layers.layer_count : source->get_array_layers() - source_layers.base_array_layer;
    source->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_COPY_SOURCE, source_layers.mip_level, 1, source_layers.base_array_layer, count);
    destination->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_COPY_DEST, destination_layers.mip_level, 1, destination_layers.base_array_layer, count);
    for (uint32_t i = 0; i < count; ++i) {
        D3D12_TEXTURE_COPY_LOCATION from = {}; from.pResource = source->get_resource(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = source->get_subresource_index(source_layers.mip_level, source_layers.base_array_layer + i);
        D3D12_TEXTURE_COPY_LOCATION to = {}; to.pResource = destination->get_resource(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = destination->get_subresource_index(destination_layers.mip_level, destination_layers.base_array_layer + i);
        backend->get_command_list()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
}
bool DX12CommandContext::read_texture(RHITextureRef texture, void* data, uint32_t size) {
    auto backend = backend_.lock(); if (!backend || !texture || !data) return false;
    auto info = texture->get_info(); uint64_t bytes = uint64_t(info.extent.width) * info.extent.height * DX12Backend::get_format_byte_size(info.format);
    if (!bytes || size < bytes) return false;
    auto buffer = backend->create_buffer({.size = bytes, .memory_usage = MEMORY_USAGE_GPU_TO_CPU}); if (!buffer) return false;
    auto* native = static_cast<DX12Texture*>(texture.get()); auto old = native->get_subresource_state(0);
    copy_texture_to_buffer(texture, {TEXTURE_ASPECT_COLOR, 0, 0, 1}, buffer, 0);
    native->transition(backend->get_command_list(), old, 0, 1, 0, 1);
    backend->submit_and_wait();
    auto result = buffer->snapshot_data(); if (result.size() < bytes) return false;
    memcpy(data, result.data(), size_t(bytes)); return true;
}

DX12CommandContextImmediate::DX12CommandContextImmediate(DX12Backend& backend) : backend_(backend) {}
DX12CommandContext* DX12CommandContextImmediate::ensure_context() {
    if (!cmd_context_) cmd_context_ = std::make_shared<DX12CommandContext>(nullptr, backend_.shared_from_this());
    backend_.ensure_recording(); return cmd_context_.get();
}
void DX12CommandContextImmediate::flush() { backend_.submit_and_wait(); }
void DX12CommandContextImmediate::texture_barrier(const RHITextureBarrier& b) { ensure_context()->texture_barrier(b); }
void DX12CommandContextImmediate::buffer_barrier(const RHIBufferBarrier& b) { ensure_context()->buffer_barrier(b); }
void DX12CommandContextImmediate::copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers layers, RHIBufferRef dst, uint64_t offset) { ensure_context()->copy_texture_to_buffer(src, layers, dst, offset); }
void DX12CommandContextImmediate::copy_buffer_to_texture(RHIBufferRef src, uint64_t offset, RHITextureRef dst, TextureSubresourceLayers layers) { ensure_context()->copy_buffer_to_texture(src, offset, dst, layers); }
void DX12CommandContextImmediate::copy_buffer(RHIBufferRef src, uint64_t soff, RHIBufferRef dst, uint64_t doff, uint64_t size) { ensure_context()->copy_buffer(src, soff, dst, doff, size); }
void DX12CommandContextImmediate::copy_texture(RHITextureRef src, TextureSubresourceLayers sl, RHITextureRef dst, TextureSubresourceLayers dl) { ensure_context()->copy_texture(src, sl, dst, dl); }
void DX12CommandContextImmediate::generate_mips(RHITextureRef texture) { ensure_context()->generate_mips(texture); }
RHICommandContextImmediateRef DX12Backend::get_immediate_command() {
    if (!immediate_command_) immediate_command_ = std::make_shared<DX12CommandContextImmediate>(*this);
    return immediate_command_;
}

namespace {
void execute_indirect(DX12Backend& backend, RHIBufferRef buffer, uint32_t offset, uint32_t count, D3D12_INDIRECT_ARGUMENT_TYPE type, uint32_t stride) {
    if (!buffer || uint64_t(offset) + uint64_t(count) * stride > buffer->get_info().size) return;
    D3D12_INDIRECT_ARGUMENT_DESC argument = {}; argument.Type = type;
    D3D12_COMMAND_SIGNATURE_DESC desc = {}; desc.ByteStride = stride; desc.NumArgumentDescs = 1; desc.pArgumentDescs = &argument;
    ComPtr<ID3D12CommandSignature> signature;
    if (FAILED(backend.get_device()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&signature)))) return;
    auto* native = static_cast<DX12Buffer*>(buffer.get());
    native->transition(backend.get_command_list(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    backend.retain(buffer);
    backend.get_command_list()->ExecuteIndirect(signature.Get(), count, native->get_resource(), offset, nullptr, 0);
    // Keep the signature alive without interrupting the active render pass.
    backend.on_complete([signature]() {});
}
}
void DX12CommandContext::draw_indirect(RHIBufferRef buffer, uint32_t offset, uint32_t count) {
    if (!current_graphics_pipeline_) return; if (!flush_graphics_bindings()) return;
    execute_indirect(*backend_.lock(), buffer, offset, count, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, sizeof(RHIIndirectCommand));
}
void DX12CommandContext::draw_indexed_indirect(RHIBufferRef buffer, uint32_t offset, uint32_t count) {
    if (!current_graphics_pipeline_) return; if (!flush_graphics_bindings()) return;
    execute_indirect(*backend_.lock(), buffer, offset, count, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, sizeof(RHIIndexedIndirectCommand));
}
void DX12CommandContext::dispatch_indirect(RHIBufferRef buffer, uint32_t offset) {
    if (!current_compute_pipeline_) return; if (!flush_compute_bindings()) return;
    execute_indirect(*backend_.lock(), buffer, offset, 1, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH, 12);
}
