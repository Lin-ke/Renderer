#include "platform_rhi.h"
#include "engine/core/log/Log.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

DEFINE_LOG_TAG(LogRHI, "DX11RHI");

class DX11Util {
public:
    static DXGI_FORMAT rhi_format_to_dxgi(RHIFormat format) {
        switch (format) {
            case FORMAT_R8_SRGB: return DXGI_FORMAT_R8_UNORM; 
            case FORMAT_R8G8_SRGB: return DXGI_FORMAT_R8G8_UNORM;
            case FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case FORMAT_R32G32B32A32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case FORMAT_R32G32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
            case FORMAT_R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
            case FORMAT_D32_SFLOAT: return DXGI_FORMAT_D32_FLOAT;
            case FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
            case FORMAT_R16_UINT: return DXGI_FORMAT_R16_UINT;
            case FORMAT_R8_UINT: return DXGI_FORMAT_R8_UINT;
            case FORMAT_R32G32B32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
            default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    static D3D11_USAGE memory_usage_to_dx11(MemoryUsage usage) {
        switch (usage) {
            case MEMORY_USAGE_GPU_ONLY: return D3D11_USAGE_DEFAULT;
            case MEMORY_USAGE_CPU_ONLY: return D3D11_USAGE_STAGING;
            case MEMORY_USAGE_CPU_TO_GPU: return D3D11_USAGE_DYNAMIC;
            case MEMORY_USAGE_GPU_TO_CPU: return D3D11_USAGE_STAGING;
            default: return D3D11_USAGE_DEFAULT;
        }
    }

    static UINT resource_type_to_bind_flags(ResourceType type) {
        UINT flags = 0;
        if (type & RESOURCE_TYPE_VERTEX_BUFFER) flags |= D3D11_BIND_VERTEX_BUFFER;
        if (type & RESOURCE_TYPE_INDEX_BUFFER) flags |= D3D11_BIND_INDEX_BUFFER;
        if (type & RESOURCE_TYPE_UNIFORM_BUFFER) flags |= D3D11_BIND_CONSTANT_BUFFER;
        if (type & RESOURCE_TYPE_TEXTURE) flags |= D3D11_BIND_SHADER_RESOURCE;
        if (type & RESOURCE_TYPE_RENDER_TARGET) flags |= D3D11_BIND_RENDER_TARGET;
        if (type & RESOURCE_TYPE_RW_TEXTURE) flags |= D3D11_BIND_UNORDERED_ACCESS;
        if (type & RESOURCE_TYPE_RW_BUFFER) flags |= D3D11_BIND_UNORDERED_ACCESS;
        return flags;
    }

    static D3D11_FILL_MODE fill_mode_to_dx11(RasterizerFillMode mode) {
        switch (mode) {
            case FILL_MODE_WIREFRAME: return D3D11_FILL_WIREFRAME;
            case FILL_MODE_SOLID: return D3D11_FILL_SOLID;
            default: return D3D11_FILL_SOLID;
        }
    }

    static D3D11_CULL_MODE cull_mode_to_dx11(RasterizerCullMode mode) {
        switch (mode) {
            case CULL_MODE_NONE: return D3D11_CULL_NONE;
            case CULL_MODE_FRONT: return D3D11_CULL_FRONT;
            case CULL_MODE_BACK: return D3D11_CULL_BACK;
            default: return D3D11_CULL_BACK;
        }
    }

    static D3D11_COMPARISON_FUNC compare_func_to_dx11(CompareFunction func) {
        switch (func) {
            case COMPARE_FUNCTION_LESS: return D3D11_COMPARISON_LESS;
            case COMPARE_FUNCTION_LESS_EQUAL: return D3D11_COMPARISON_LESS_EQUAL;
            case COMPARE_FUNCTION_GREATER: return D3D11_COMPARISON_GREATER;
            case COMPARE_FUNCTION_GREATER_EQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
            case COMPARE_FUNCTION_EQUAL: return D3D11_COMPARISON_EQUAL;
            case COMPARE_FUNCTION_NOT_EQUAL: return D3D11_COMPARISON_NOT_EQUAL;
            case COMPARE_FUNCTION_NEVER: return D3D11_COMPARISON_NEVER;
            case COMPARE_FUNCTION_ALWAYS: return D3D11_COMPARISON_ALWAYS;
            default: return D3D11_COMPARISON_LESS_EQUAL;
        }
    }
};

// --- DX11Buffer ---
DX11Buffer::DX11Buffer(const RHIBufferInfo& info, DX11Backend& backend) 
    : RHIBuffer(info), backend_(backend) {
}

bool DX11Buffer::init() {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (UINT)info_.size;
    desc.Usage = DX11Util::memory_usage_to_dx11(info_.memory_usage);
    desc.BindFlags = DX11Util::resource_type_to_bind_flags(info_.type);
    desc.CPUAccessFlags = (info_.memory_usage == MEMORY_USAGE_CPU_TO_GPU) ? D3D11_CPU_ACCESS_WRITE : 0;
    if (info_.memory_usage == MEMORY_USAGE_CPU_ONLY || info_.memory_usage == MEMORY_USAGE_GPU_TO_CPU) {
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    }

    HRESULT hr = backend_.get_device()->CreateBuffer(&desc, nullptr, buffer_.GetAddressOf());
    if (FAILED(hr)) {
        ERR(LogRHI, "Failed to create DX11 Buffer (HRESULT: 0x{:08X})", (uint32_t)hr);
        return false;
    }
    return true;
}

void* DX11Buffer::map() {
    if (!buffer_) return nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped_res;
    D3D11_MAP map_type = D3D11_MAP_WRITE_DISCARD;
    
    D3D11_BUFFER_DESC desc;
    buffer_->GetDesc(&desc);
    if (desc.Usage == D3D11_USAGE_STAGING) {
        map_type = D3D11_MAP_WRITE;
    } else if (desc.Usage != D3D11_USAGE_DYNAMIC) {
        // Can't map non-dynamic/non-staging buffer with WRITE_DISCARD
        return nullptr;
    }

    HRESULT hr = backend_.get_context()->Map(buffer_.Get(), 0, map_type, 0, &mapped_res);
    if (SUCCEEDED(hr)) {
        mapped_data_ = mapped_res.pData;
        return mapped_data_;
    }
    return nullptr;
}

void DX11Buffer::unmap() {
    backend_.get_context()->Unmap(buffer_.Get(), 0);
    mapped_data_ = nullptr;
}

void DX11Buffer::destroy() { buffer_.Reset(); }

// --- DX11Texture ---
DX11Texture::DX11Texture(const RHITextureInfo& info, DX11Backend& backend, ComPtr<ID3D11Texture2D> handle)
    : RHITexture(info), texture_(handle), backend_(backend) {
}

bool DX11Texture::init() {
    if (!texture_) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = info_.extent.width;
        desc.Height = info_.extent.height;
        desc.MipLevels = info_.mip_levels;
        desc.ArraySize = info_.array_layers;
        desc.Format = DX11Util::rhi_format_to_dxgi(info_.format);
        desc.SampleDesc.Count = 1;
        desc.Usage = DX11Util::memory_usage_to_dx11(info_.memory_usage);
        desc.BindFlags = DX11Util::resource_type_to_bind_flags(info_.type);
        desc.CPUAccessFlags = (info_.memory_usage == MEMORY_USAGE_CPU_TO_GPU) ? D3D11_CPU_ACCESS_WRITE : 0;

        HRESULT hr = backend_.get_device()->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr)) {
            ERR(LogRHI, "Failed to create DX11 Texture2D (HRESULT: 0x{:08X})", (uint32_t)hr);
            return false;
        }
    }
    return true;
}

