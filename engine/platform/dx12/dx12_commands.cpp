#include "platform_rhi.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <algorithm>
#include <cstring>

DEFINE_LOG_TAG(LogDX12Commands, "DX12Commands");

namespace {
D3D12_BLEND blend_factor(BlendFactor factor) {
    constexpr D3D12_BLEND values[] = {D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR,
        D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
        D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_SRC_ALPHA_SAT, D3D12_BLEND_BLEND_FACTOR, D3D12_BLEND_INV_BLEND_FACTOR};
    return values[factor];
}
D3D12_COMPARISON_FUNC depth_compare(CompareFunction func) {
    constexpr D3D12_COMPARISON_FUNC values[] = {D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_COMPARISON_FUNC_NOT_EQUAL, D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_ALWAYS};
    return values[func];
}
bool is_uav(ResourceType type) { return type == RESOURCE_TYPE_RW_TEXTURE || type == RESOURCE_TYPE_RW_BUFFER || type == RESOURCE_TYPE_RW_TEXEL_BUFFER; }
}

DX12GraphicsPipeline::DX12GraphicsPipeline(const RHIGraphicsPipelineInfo& info, std::shared_ptr<DX12Backend> backend) : RHIGraphicsPipeline(info), backend_(backend) {}
DX12GraphicsPipeline::~DX12GraphicsPipeline() { destroy(); }
bool DX12GraphicsPipeline::init() {
    auto backend = backend_.lock();
    if (!backend || !info_.vertex_shader || !info_.root_signature) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = static_cast<DX12RootSignature*>(info_.root_signature.get())->get_signature();
    desc.VS = static_cast<DX12Shader*>(info_.vertex_shader.get())->get_bytecode();
    if (info_.fragment_shader) desc.PS = static_cast<DX12Shader*>(info_.fragment_shader.get())->get_bytecode();
    if (info_.geometry_shader) desc.GS = static_cast<DX12Shader*>(info_.geometry_shader.get())->get_bytecode();
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    for (const auto& element : info_.vertex_input_state.vertex_elements) {
        elements.push_back({element.semantic_name.c_str(), element.semantic_index, DX12Backend::to_dxgi_format(element.format),
            element.stream_index, element.offset, element.use_instance_index ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            element.use_instance_index ? 1u : 0u});
        if (vertex_strides_.size() <= element.stream_index) vertex_strides_.resize(element.stream_index + 1);
        vertex_strides_[element.stream_index] = std::max(vertex_strides_[element.stream_index],
            element.stride ? element.stride : element.offset + DX12Backend::get_format_byte_size(element.format));
    }
    desc.InputLayout = {elements.data(), UINT(elements.size())};
    desc.RasterizerState.FillMode = info_.rasterizer_state.fill_mode == FILL_MODE_WIREFRAME ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = info_.rasterizer_state.cull_mode == CULL_MODE_NONE ? D3D12_CULL_MODE_NONE :
        info_.rasterizer_state.cull_mode == CULL_MODE_FRONT ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK;
    desc.RasterizerState.DepthClipEnable = info_.rasterizer_state.depth_clip_mode == DEPTH_CLIP;
    desc.RasterizerState.DepthBias = INT(info_.rasterizer_state.depth_bias);
    desc.RasterizerState.SlopeScaledDepthBias = info_.rasterizer_state.slope_scale_depth_bias;
    desc.BlendState.IndependentBlendEnable = TRUE;
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i) {
        auto& target = desc.BlendState.RenderTarget[i];
        const auto& blend = info_.blend_state.render_targets[i];
        target.BlendEnable = blend.enable;
        target.SrcBlend = blend_factor(blend.color_src_blend); target.DestBlend = blend_factor(blend.color_dst_blend);
        target.BlendOp = D3D12_BLEND_OP(UINT(blend.color_blend_op) + 1);
        target.SrcBlendAlpha = blend_factor(blend.alpha_src_blend); target.DestBlendAlpha = blend_factor(blend.alpha_dst_blend);
        target.BlendOpAlpha = D3D12_BLEND_OP(UINT(blend.alpha_blend_op) + 1);
        target.RenderTargetWriteMask = UINT8(blend.color_write_mask);
        if (info_.color_attachment_formats[i] != FORMAT_UKNOWN) {
            desc.NumRenderTargets = i + 1; desc.RTVFormats[i] = DX12Backend::to_dxgi_format(info_.color_attachment_formats[i]);
        }
    }
    desc.DSVFormat = DX12Backend::to_dxgi_dsv_format(info_.depth_stencil_attachment_format);
    desc.DepthStencilState.DepthEnable = info_.depth_stencil_state.enable_depth_test && desc.DSVFormat != DXGI_FORMAT_UNKNOWN;
    desc.DepthStencilState.DepthWriteMask = info_.depth_stencil_state.enable_depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = depth_compare(info_.depth_stencil_state.depth_test);
    desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace = desc.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    topology_ = info_.primitive_type == PRIMITIVE_TYPE_TRIANGLE_STRIP ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP :
        info_.primitive_type == PRIMITIVE_TYPE_LINE_LIST ? D3D_PRIMITIVE_TOPOLOGY_LINELIST :
        info_.primitive_type == PRIMITIVE_TYPE_POINT_LIST ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    desc.PrimitiveTopologyType = info_.primitive_type == PRIMITIVE_TYPE_LINE_LIST ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE :
        info_.primitive_type == PRIMITIVE_TYPE_POINT_LIST ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
    HRESULT hr = backend->get_device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) ERR(LogDX12Commands, "Create graphics PSO failed 0x{:08X}", uint32_t(hr));
    return SUCCEEDED(hr);
}
void DX12GraphicsPipeline::destroy() { pso_.Reset(); }
DX12ComputePipeline::DX12ComputePipeline(const RHIComputePipelineInfo& info, std::shared_ptr<DX12Backend> backend) : RHIComputePipeline(info), backend_(backend) {}
DX12ComputePipeline::~DX12ComputePipeline() { destroy(); }
bool DX12ComputePipeline::init() {
    auto backend = backend_.lock(); if (!backend || !info_.compute_shader || !info_.root_signature) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = static_cast<DX12RootSignature*>(info_.root_signature.get())->get_signature();
    desc.CS = static_cast<DX12Shader*>(info_.compute_shader.get())->get_bytecode();
    HRESULT hr = backend->get_device()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) ERR(LogDX12Commands, "Create compute PSO failed 0x{:08X}", uint32_t(hr));
    return SUCCEEDED(hr);
}
void DX12ComputePipeline::destroy() { pso_.Reset(); }

