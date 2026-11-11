#include "platform_rhi.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <algorithm>

DEFINE_LOG_TAG(LogDX12Support, "DX12Support");

uint32_t DX12Backend::allocate_imgui_srv_index() {
    if (!imgui_free_srv_indices_.empty()) { auto index = imgui_free_srv_indices_.back(); imgui_free_srv_indices_.pop_back(); return index; }
    if (imgui_next_srv_index_ >= GPU_SRV_UAV_RESERVED_COUNT) { ERR(LogDX12Support, "Persistent descriptor heap exhausted"); return UINT32_MAX; }
    return imgui_next_srv_index_++;
}
void DX12Backend::free_imgui_srv_index(uint32_t index) { if (index != UINT32_MAX) imgui_free_srv_indices_.push_back(index); }
uint32_t DX12Backend::sampler_table(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (auto iter = sampler_tables_.find(handle.ptr); iter != sampler_tables_.end()) return iter->second;
    auto index = gpu_sampler_heap_.allocate();
    if (index != UINT32_MAX) {
        device_->CopyDescriptorsSimple(1, gpu_sampler_heap_.get_cpu_handle(index), handle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        sampler_tables_[handle.ptr] = index;
    }
    return index;
}
void DX12Backend::create_default_descriptors() {
    auto allocate = [&]() { return cpu_srv_uav_allocator_.get_cpu_handle(cpu_srv_uav_allocator_.allocate()); };
    null_srv_2d_ = allocate(); null_srv_cube_ = allocate(); null_uav_2d_ = allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(nullptr, &srv, null_srv_2d_);
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; srv.TextureCube.MipLevels = 1;
    device_->CreateShaderResourceView(nullptr, &srv, null_srv_cube_);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {}; uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(nullptr, nullptr, &uav, null_uav_2d_);
    default_sampler_handle_ = cpu_sampler_allocator_.get_cpu_handle(cpu_sampler_allocator_.allocate());
    D3D12_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    device_->CreateSampler(&sampler, default_sampler_handle_);
}
void DX12Backend::init_imgui(void* window) {
    if (imgui_initialized_ || !device_) return;
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX12_InitInfo info;
    info.Device = device_.Get(); info.CommandQueue = command_queue_.Get(); info.NumFramesInFlight = 2;
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    info.SrvDescriptorHeap = gpu_srv_uav_heap_.get_heap(); info.UserData = this;
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* backend = static_cast<DX12Backend*>(info->UserData);
        auto index = backend->allocate_imgui_srv_index();
        *cpu = backend->get_gpu_srv_uav_heap().get_cpu_handle(index); *gpu = backend->get_gpu_srv_uav_heap().get_gpu_handle(index);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto* backend = static_cast<DX12Backend*>(info->UserData);
        auto& heap = backend->get_gpu_srv_uav_heap();
        auto index = uint32_t((cpu.ptr - heap.get_cpu_handle(0).ptr) / heap.get_descriptor_size());
        backend->free_imgui_srv_index(index);
    };
    imgui_initialized_ = ImGui_ImplDX12_Init(&info);
}
void DX12Backend::imgui_new_frame() {
    if (!imgui_initialized_) return;
    ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
}
void DX12Backend::imgui_render() {
    if (!imgui_initialized_) return;
    ImGui::Render();
    if (!active_swapchain_) return;
    ensure_recording();
    auto texture = active_swapchain_->get_texture(active_swapchain_->get_current_frame_index());
    auto* native = static_cast<DX12Texture*>(texture.get());
    native->transition(command_list_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = native->get_rtv(); command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    // ImGui image views share the persistent part of the same shader-visible heap.
    prepare_imgui_resources();
    native->transition(command_list_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[] = {gpu_srv_uav_heap_.get_heap(), gpu_sampler_heap_.get_heap()};
    command_list_->SetDescriptorHeaps(2, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list_.Get());
}
void DX12Backend::imgui_shutdown() {
    if (!imgui_initialized_) return;
    ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); imgui_initialized_ = false;
}
void DX12CommandContext::imgui_create_fonts_texture() { ImGui_ImplDX12_CreateDeviceObjects(); }
void DX12CommandContext::imgui_render_draw_data() {
    if (auto backend = backend_.lock()) {
        backend->ensure_recording();
        // ImGui does not use depth. Unbind the editor depth attachment before
        // sampling any depth visualization images.
        if (current_render_pass_) {
            D3D12_CPU_DESCRIPTOR_HANDLE targets[MAX_RENDER_TARGETS] = {};
            uint32_t count = 0;
            const auto& attachments = current_render_pass_->get_info().color_attachments;
            for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i) if (attachments[i].texture_view) {
                targets[i] = static_cast<DX12TextureView*>(attachments[i].texture_view.get())->get_rtv(); count = i + 1;
            }
            backend->get_command_list()->OMSetRenderTargets(count, targets, FALSE, nullptr);
        }
        backend->prepare_imgui_resources();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), backend->get_command_list());
    }
}
void DX12Backend::prepare_imgui_resources() {
    for (auto& resource : resource_map_[RHI_TEXTURE_VIEW]) {
        auto* view = static_cast<DX12TextureView*>(resource.get());
        if (view->has_imgui_descriptor()) view->get_dx12_texture()->transition(command_list_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
GPUProfilerRef DX12Backend::create_gpu_profiler() { return std::make_unique<DX12GPUProfiler>(device_.Get(), command_queue_.Get(), command_list_.Get()); }

void DX12Backend::create_mipmap_pipelines() {
    D3D12_DESCRIPTOR_RANGE ranges[] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}};
    D3D12_ROOT_PARAMETER parameters[3] = {};
    for (uint32_t i = 0; i < 2; ++i) { parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; parameters[i].DescriptorTable = {1, &ranges[i]}; }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants = {0, 0, 1};
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    D3D12_ROOT_SIGNATURE_DESC desc = {}; desc.NumParameters = 3; desc.pParameters = parameters; desc.NumStaticSamplers = 1; desc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))) return;
    if (FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mipmap_root_sig_)))) return;
    const char* source = R"(
        Texture2DArray<float4> input_tex : register(t0);
        RWTexture2DArray<float4> output_tex : register(u0);
        SamplerState linear_sampler : register(s0);
        cbuffer MipParams : register(b0) { uint encode_srgb; };
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) {
            uint width, height, layers;
            output_tex.GetDimensions(width, height, layers);
            if (id.x >= width || id.y >= height || id.z >= layers) return;
            float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
            float4 color = input_tex.SampleLevel(linear_sampler, float3(uv, id.z), 0);
            if (encode_srgb) color.rgb = lerp(12.92 * color.rgb, 1.055 * pow(max(color.rgb, 0.0), 1.0/2.4) - 0.055, step(0.0031308, color.rgb));
            output_tex[id] = color;
        })";
    auto code = compile_shader(source, "main", "cs_5_0");
    if (code.empty()) return;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {}; pipeline.pRootSignature = mipmap_root_sig_.Get(); pipeline.CS = {code.data(), code.size()};
    if (FAILED(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&mipmap_pso_array_)))) return;
}
void DX12CommandContext::generate_mips(RHITextureRef texture) {
    auto backend = backend_.lock(); if (!backend || !texture || texture->get_info().mip_levels < 2) return;
    auto* native = static_cast<DX12Texture*>(texture.get());
    if (native->get_resource()->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        ERR(LogDX12Support, "Mipmap generation supports 2D and array textures only"); return;
    }
    if (!(texture->get_info().type & RESOURCE_TYPE_RW_TEXTURE) || !backend->get_mipmap_pso_array()) {
        ERR(LogDX12Support, "Mipmap generation requires a UAV-capable texture"); return;
    }
    backend->ensure_recording(); backend->retain(texture);
    auto* command = backend->get_command_list();
    command->SetComputeRootSignature(backend->get_mipmap_root_sig()); command->SetPipelineState(backend->get_mipmap_pso_array());
    command->SetComputeRoot32BitConstant(2, texture->get_info().format == FORMAT_R8G8B8A8_SRGB ? 1 : 0, 0);
    for (uint32_t mip = 1; mip < texture->get_info().mip_levels; ++mip) {
        native->transition(command, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, mip - 1, 1);
        native->transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mip, 1);
        auto srv = native->get_srv(VIEW_TYPE_2D_ARRAY, mip - 1, 1); auto uav = native->get_uav(mip, 0, 0, true);
        auto& heap = backend->get_gpu_srv_uav_heap(); auto index = heap.allocate(2); if (index == UINT32_MAX) return;
        backend->get_device()->CopyDescriptorsSimple(1, heap.get_cpu_handle(index), srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        backend->get_device()->CopyDescriptorsSimple(1, heap.get_cpu_handle(index + 1), uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        command->SetComputeRootDescriptorTable(0, heap.get_gpu_handle(index)); command->SetComputeRootDescriptorTable(1, heap.get_gpu_handle(index + 1));
        auto extent = texture->mip_extent(mip); command->Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, native->get_array_layers());
    }
    native->transition(command, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (current_compute_pipeline_) {
        command->SetPipelineState(current_compute_pipeline_->get_pso());
        command->SetComputeRootSignature(static_cast<DX12RootSignature*>(current_compute_pipeline_->get_info().root_signature.get())->get_signature());
    } else if (current_graphics_pipeline_) {
        command->SetPipelineState(current_graphics_pipeline_->get_pso());
    }
}