void DX11Texture::destroy() { texture_.Reset(); }

// --- DX11TextureView ---
DX11TextureView::DX11TextureView(const RHITextureViewInfo& info, DX11Backend& backend)
    : RHITextureView(info) {
    auto dx_tex = resource_cast(info.texture);
    if (!dx_tex) return;

    if (info.texture->get_info().type & RESOURCE_TYPE_TEXTURE) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DX11Util::rhi_format_to_dxgi(info.format != FORMAT_UKNOWN ? info.format : info.texture->get_info().format);
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = info.texture->get_info().mip_levels;
        backend.get_device()->CreateShaderResourceView(dx_tex->get_handle().Get(), &srv_desc, srv_.GetAddressOf());
    }

    if (info.texture->get_info().type & RESOURCE_TYPE_RENDER_TARGET) {
        backend.get_device()->CreateRenderTargetView(dx_tex->get_handle().Get(), nullptr, rtv_.GetAddressOf());
    }
}

void DX11TextureView::destroy() {
    srv_.Reset(); rtv_.Reset(); dsv_.Reset(); uav_.Reset();
}

// --- DX11Swapchain ---
DX11Swapchain::DX11Swapchain(const RHISwapchainInfo& info, DX11Backend& backend)
    : RHISwapchain(info) {
    auto dx_surface = resource_cast(info.surface);
    if (!dx_surface) return;

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = info.image_count;
    desc.BufferDesc.Width = info.extent.width;
    desc.BufferDesc.Height = info.extent.height;
    desc.BufferDesc.Format = DX11Util::rhi_format_to_dxgi(info.format);
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = dx_surface->get_hwnd();
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain> temp_swap_chain;
    HRESULT hr = backend.get_factory()->CreateSwapChain(backend.get_device().Get(), &desc, temp_swap_chain.GetAddressOf());
    if (FAILED(hr)) {
        // Fallback to DISCARD if FLIP_DISCARD is not supported
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = backend.get_factory()->CreateSwapChain(backend.get_device().Get(), &desc, temp_swap_chain.GetAddressOf());
    }

    if (SUCCEEDED(hr)) {
        temp_swap_chain.As(&swap_chain_);
    }

    if (FAILED(hr) || !swap_chain_) {
        ERR(LogRHI, "Failed to create DX11 SwapChain. Format: {}, HRESULT: 0x{:08X}", (uint32_t)desc.BufferDesc.Format, (uint32_t)hr);
        return;
    }

    for (uint32_t i = 0; i < info.image_count; ++i) {
        ComPtr<ID3D11Texture2D> back_buffer;
        swap_chain_->GetBuffer(i, __uuidof(ID3D11Texture2D), (void**)back_buffer.GetAddressOf());
        RHITextureInfo tex_info = {};
        tex_info.format = info.format;
        tex_info.extent = { info.extent.width, info.extent.height, 1 };
        tex_info.type = RESOURCE_TYPE_RENDER_TARGET | RESOURCE_TYPE_TEXTURE;
        auto texture = std::make_shared<DX11Texture>(tex_info, backend, back_buffer);
        texture->init();
        textures_.push_back(texture);
    }
}