DX12CommandContext::DX12CommandContext(RHICommandPoolRef pool, std::shared_ptr<DX12Backend> backend) : RHICommandContext(pool), backend_(backend) {}
void DX12CommandContext::begin_command() {
    if (auto backend = backend_.lock()) backend->ensure_recording();
    current_graphics_pipeline_ = nullptr; current_compute_pipeline_ = nullptr; bound_sets_ = {}; vertex_streams_ = {};
}
void DX12CommandContext::end_command() {}
void DX12CommandContext::execute(RHIFenceRef fence, RHISemaphoreRef, RHISemaphoreRef) {
    if (auto backend = backend_.lock()) {
        backend->submit_and_wait();
        if (fence) static_cast<DX12Fence*>(fence.get())->signal(backend->get_command_queue());
    }
}
void DX12CommandContext::texture_barrier(const RHITextureBarrier& barrier) {
    auto backend = backend_.lock(); if (!backend || !barrier.texture) return;
    backend->ensure_recording(); backend->retain(barrier.texture);
    auto range = barrier.subresource;
    static_cast<DX12Texture*>(barrier.texture.get())->transition(backend->get_command_list(), DX12Backend::to_d3d12_resource_state(barrier.dst_state),
        range.base_mip_level, range.level_count, range.base_array_layer, range.layer_count);
}
void DX12CommandContext::buffer_barrier(const RHIBufferBarrier& barrier) {
    auto backend = backend_.lock(); if (!backend || !barrier.buffer) return;
    backend->ensure_recording(); backend->retain(barrier.buffer);
    static_cast<DX12Buffer*>(barrier.buffer.get())->transition(backend->get_command_list(), DX12Backend::to_d3d12_resource_state(barrier.dst_state));
}
void DX12CommandContext::begin_render_pass(RHIRenderPassRef pass) {
    auto backend = backend_.lock(); if (!backend || !pass) return;
    backend->ensure_recording(); backend->retain(pass);
    current_render_pass_ = static_cast<DX12RenderPass*>(pass.get());
    current_depth_texture_ = nullptr; current_depth_read_only_ = false;
    const auto& info = pass->get_info();
    auto* command = backend->get_command_list();
    D3D12_CPU_DESCRIPTOR_HANDLE targets[MAX_RENDER_TARGETS] = {};
    uint32_t count = 0;
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i) {
        const auto& attachment = info.color_attachments[i];
        if (!attachment.texture_view) continue;
        auto* view = static_cast<DX12TextureView*>(attachment.texture_view.get());
        auto range = view->get_info().subresource;
        view->get_dx12_texture()->transition(command, D3D12_RESOURCE_STATE_RENDER_TARGET, range.base_mip_level, 1, range.base_array_layer, range.layer_count);
        targets[i] = view->get_rtv(); count = i + 1;
        if (attachment.load_op == ATTACHMENT_LOAD_OP_CLEAR) command->ClearRenderTargetView(targets[i], &attachment.clear_color.r, 0, nullptr);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE depth = {};
    const auto& attachment = info.depth_stencil_attachment;
    if (attachment.texture_view) {
        auto* view = static_cast<DX12TextureView*>(attachment.texture_view.get());
        auto range = view->get_info().subresource;
        current_depth_texture_ = view->get_dx12_texture(); current_depth_read_only_ = attachment.read_only;
        auto state = attachment.read_only ? D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_DEPTH_WRITE;
        current_depth_texture_->transition(command, state, range.base_mip_level, 1, range.base_array_layer, range.layer_count);
        depth = view->get_dsv(attachment.read_only);
        if (attachment.load_op == ATTACHMENT_LOAD_OP_CLEAR && !attachment.read_only) {
            auto flags = is_stencil_format(view->get_info().format) ? D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL : D3D12_CLEAR_FLAG_DEPTH;
            command->ClearDepthStencilView(depth, flags, attachment.clear_depth, UINT8(attachment.clear_stencil), 0, nullptr);
        }
    }
    command->OMSetRenderTargets(count, targets, FALSE, depth.ptr ? &depth : nullptr);
    set_viewport({0, 0}, {info.extent.width, info.extent.height}); set_scissor({0, 0}, {info.extent.width, info.extent.height});
}
void DX12CommandContext::end_render_pass() {
    if (auto backend = backend_.lock()) backend->get_command_list()->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    current_render_pass_ = nullptr; current_depth_texture_ = nullptr; current_depth_read_only_ = false;
}
void DX12CommandContext::set_viewport(Offset2D min, Offset2D max) {
    auto backend = backend_.lock(); if (!backend) return;
    D3D12_VIEWPORT viewport = {float(min.x), float(min.y), float(max.x - min.x), float(max.y - min.y), 0, 1};
    backend->get_command_list()->RSSetViewports(1, &viewport);
}
void DX12CommandContext::set_scissor(Offset2D min, Offset2D max) {
    auto backend = backend_.lock(); if (!backend) return;
    D3D12_RECT rect = {LONG(min.x), LONG(min.y), LONG(max.x), LONG(max.y)}; backend->get_command_list()->RSSetScissorRects(1, &rect);
}
void DX12CommandContext::set_depth_bias(float constant, float slope, float clamp) {
    if (constant != 0 || slope != 0 || clamp != 0) WARN(LogDX12Commands, "Depth bias must be set in the DX12 pipeline rasterizer state");
}
void DX12CommandContext::set_line_width(float width) { if (width != 1) WARN(LogDX12Commands, "DX12 supports line width 1 only"); }
void DX12CommandContext::set_graphics_pipeline(RHIGraphicsPipelineRef pipeline) {
    auto backend = backend_.lock(); if (!backend || !pipeline) return;
    backend->ensure_recording(); backend->retain(pipeline);
    current_graphics_pipeline_ = static_cast<DX12GraphicsPipeline*>(pipeline.get()); current_compute_pipeline_ = nullptr;
    auto* command = backend->get_command_list();
    command->SetPipelineState(current_graphics_pipeline_->get_pso());
    command->SetGraphicsRootSignature(static_cast<DX12RootSignature*>(pipeline->get_info().root_signature.get())->get_signature());
    command->IASetPrimitiveTopology(current_graphics_pipeline_->get_topology());
}
void DX12CommandContext::set_compute_pipeline(RHIComputePipelineRef pipeline) {
    auto backend = backend_.lock(); if (!backend || !pipeline) return;
    backend->ensure_recording(); backend->retain(pipeline);
    current_compute_pipeline_ = static_cast<DX12ComputePipeline*>(pipeline.get()); current_graphics_pipeline_ = nullptr;
    backend->get_command_list()->SetPipelineState(current_compute_pipeline_->get_pso());
    backend->get_command_list()->SetComputeRootSignature(static_cast<DX12RootSignature*>(pipeline->get_info().root_signature.get())->get_signature());
}
void DX12CommandContext::set_ray_tracing_pipeline(RHIRayTracingPipelineRef) { ERR(LogDX12Commands, "DXR is unsupported"); }
void DX12CommandContext::push_constants(void* data, uint16_t size, ShaderFrequency frequency) {
    auto backend = backend_.lock();
    if (!backend || !data || !size || size % 4 || (!current_graphics_pipeline_ && !current_compute_pipeline_)) return;
    auto root = current_compute_pipeline_ ? current_compute_pipeline_->get_info().root_signature : current_graphics_pipeline_->get_info().root_signature;
    const auto& constants = root->get_info().get_push_constants();
    for (uint32_t i = 0; i < constants.size(); ++i) {
        if ((constants[i].frequency & frequency) && size <= constants[i].size) {
            uint32_t parameter = uint32_t(root->get_info().get_entries().size()) + i;
            if (current_compute_pipeline_) backend->get_command_list()->SetComputeRoot32BitConstants(parameter, size / 4, data, 0);
            else backend->get_command_list()->SetGraphicsRoot32BitConstants(parameter, size / 4, data, 0);
            return;
        }
    }
    ERR(LogDX12Commands, "Push constants do not match the pipeline root layout");
}
void DX12CommandContext::bind_descriptor_set(RHIDescriptorSetRef set, uint32_t index) {
    if (!set || index >= MAX_DESCRIPTOR_SETS || index != set->get_set()) { ERR(LogDX12Commands, "Invalid descriptor set index"); return; }
    bound_sets_[index] = set->snapshot();
}
bool DX12CommandContext::flush_bindings(bool compute) {
    auto backend = backend_.lock(); if (!backend) return false;
    auto root = compute ? current_compute_pipeline_->get_info().root_signature : current_graphics_pipeline_->get_info().root_signature;
    auto* device = backend->get_device(); auto* command = backend->get_command_list();
    const auto& entries = root->get_info().get_entries();
    for (uint32_t parameter = 0; parameter < entries.size(); ++parameter) {
        const auto& entry = entries[parameter];
        const auto& set = bound_sets_[entry.set];
        if (set && set->get_root_signature()->get_info().get_entries() != entries) { ERR(LogDX12Commands, "Descriptor layout does not match pipeline"); return false; }
        bool sampler = entry.type == RESOURCE_TYPE_SAMPLER;
        auto& heap = sampler ? backend->get_gpu_sampler_heap() : backend->get_gpu_srv_uav_heap();
        D3D12_CPU_DESCRIPTOR_HANDLE sampler_source = backend->get_default_sampler();
        if (sampler && entry.size == 1 && set) for (const auto& descriptor : set->get_descriptors())
            if (descriptor.binding == entry.binding && descriptor.resource_type == RESOURCE_TYPE_SAMPLER && descriptor.sampler)
                sampler_source = static_cast<DX12Sampler*>(descriptor.sampler.get())->get_handle();
        uint32_t start = sampler && entry.size == 1 ? backend->sampler_table(sampler_source) : heap.allocate(entry.size);
        if (start == UINT32_MAX) return false;
        for (uint32_t i = 0; i < entry.size; ++i) {
            const RHIDescriptorUpdateInfo* descriptor = nullptr;
            if (set) for (const auto& candidate : set->get_descriptors())
                if (candidate.binding == entry.binding && candidate.index == i &&
                    (candidate.resource_type == entry.type || (entry.type == RESOURCE_TYPE_TEXTURE_CUBE && candidate.resource_type == RESOURCE_TYPE_TEXTURE))) { descriptor = &candidate; break; }
            auto destination = heap.get_cpu_handle(start + i);
            D3D12_CPU_DESCRIPTOR_HANDLE source = {};
            if (sampler) source = descriptor && descriptor->sampler ? static_cast<DX12Sampler*>(descriptor->sampler.get())->get_handle() : backend->get_default_sampler();
            else if (entry.type == RESOURCE_TYPE_UNIFORM_BUFFER) {
                if (descriptor && descriptor->buffer) {
                    auto bytes = descriptor->constant_data;
                    if (bytes.empty()) bytes = descriptor->buffer->snapshot_data();
                    uint64_t offset = descriptor->buffer_offset;
                    uint64_t size = descriptor->buffer_range ? descriptor->buffer_range : descriptor->buffer->get_info().size - offset;
                    D3D12_GPU_VIRTUAL_ADDRESS address = 0; void* cpu = nullptr;
                    uint64_t aligned_size = (size + 255) & ~255ull;
                    if (bytes.empty()) {
                        if (offset % 256 || !size || size > 65536) { ERR(LogDX12Commands, "Invalid GPU constant buffer range"); return false; }
                        auto* buffer = static_cast<DX12Buffer*>(descriptor->buffer.get());
                        buffer->transition(command, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {buffer->get_current_gpu_address() + offset, UINT(aligned_size)};
                        device->CreateConstantBufferView(&cbv, destination);
                        continue;
                    }
                    if (size > 65536 || offset > bytes.size() || size > bytes.size() - offset ||
                        !backend->get_dynamic_ring_buffer().allocate(aligned_size, 256, &cpu, &address)) {
                        ERR(LogDX12Commands, "Constant buffer cannot be captured"); return false;
                    }
                    memset(cpu, 0, size_t(aligned_size)); memcpy(cpu, bytes.data() + offset, size_t(size));
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {address, UINT(aligned_size)};
                    device->CreateConstantBufferView(&cbv, destination);
                } else device->CreateConstantBufferView(nullptr, destination);
            } else if (descriptor && descriptor->texture_view) {
                auto* view = static_cast<DX12TextureView*>(descriptor->texture_view.get());
                auto* texture = view->get_dx12_texture(); auto range = view->get_info().subresource;
                auto state = is_uav(entry.type) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                if (texture == current_depth_texture_ && current_depth_read_only_) state |= D3D12_RESOURCE_STATE_DEPTH_READ;
                texture->transition(command, state, range.base_mip_level, range.level_count, range.base_array_layer, range.layer_count);
                source = is_uav(entry.type) ? view->get_uav() : view->get_srv();
            } else if (descriptor && descriptor->buffer) {
                auto* buffer = static_cast<DX12Buffer*>(descriptor->buffer.get());
                buffer->transition(command, is_uav(entry.type) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                auto info = buffer->get_info();
                uint32_t stride = info.stride ? info.stride : 4;
                uint64_t size = descriptor->buffer_range ? descriptor->buffer_range : info.size - descriptor->buffer_offset;
                if (is_uav(entry.type)) {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
                    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    view.Format = info.stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                    view.Buffer.FirstElement = descriptor->buffer_offset / stride; view.Buffer.NumElements = UINT(size / stride);
                    view.Buffer.StructureByteStride = info.stride;
                    view.Buffer.Flags = info.stride ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
                    device->CreateUnorderedAccessView(buffer->get_resource(), nullptr, &view, destination);
                } else {
                    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
                    view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    view.Format = info.stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
                    view.Buffer.FirstElement = descriptor->buffer_offset / stride; view.Buffer.NumElements = UINT(size / stride);
                    view.Buffer.StructureByteStride = info.stride;
                    view.Buffer.Flags = info.stride ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;
                    device->CreateShaderResourceView(buffer->get_resource(), &view, destination);
                }
            } else if (is_uav(entry.type)) source = backend->get_null_uav_2d();
            else source = entry.type == RESOURCE_TYPE_TEXTURE_CUBE ? backend->get_null_srv_cube() : backend->get_null_srv_2d();
            if (source.ptr) device->CopyDescriptorsSimple(1, destination, source,
                sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (compute) command->SetComputeRootDescriptorTable(parameter, heap.get_gpu_handle(start));
        else command->SetGraphicsRootDescriptorTable(parameter, heap.get_gpu_handle(start));
        if (set) backend->retain(set);
    }
    return true;
}
bool DX12CommandContext::flush_graphics_bindings() {
    if (!flush_bindings(false)) return false;
    auto backend = backend_.lock();
    const auto& strides = current_graphics_pipeline_->get_vertex_strides();
    for (uint32_t i = 0; i < vertex_streams_.size(); ++i) {
        auto& binding = vertex_streams_[i]; if (!binding.active) continue;
        auto* buffer = binding.buffer;
        buffer->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        D3D12_VERTEX_BUFFER_VIEW view = {buffer->get_current_gpu_address() + binding.offset, UINT(buffer->get_info().size - binding.offset),
            buffer->get_info().stride ? buffer->get_info().stride : (i < strides.size() ? strides[i] : 0)};
        backend->get_command_list()->IASetVertexBuffers(i, 1, &view);
    }
    return true;
}
bool DX12CommandContext::flush_compute_bindings() { return flush_bindings(true); }
void DX12CommandContext::bind_vertex_buffer(RHIBufferRef buffer, uint32_t stream, uint32_t offset) {
    auto backend = backend_.lock(); if (!backend || !buffer || stream >= vertex_streams_.size()) return;
    backend->retain(buffer); vertex_streams_[stream] = {static_cast<DX12Buffer*>(buffer.get()), offset, true};
}
void DX12CommandContext::bind_index_buffer(RHIBufferRef buffer, uint32_t offset) {
    auto backend = backend_.lock(); if (!backend || !buffer) return;
    backend->retain(buffer); auto* native = static_cast<DX12Buffer*>(buffer.get());
    native->transition(backend->get_command_list(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
    D3D12_INDEX_BUFFER_VIEW view = {native->get_current_gpu_address() + offset, UINT(buffer->get_info().size - offset),
        buffer->get_info().stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
    backend->get_command_list()->IASetIndexBuffer(&view);
}
void DX12CommandContext::draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) {
    if (!current_graphics_pipeline_) return;
    if (!flush_graphics_bindings()) return;
    backend_.lock()->get_command_list()->DrawInstanced(vertices, instances, first_vertex, first_instance);
}
void DX12CommandContext::draw_indexed(uint32_t indices, uint32_t instances, uint32_t first_index, uint32_t offset, uint32_t first_instance) {
    if (!current_graphics_pipeline_) return;
    if (!flush_graphics_bindings()) return;
    backend_.lock()->get_command_list()->DrawIndexedInstanced(indices, instances, first_index, INT(offset), first_instance);
}
void DX12CommandContext::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    if (!current_compute_pipeline_) return;
    if (!flush_compute_bindings()) return;
    backend_.lock()->get_command_list()->Dispatch(x, y, z);
}
void DX12CommandContext::trace_rays(uint32_t, uint32_t, uint32_t) { ERR(LogDX12Commands, "DXR is unsupported"); }
void DX12CommandContext::push_event(const std::string& name, Color3) {
    auto backend = backend_.lock(); if (!backend) return;
    backend->get_command_list()->BeginEvent(0, name.data(), UINT(name.size()));
}
void DX12CommandContext::pop_event() { if (auto backend = backend_.lock()) backend->get_command_list()->EndEvent(); }

DX12Fence::DX12Fence(bool signaled, std::shared_ptr<DX12Backend> backend) : signaled_(signaled), backend_(backend) {}
DX12Fence::~DX12Fence() { destroy(); }
bool DX12Fence::init() {
    auto backend = backend_.lock(); if (!backend) return false;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return event_ && SUCCEEDED(backend->get_device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
}
void DX12Fence::signal(ID3D12CommandQueue* queue) { queue->Signal(fence_.Get(), ++current_value_); signaled_ = true; }
void DX12Fence::wait() {
    if (!fence_ || !current_value_ || fence_->GetCompletedValue() >= current_value_) return;
    if (SUCCEEDED(fence_->SetEventOnCompletion(current_value_, event_))) WaitForSingleObject(event_, INFINITE);
}
void DX12Fence::destroy() { wait(); fence_.Reset(); if (event_) CloseHandle(event_); event_ = nullptr; }
RHIFenceRef DX12Backend::create_fence(bool signaled) {
    auto fence = std::make_shared<DX12Fence>(signaled, shared_from_this()); if (!fence->init()) return nullptr; register_resource(fence); return fence;
}
RHISemaphoreRef DX12Backend::create_semaphore() { return std::make_shared<DX12Semaphore>(*this); }