RHITextureRef DX11Swapchain::get_new_frame(RHIFenceRef fence, RHISemaphoreRef signal_semaphore) {
    if (swap_chain_) {
        current_index_ = swap_chain_->GetCurrentBackBufferIndex();
    }
    return textures_[current_index_];
}

void DX11Swapchain::present(RHISemaphoreRef wait_semaphore) {
    if (swap_chain_) {
        swap_chain_->Present(1, 0);
    }
}

void DX11Swapchain::destroy() { swap_chain_.Reset(); textures_.clear(); }

// --- DX11Sampler ---
DX11Sampler::DX11Sampler(const RHISamplerInfo& info, DX11Backend& backend) 
    : RHISampler(info), backend_(backend) {
}

bool DX11Sampler::init() {
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = backend_.get_device()->CreateSamplerState(&desc, sampler_state_.GetAddressOf());
    if (FAILED(hr)) {
        ERR(LogRHI, "Failed to create DX11 Sampler State (HRESULT: 0x{:08X})", (uint32_t)hr);
        return false;
    }
    return true;
}

void DX11Sampler::destroy() { sampler_state_.Reset(); }

// --- DX11Shader ---
DX11Shader::DX11Shader(const RHIShaderInfo& info, DX11Backend& backend) 
    : RHIShader(info), backend_(backend) {
}

bool DX11Shader::init() {
    HRESULT hr = S_OK;
    const void* code_ptr = info_.code.data();
    size_t code_size = info_.code.size();
    
    if (code_size == 0) {
        ERR(LogRHI, "Shader code is empty");
        return false;
    }

    switch (info_.frequency) {
        case SHADER_FREQUENCY_VERTEX:
            hr = backend_.get_device()->CreateVertexShader(code_ptr, code_size, nullptr, (ID3D11VertexShader**)shader_resource_.GetAddressOf());
            break;
        case SHADER_FREQUENCY_FRAGMENT:
            hr = backend_.get_device()->CreatePixelShader(code_ptr, code_size, nullptr, (ID3D11PixelShader**)shader_resource_.GetAddressOf());
            break;
        case SHADER_FREQUENCY_COMPUTE:
            hr = backend_.get_device()->CreateComputeShader(code_ptr, code_size, nullptr, (ID3D11ComputeShader**)shader_resource_.GetAddressOf());
            break;
        default:
            ERR(LogRHI, "Unsupported shader frequency for DX11");
            return false;
    }

    if (FAILED(hr)) {
        ERR(LogRHI, "Failed to create DX11 Shader (HRESULT: 0x{:08X})", (uint32_t)hr);
        return false;
    }

    return true;
}

void DX11Shader::destroy() { shader_resource_.Reset(); blob_.Reset(); }

// --- DX11GraphicsPipeline ---
DX11GraphicsPipeline::DX11GraphicsPipeline(const RHIGraphicsPipelineInfo& info, DX11Backend& backend)
    : RHIGraphicsPipeline(info), backend_(backend) {
}

bool DX11GraphicsPipeline::init() {
    auto vs = resource_cast(info_.vertex_shader);
    if (vs) {
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        for (const auto& el : info_.vertex_input_state.vertex_elements) {
            // Map attribute_index to semantic name
            const char* semantic_name = "POSITION";
            switch (el.attribute_index) {
                case 0: semantic_name = "POSITION"; break;
                case 1: semantic_name = "NORMAL"; break;
                case 2: semantic_name = "TEXCOORD"; break;
                case 3: semantic_name = "COLOR"; break;
                case 4: semantic_name = "TANGENT"; break;
                default: semantic_name = "POSITION"; break;
            }
            D3D11_INPUT_ELEMENT_DESC desc = { semantic_name, 0, DX11Util::rhi_format_to_dxgi(el.format), el.stream_index, el.offset, 
                                              el.use_instance_index ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, el.use_instance_index ? 1u : 0u };
            elements.push_back(desc);
        }
        backend_.get_device()->CreateInputLayout(elements.data(), (UINT)elements.size(), vs->get_info().code.data(), vs->get_info().code.size(), input_layout_.GetAddressOf());
    }
    
    // Rasterizer State
    D3D11_RASTERIZER_DESC rast_desc = {};
    rast_desc.FillMode = DX11Util::fill_mode_to_dx11(info_.rasterizer_state.fill_mode);
    rast_desc.CullMode = DX11Util::cull_mode_to_dx11(info_.rasterizer_state.cull_mode);
    rast_desc.FrontCounterClockwise = FALSE; 
    rast_desc.DepthBias = (INT)info_.rasterizer_state.depth_bias;
    rast_desc.SlopeScaledDepthBias = info_.rasterizer_state.slope_scale_depth_bias;
    rast_desc.DepthClipEnable = info_.rasterizer_state.depth_clip_mode == DEPTH_CLIP;
    rast_desc.ScissorEnable = FALSE; // Disable scissor for now
    rast_desc.MultisampleEnable = FALSE;
    rast_desc.AntialiasedLineEnable = FALSE;

    backend_.get_device()->CreateRasterizerState(&rast_desc, rasterizer_state_.GetAddressOf());
    
    // Blend State
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    backend_.get_device()->CreateBlendState(&blend_desc, blend_state_.GetAddressOf());
    
    // Depth Stencil State
    D3D11_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = info_.depth_stencil_state.enable_depth_test;
    ds_desc.DepthWriteMask = info_.depth_stencil_state.enable_depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    ds_desc.DepthFunc = DX11Util::compare_func_to_dx11(info_.depth_stencil_state.depth_test);
    ds_desc.StencilEnable = FALSE;
    
    backend_.get_device()->CreateDepthStencilState(&ds_desc, depth_stencil_state_.GetAddressOf());
    topology_ = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    return true;
}

void DX11GraphicsPipeline::bind(ID3D11DeviceContext* context) {
    auto vs = resource_cast(info_.vertex_shader);
    auto ps = resource_cast(info_.fragment_shader);
    if (vs) context->VSSetShader((ID3D11VertexShader*)vs->get_shader().Get(), nullptr, 0);
    if (ps) context->PSSetShader((ID3D11PixelShader*)ps->get_shader().Get(), nullptr, 0);
    context->IASetInputLayout(input_layout_.Get());
    context->IASetPrimitiveTopology(topology_);
    context->RSSetState(rasterizer_state_.Get());
    context->OMSetBlendState(blend_state_.Get(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(depth_stencil_state_.Get(), 0);
}

void DX11GraphicsPipeline::destroy() {
    input_layout_.Reset(); rasterizer_state_.Reset(); blend_state_.Reset(); depth_stencil_state_.Reset();
}

// --- DX11Fence ---
DX11Fence::DX11Fence(bool signaled, DX11Backend& backend) 
    : backend_(backend), signaled_(signaled) {
}

bool DX11Fence::init() {
    D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
    HRESULT hr = backend_.get_device()->CreateQuery(&desc, query_.GetAddressOf());
    if (FAILED(hr)) {
        ERR(LogRHI, "Failed to create DX11 Query for Fence (HRESULT: 0x{:08X})", (uint32_t)hr);
        return false;
    }
    // Note: DX11 Queries don't have a "signaled" state on creation in the same way Vulkan Fences do.
    // If signaled_ is true, we might need to issue an End() on it, but typical usage here
    // is to wait for a GPU command to complete.
    return true;
}

void DX11Fence::wait() {
    while (backend_.get_context()->GetData(query_.Get(), nullptr, 0, 0) == S_FALSE) {}
}

void DX11Fence::destroy() { query_.Reset(); }

// --- DX11Backend ---
DX11Backend::DX11Backend(const RHIBackendInfo& info) : RHIBackend(info) {
    UINT flags = 0;
    if (info.enable_debug) flags |= D3D11_CREATE_DEVICE_DEBUG;

    D3D_FEATURE_LEVEL feature_levels[] = { 
        D3D_FEATURE_LEVEL_11_1, 
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    // 1. Try Hardware with requested flags
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, feature_levels, _countof(feature_levels), D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr, context_.GetAddressOf());
    
    // 2. Fallback: Try Hardware without Debug flag (if debug was requested)
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        WARN(LogRHI, "Failed to create DX11 Device with Debug Layer. Trying without debug...");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, feature_levels, _countof(feature_levels), D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr, context_.GetAddressOf());
    }

    ComPtr<IDXGIDevice> dxgi_device;
    device_.As(&dxgi_device);
    ComPtr<IDXGIAdapter> adapter;
    dxgi_device->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory), (void**)factory_.GetAddressOf());
    
    immediate_context_ = std::make_shared<DX11CommandContextImmediate>(*this);
}

void DX11Backend::tick() { RHIBackend::tick(); }
void DX11Backend::destroy() { 
    // Release immediate context wrapper first (it holds references to backend)
    immediate_context_.reset();
    
    // Release D3D11 resources
    context_.Reset(); 
    device_.Reset(); 
    factory_.Reset(); 
}
void DX11Backend::init_imgui(GLFWwindow* window) {}
RHIQueueRef DX11Backend::get_queue(const RHIQueueInfo& info) { return std::make_shared<DX11Queue>(info); }

RHISurfaceRef DX11Backend::create_surface(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    return std::make_shared<DX11Surface>(hwnd);
}

RHISwapchainRef DX11Backend::create_swapchain(const RHISwapchainInfo& info) {
    auto swapchain = std::make_shared<DX11Swapchain>(info, *this);
    register_resource(swapchain);
    return swapchain;
}

RHICommandPoolRef DX11Backend::create_command_pool(const RHICommandPoolInfo& info) { return std::make_shared<DX11CommandPool>(info); }
RHICommandContextRef DX11Backend::create_command_context(RHICommandPoolRef pool) { return std::make_shared<DX11CommandContext>(pool, *this); }

RHIBufferRef DX11Backend::create_buffer(const RHIBufferInfo& info) {
    auto buffer = std::make_shared<DX11Buffer>(info, *this);
    if (!buffer->init()) {
        return nullptr;
    }
    register_resource(buffer); return buffer;
}

RHITextureRef DX11Backend::create_texture(const RHITextureInfo& info) {
    auto texture = std::make_shared<DX11Texture>(info, *this);
    if (!texture->init()) {
        return nullptr;
    }
    register_resource(texture); return texture;
}

RHITextureViewRef DX11Backend::create_texture_view(const RHITextureViewInfo& info) {
    auto view = std::make_shared<DX11TextureView>(info, *this);
    register_resource(view); return view;
}

RHISamplerRef DX11Backend::create_sampler(const RHISamplerInfo& info) {
    auto sampler = std::make_shared<DX11Sampler>(info, *this);
    if (!sampler->init()) {
        return nullptr;
    }
    register_resource(sampler); return sampler;
}

RHIShaderRef DX11Backend::create_shader(const RHIShaderInfo& info) {
    auto shader = std::make_shared<DX11Shader>(info, *this);
    if (!shader->init()) {
        WARN(LogRHI, "Shader initialization failed, using null shader fallback.");
        return nullptr;
    }
    register_resource(shader); 
    return shader;
}

RHIShaderBindingTableRef DX11Backend::create_shader_binding_table(const RHIShaderBindingTableInfo& info) { return nullptr; }
RHITopLevelAccelerationStructureRef DX11Backend::create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo& info) { return nullptr; }
RHIBottomLevelAccelerationStructureRef DX11Backend::create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo& info) { return nullptr; }

RHIRootSignatureRef DX11Backend::create_root_signature(const RHIRootSignatureInfo& info) {
    auto sig = std::make_shared<DX11RootSignature>(info, *this);
    if (!sig->init()) {
        return nullptr;
    }
    register_resource(sig); return sig;
}

RHIRenderPassRef DX11Backend::create_render_pass(const RHIRenderPassInfo& info) {
    auto pass = std::make_shared<DX11RenderPass>(info, *this);
    if (!pass->init()) {
        return nullptr;
    }
    register_resource(pass); return pass;
}

RHIGraphicsPipelineRef DX11Backend::create_graphics_pipeline(const RHIGraphicsPipelineInfo& info) {
    auto pipeline = std::make_shared<DX11GraphicsPipeline>(info, *this);
    if (!pipeline->init()) {
        return nullptr;
    }
    register_resource(pipeline); return pipeline;
}

RHIComputePipelineRef DX11Backend::create_compute_pipeline(const RHIComputePipelineInfo& info) { return nullptr; }
RHIRayTracingPipelineRef DX11Backend::create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo& info) { return nullptr; }
RHIFenceRef DX11Backend::create_fence(bool signaled) { 
    auto fence = std::make_shared<DX11Fence>(signaled, *this);
    if (!fence->init()) {
        return nullptr;
    }
    return fence;
}
RHISemaphoreRef DX11Backend::create_semaphore() { return std::make_shared<DX11Semaphore>(*this); }
RHICommandContextImmediateRef DX11Backend::get_immediate_command() { return immediate_context_; }

std::vector<uint8_t> DX11Backend::compile_shader(const char* source, const char* entry, const char* profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, 
                            &blob, &error_blob);
    
    if (FAILED(hr)) {
        if (error_blob) {
            ERR(LogRHI, "Shader compilation failed: {}", (char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return {};
    }
    
    if (error_blob) {
        error_blob->Release();
    }
    
    std::vector<uint8_t> code(blob->GetBufferSize());
    memcpy(code.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();
    
    return code;
}

// --- DX11CommandContext ---
DX11CommandContext::DX11CommandContext(RHICommandPoolRef pool, DX11Backend& backend) : RHICommandContext(pool), backend_(backend), context_(backend.get_context()) {}
void DX11CommandContext::begin_command() {}
void DX11CommandContext::end_command() {}
void DX11CommandContext::execute(RHIFenceRef fence, RHISemaphoreRef ws, RHISemaphoreRef ss) {
    if (fence) {
        auto dx_fence = resource_cast(fence);
        context_->End(dx_fence->raw_handle_as<ID3D11Query>());
    }
}
void DX11CommandContext::texture_barrier(const RHITextureBarrier& b) {}
void DX11CommandContext::buffer_barrier(const RHIBufferBarrier& b) {}
void DX11CommandContext::copy_texture_to_buffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff) { context_->CopyResource((ID3D11Resource*)d->raw_handle(), (ID3D11Resource*)s->raw_handle()); }
void DX11CommandContext::copy_buffer_to_texture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds) { context_->CopyResource((ID3D11Resource*)d->raw_handle(), (ID3D11Resource*)s->raw_handle()); }
void DX11CommandContext::copy_buffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz) { D3D11_BOX box = { (UINT)soff, 0, 0, (UINT)(soff + sz), 1, 1 }; context_->CopySubresourceRegion((ID3D11Resource*)d->raw_handle(), 0, (UINT)doff, 0, 0, (ID3D11Resource*)s->raw_handle(), 0, &box); }
void DX11CommandContext::copy_texture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds) { context_->CopyResource((ID3D11Resource*)d->raw_handle(), (ID3D11Resource*)s->raw_handle()); }
void DX11CommandContext::generate_mips(RHITextureRef s) {}
void DX11CommandContext::push_event(const std::string& n, Color3 c) {}
void DX11CommandContext::pop_event() {}
void DX11CommandContext::begin_render_pass(RHIRenderPassRef rp) {
    if (!rp) return;
    const auto& info = rp->get_info();
    
    std::vector<ID3D11RenderTargetView*> rtvs;
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i) {
        if (info.color_attachments[i].texture_view) {
            auto dx_view = resource_cast(info.color_attachments[i].texture_view);
            ID3D11RenderTargetView* rtv = dx_view->get_rtv().Get();
            rtvs.push_back(rtv);

            if (info.color_attachments[i].load_op == ATTACHMENT_LOAD_OP_CLEAR) {
                context_->ClearRenderTargetView(rtv, (float*)&info.color_attachments[i].clear_color);
            }
        }
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (info.depth_stencil_attachment.texture_view) {
        auto dx_view = resource_cast(info.depth_stencil_attachment.texture_view);
        dsv = dx_view->get_dsv().Get();
        
        UINT clear_flags = 0;
        if (info.depth_stencil_attachment.load_op == ATTACHMENT_LOAD_OP_CLEAR) {
            clear_flags |= D3D11_CLEAR_DEPTH;
            // if (has_stencil) clear_flags |= D3D11_CLEAR_STENCIL;
            context_->ClearDepthStencilView(dsv, clear_flags, info.depth_stencil_attachment.clear_depth, (UINT8)info.depth_stencil_attachment.clear_stencil);
        }
    }

    if (!rtvs.empty() || dsv) {
        context_->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), dsv);
    }
}
void DX11CommandContext::end_render_pass() {}
void DX11CommandContext::set_viewport(Offset2D min, Offset2D max) { D3D11_VIEWPORT vp = { (float)min.x, (float)min.y, (float)(max.x - min.x), (float)(max.y - min.y), 0.0f, 1.0f }; context_->RSSetViewports(1, &vp); }
void DX11CommandContext::set_scissor(Offset2D min, Offset2D max) { D3D11_RECT rect = { (LONG)min.x, (LONG)min.y, (LONG)max.x, (LONG)max.y }; context_->RSSetScissorRects(1, &rect); }
void DX11CommandContext::set_depth_bias(float c, float s, float cl) {}
void DX11CommandContext::set_line_width(float w) {}
void DX11CommandContext::set_graphics_pipeline(RHIGraphicsPipelineRef p) { resource_cast(p)->bind(context_.Get()); }
void DX11CommandContext::set_compute_pipeline(RHIComputePipelineRef p) {}
void DX11CommandContext::set_ray_tracing_pipeline(RHIRayTracingPipelineRef p) {}
void DX11CommandContext::push_constants(void* d, uint16_t s, ShaderFrequency f) {}
void DX11CommandContext::bind_descriptor_set(RHIDescriptorSetRef d, uint32_t s) {}
void DX11CommandContext::bind_constant_buffer(RHIBufferRef b, uint32_t s, ShaderFrequency f) {
    ID3D11Buffer* cb = (ID3D11Buffer*)b->raw_handle();
    if (f & SHADER_FREQUENCY_VERTEX) context_->VSSetConstantBuffers(s, 1, &cb);
    if (f & SHADER_FREQUENCY_FRAGMENT) context_->PSSetConstantBuffers(s, 1, &cb);
    if (f & SHADER_FREQUENCY_COMPUTE) context_->CSSetConstantBuffers(s, 1, &cb);
}
void DX11CommandContext::bind_vertex_buffer(RHIBufferRef b, uint32_t s, uint32_t o) { ID3D11Buffer* vb = (ID3D11Buffer*)b->raw_handle(); UINT stride = b->get_info().stride; UINT uo = (UINT)o; context_->IASetVertexBuffers(s, 1, &vb, &stride, &uo); }
void DX11CommandContext::bind_index_buffer(RHIBufferRef b, uint32_t o) { context_->IASetIndexBuffer((ID3D11Buffer*)b->raw_handle(), DXGI_FORMAT_R32_UINT, (UINT)o); }
void DX11CommandContext::dispatch(uint32_t x, uint32_t y, uint32_t z) { context_->Dispatch(x, y, z); }
void DX11CommandContext::dispatch_indirect(RHIBufferRef b, uint32_t o) { context_->DispatchIndirect((ID3D11Buffer*)b->raw_handle(), (UINT)o); }
void DX11CommandContext::trace_rays(uint32_t x, uint32_t y, uint32_t z) {}
void DX11CommandContext::draw(uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) { if (ic > 1) context_->DrawInstanced(vc, ic, fv, fi); else context_->Draw(vc, fv); }
void DX11CommandContext::draw_indexed(uint32_t ic, uint32_t instc, uint32_t fi, uint32_t vo, uint32_t finst) { if (instc > 1) context_->DrawIndexedInstanced(ic, instc, fi, vo, finst); else context_->DrawIndexed(ic, fi, vo); }
void DX11CommandContext::draw_indirect(RHIBufferRef b, uint32_t o, uint32_t c) { context_->DrawInstancedIndirect((ID3D11Buffer*)b->raw_handle(), (UINT)o); }
void DX11CommandContext::draw_indexed_indirect(RHIBufferRef b, uint32_t o, uint32_t c) { context_->DrawIndexedInstancedIndirect((ID3D11Buffer*)b->raw_handle(), (UINT)o); }
void DX11CommandContext::imgui_create_fonts_texture() {}
void DX11CommandContext::imgui_render_draw_data() {}

bool DX11CommandContext::read_texture(RHITextureRef texture, void* data, uint32_t size) {
    if (!texture || !data || size == 0) return false;
    
    auto* dx11_texture = static_cast<DX11Texture*>(texture.get());
    if (!dx11_texture) return false;
    
    ID3D11Texture2D* src_texture = dx11_texture->get_handle().Get();
    if (!src_texture) return false;
    
    D3D11_TEXTURE2D_DESC desc;
    src_texture->GetDesc(&desc);
    
    // Create staging texture for CPU read
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    
    ComPtr<ID3D11Texture2D> staging_texture;
    HRESULT hr = backend_.get_device()->CreateTexture2D(&staging_desc, nullptr, staging_texture.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Copy to staging
    context_->CopyResource(staging_texture.Get(), src_texture);
    
    // Map and read
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;
    
    // Copy row by row (handle pitch)
    uint32_t row_size = desc.Width * 4; // Assuming RGBA8 format
    uint32_t height = desc.Height;
    
    if (size < row_size * height) {
        context_->Unmap(staging_texture.Get(), 0);
        return false;
    }
    
    uint8_t* dst = static_cast<uint8_t*>(data);
    uint8_t* src = static_cast<uint8_t*>(mapped.pData);
    
    for (uint32_t y = 0; y < height; y++) {
        memcpy(dst + y * row_size, src + y * mapped.RowPitch, row_size);
    }
    
    context_->Unmap(staging_texture.Get(), 0);
    return true;
}

DX11RenderPass::DX11RenderPass(const RHIRenderPassInfo& info, DX11Backend& backend) : RHIRenderPass(info) {}
RHIDescriptorSetRef DX11RootSignature::create_descriptor_set(uint32_t set) { return nullptr; }
DX11CommandContextImmediate::DX11CommandContextImmediate(DX11Backend& backend) : backend_(backend) {}
void DX11CommandContextImmediate::flush() {}
void DX11CommandContextImmediate::texture_barrier(const RHITextureBarrier& b) {}
void DX11CommandContextImmediate::buffer_barrier(const RHIBufferBarrier& b) {}
void DX11CommandContextImmediate::copy_texture_to_buffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff) {}
void DX11CommandContextImmediate::copy_buffer_to_texture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds) {}
void DX11CommandContextImmediate::copy_buffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz) {}
void DX11CommandContextImmediate::copy_texture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds) {}
void DX11CommandContextImmediate::generate_mips(RHITextureRef s) {}