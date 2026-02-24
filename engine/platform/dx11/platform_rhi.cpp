#include "platform_rhi.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/main/engine_context.h"
#include <d3d11_1.h>
#include <d3d11shader.h>
#include <dxgi1_6.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <d3dcompiler.h>

#include <imgui.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

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
        if (type & RESOURCE_TYPE_DEPTH_STENCIL) {
            flags |= D3D11_BIND_DEPTH_STENCIL;
            // Also add SRV bind flag so depth buffer can be sampled/shown in ImGui
            flags |= D3D11_BIND_SHADER_RESOURCE;
        }
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

    static D3D11_PRIMITIVE_TOPOLOGY primitive_type_to_dx11(PrimitiveType type) {
        switch (type) {
            case PRIMITIVE_TYPE_TRIANGLE_LIST: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case PRIMITIVE_TYPE_TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case PRIMITIVE_TYPE_LINE_LIST: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case PRIMITIVE_TYPE_POINT_LIST: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
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

    if (!get_name().empty()) {
        backend_.set_name(shared_from_this(), get_name());
    }

    return true;
}

void* DX11Buffer::map() {
    if (!buffer_ || mapped_data_) return nullptr;
    
    D3D11_MAPPED_SUBRESOURCE mapped_res;
    D3D11_MAP map_type = D3D11_MAP_WRITE_DISCARD;
    
    D3D11_BUFFER_DESC desc;
    buffer_->GetDesc(&desc);
    
    // Determine map type based on buffer usage and CPU access flags
    if (desc.Usage == D3D11_USAGE_STAGING) {
        // Staging buffers: check CPU access flags for read/write permissions
        if ((desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) && (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)) {
            map_type = D3D11_MAP_READ_WRITE;
        } else if (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) {
            map_type = D3D11_MAP_READ;
        } else if (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) {
            map_type = D3D11_MAP_WRITE;
        } else {
            return nullptr; // No CPU access
        }
    } else if (desc.Usage == D3D11_USAGE_DYNAMIC) {
        // Dynamic buffers: use WRITE_DISCARD for CPU-to-GPU updates
        map_type = D3D11_MAP_WRITE_DISCARD;
    } else {
        // Default/Immutable buffers cannot be mapped
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
    if (!buffer_ || !mapped_data_) return;
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
        
        DXGI_FORMAT format = DX11Util::rhi_format_to_dxgi(info_.format);
        // Use typeless for depth-stencil to allow both DSV and SRV
        if (info_.type & RESOURCE_TYPE_DEPTH_STENCIL) {
            if (format == DXGI_FORMAT_D32_FLOAT) format = DXGI_FORMAT_R32_TYPELESS;
            else if (format == DXGI_FORMAT_D24_UNORM_S8_UINT) format = DXGI_FORMAT_R24G8_TYPELESS;
        } else if (info_.mip_levels > 1) {
            // For SRGB textures with mipmaps: use UNORM format for the texture (required for GenerateMips)
            // The SRV will be created with SRGB format for correct gamma correction
            if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) format = DXGI_FORMAT_R8G8B8A8_UNORM;
            else if (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) format = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        desc.Format = format;
        
        desc.SampleDesc.Count = 1;
        desc.Usage = DX11Util::memory_usage_to_dx11(info_.memory_usage);
        desc.BindFlags = DX11Util::resource_type_to_bind_flags(info_.type);
        if (info_.mip_levels > 1 && !(info_.type & RESOURCE_TYPE_DEPTH_STENCIL)) {
            desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        }
        desc.CPUAccessFlags = (info_.memory_usage == MEMORY_USAGE_CPU_TO_GPU) ? D3D11_CPU_ACCESS_WRITE : 0;

        HRESULT hr = backend_.get_device()->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr)) {
            ERR(LogRHI, "Failed to create DX11 Texture2D ({}x{}, format: {}, bind: 0x{:X}, type: 0x{:X}, HRESULT: 0x{:08X})", 
                desc.Width, desc.Height, (uint32_t)desc.Format, (uint32_t)desc.BindFlags, (uint32_t)info_.type, (uint32_t)hr);
            return false;
        }
    }
    
    if (!get_name().empty()) {
        backend_.set_name(shared_from_this(), get_name());
    }
    
    return true;
}

void DX11Texture::destroy() {
    srv_.Reset();
    texture_.Reset();
}

ComPtr<ID3D11ShaderResourceView> DX11Texture::create_srv() {
    if (!texture_) return nullptr;
    if (srv_) return srv_;
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    
    // Get the actual texture format
    D3D11_TEXTURE2D_DESC tex_desc;
    texture_->GetDesc(&tex_desc);
    DXGI_FORMAT format = tex_desc.Format;
    
    // Handle depth format mapping for SRV (D32/TYPELESS->R32, etc.)
    if (format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_R32_TYPELESS) format = DXGI_FORMAT_R32_FLOAT;
    else if (format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_R24G8_TYPELESS) format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    
    srv_desc.Format = format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = info_.mip_levels > 0 ? info_.mip_levels : (UINT)-1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    
    HRESULT hr = backend_.get_device()->CreateShaderResourceView(texture_.Get(), &srv_desc, srv_.GetAddressOf());
    if (FAILED(hr)) {
        ERR(LogRHI, "Failed to create SRV for texture (format: {}, HRESULT: 0x{:08X})", (uint32_t)format, (uint32_t)hr);
        return nullptr;
    }
    
    return srv_;
}

ComPtr<ID3D11RenderTargetView> DX11Texture::create_rtv() {
    if (!texture_) return nullptr;
    ComPtr<ID3D11RenderTargetView> rtv;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = DX11Util::rhi_format_to_dxgi(info_.format);
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    backend_.get_device()->CreateRenderTargetView(texture_.Get(), &rtv_desc, rtv.GetAddressOf());
    return rtv;
}

// --- DX11TextureView ---
DX11TextureView::DX11TextureView(const RHITextureViewInfo& info, DX11Backend& backend)
    : RHITextureView(info) {
    auto dx_tex = resource_cast(info.texture);
    if (!dx_tex) return;

    const auto& tex_info = info.texture->get_info();
    DXGI_FORMAT base_format = DX11Util::rhi_format_to_dxgi(tex_info.format);
    
    // Get the actual DXGI format used when creating the texture
    // For depth-stencil textures, this might be a typeless format (R32_TYPELESS, R24G8_TYPELESS)
    D3D11_TEXTURE2D_DESC tex_desc = {};
    dx_tex->get_handle()->GetDesc(&tex_desc);
    DXGI_FORMAT actual_tex_format = tex_desc.Format;
    
    // Determine view format:
    // - For depth-stencil textures, we must use a format compatible with the actual texture format
    // - If info.format is specified but incompatible with depth-stencil texture, fall back to base_format
    DXGI_FORMAT view_format;
    if (info.format != FORMAT_UKNOWN) {
        view_format = DX11Util::rhi_format_to_dxgi(info.format);
        // If texture is depth-stencil but view_format is not depth-compatible, use base_format
        if ((tex_info.type & RESOURCE_TYPE_DEPTH_STENCIL) && 
            !(view_format == DXGI_FORMAT_D32_FLOAT || view_format == DXGI_FORMAT_R32_TYPELESS ||
              view_format == DXGI_FORMAT_R32_FLOAT || view_format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
              view_format == DXGI_FORMAT_R24G8_TYPELESS || view_format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)) {
            view_format = base_format;
        }
    } else {
        view_format = base_format;
    }

    // Create SRV for texture types
    if (tex_info.type & RESOURCE_TYPE_TEXTURE) {
        // Check if texture was created with D3D11_BIND_SHADER_RESOURCE flag
        // Swapchain back buffers may not have this flag, so we skip SRV creation for them
        if ((tex_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
            // Texture doesn't support SRV, skip creation silently
            // (This is normal for swapchain back buffers)
        } else {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            // Use actual texture format for SRV, not the original RHI format
            // This is necessary because the texture may have been created with a different format
            // (e.g., SRGB -> UNORM for mipmap generation support)
            DXGI_FORMAT srv_format = actual_tex_format;
            
            // Handle depth format mapping for SRV (depth/typeless formats cannot be directly sampled)
            if (srv_format == DXGI_FORMAT_D32_FLOAT || srv_format == DXGI_FORMAT_R32_TYPELESS) {
                srv_format = DXGI_FORMAT_R32_FLOAT;
            } else if (srv_format == DXGI_FORMAT_D24_UNORM_S8_UINT || srv_format == DXGI_FORMAT_R24G8_TYPELESS) {
                srv_format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }
            
            srv_desc.Format = srv_format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = tex_info.mip_levels > 0 ? tex_info.mip_levels : (UINT)-1;
            srv_desc.Texture2D.MostDetailedMip = info.subresource.base_mip_level;
            HRESULT hr = backend.get_device()->CreateShaderResourceView(dx_tex->get_handle().Get(), &srv_desc, srv_.GetAddressOf());
            if (FAILED(hr)) {
                ERR(LogRHI, "Failed to create texture SRV (format: {}, HRESULT: 0x{:08X})", 
                    (uint32_t)srv_format, (uint32_t)hr);
            }
        }
    }

    // Create RTV for render target types
    if (tex_info.type & RESOURCE_TYPE_RENDER_TARGET) {
        D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        // Use actual texture format for RTV
        DXGI_FORMAT rtv_format = actual_tex_format;
        
        // Handle typeless format mapping for RTV
        if (rtv_format == DXGI_FORMAT_R32_TYPELESS || rtv_format == DXGI_FORMAT_D32_FLOAT) rtv_format = DXGI_FORMAT_R32_FLOAT;
        else if (rtv_format == DXGI_FORMAT_R24G8_TYPELESS || rtv_format == DXGI_FORMAT_D24_UNORM_S8_UINT) rtv_format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        
        rtv_desc.Format = rtv_format;
        rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtv_desc.Texture2D.MipSlice = info.subresource.base_mip_level;
        backend.get_device()->CreateRenderTargetView(dx_tex->get_handle().Get(), &rtv_desc, rtv_.GetAddressOf());
    }

    // Create DSV for depth-stencil types
    if (tex_info.type & RESOURCE_TYPE_DEPTH_STENCIL) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        // Use actual texture format for DSV
        DXGI_FORMAT dsv_format = actual_tex_format;
        
        // Handle typeless format mapping for DSV
        if (dsv_format == DXGI_FORMAT_R32_TYPELESS) dsv_format = DXGI_FORMAT_D32_FLOAT;
        else if (dsv_format == DXGI_FORMAT_R24G8_TYPELESS) dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        // Ensure we're using a depth-stencil format, not a typeless one
        else if (dsv_format == DXGI_FORMAT_UNKNOWN || dsv_format == 0) {
            // Fallback based on original texture format
            if (base_format == DXGI_FORMAT_R32_TYPELESS || base_format == DXGI_FORMAT_D32_FLOAT) {
                dsv_format = DXGI_FORMAT_D32_FLOAT;
            } else if (base_format == DXGI_FORMAT_R24G8_TYPELESS || base_format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
                dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            }
        }
        
        dsv_desc.Format = dsv_format;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Texture2D.MipSlice = info.subresource.base_mip_level;
        backend.get_device()->CreateDepthStencilView(dx_tex->get_handle().Get(), &dsv_desc, dsv_.GetAddressOf());
        
        // Create read-only DSV for simultaneous SRV binding
        dsv_desc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
        backend.get_device()->CreateDepthStencilView(dx_tex->get_handle().Get(), &dsv_desc, dsv_read_only_.GetAddressOf());
        
        // If srv_ was not created yet (e.g. depth texture not marked as TEXTURE), create it for shader sampling
        if (!srv_) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            DXGI_FORMAT srv_format = dsv_format;
            if (srv_format == DXGI_FORMAT_D32_FLOAT) srv_format = DXGI_FORMAT_R32_FLOAT;
            else if (srv_format == DXGI_FORMAT_D24_UNORM_S8_UINT) srv_format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            
            srv_desc.Format = srv_format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            srv_desc.Texture2D.MostDetailedMip = info.subresource.base_mip_level;
            HRESULT hr = backend.get_device()->CreateShaderResourceView(dx_tex->get_handle().Get(), &srv_desc, srv_.GetAddressOf());
            if (FAILED(hr)) {
                ERR(LogRHI, "Failed to create depth buffer SRV (HRESULT: 0x{:08X})", (uint32_t)hr);
            }
        }
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
    
    // Check if swap effect supports GetCurrentBackBufferIndex()
    // FLIP_* effects support it, DISCARD/SEQUENTIAL do not
    supports_frame_index_query_ = (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD || 
                                   desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);

    // Query actual buffer count from swapchain (for some swap effects, actual count may differ from requested)
    DXGI_SWAP_CHAIN_DESC actual_desc = {};
    swap_chain_->GetDesc(&actual_desc);
    uint32_t requested_buffer_count = actual_desc.BufferCount;
    
    for (uint32_t i = 0; i < requested_buffer_count; ++i) {
        ComPtr<ID3D11Texture2D> back_buffer;
        HRESULT hr = swap_chain_->GetBuffer(i, __uuidof(ID3D11Texture2D), (void**)back_buffer.GetAddressOf());
        if (FAILED(hr) || !back_buffer) {
            // For some swap effects (e.g., DISCARD), actual buffer count may be less than requested
            // Stop trying to get more buffers
            break;
        }
        RHITextureInfo tex_info = {};
        tex_info.format = info.format;
        tex_info.extent = { info.extent.width, info.extent.height, 1 };
        tex_info.mip_levels = 1;
        tex_info.type = RESOURCE_TYPE_RENDER_TARGET | RESOURCE_TYPE_TEXTURE;
        auto texture = std::make_shared<DX11Texture>(tex_info, backend, back_buffer);
        texture->init();
        textures_.push_back(texture);

        ComPtr<ID3D11RenderTargetView> rtv;
        backend.get_device()->CreateRenderTargetView(back_buffer.Get(), nullptr, rtv.GetAddressOf());
        back_buffer_rtvs_.push_back(rtv);
    }
}

RHITextureRef DX11Swapchain::get_new_frame(RHIFenceRef fence, RHISemaphoreRef signal_semaphore) {
    if (!swap_chain_ || textures_.empty()) {
        return nullptr;
    }
    
    // Get current back buffer index from swapchain (only if supported)
    if (supports_frame_index_query_) {
        current_index_ = swap_chain_->GetCurrentBackBufferIndex();
        
        // Validate index is within bounds
        if (current_index_ >= textures_.size()) {
            current_index_ = 0;
        }
    }
    // If not supported, current_index_ is managed manually in present()
    
    return textures_[current_index_];
}

void DX11Swapchain::present(RHISemaphoreRef wait_semaphore) {
    if (swap_chain_) {
        HRESULT hr = swap_chain_->Present(0, 0);  // Disable VSync for testing
        if (FAILED(hr)) {
            ERR(LogRHI, "Present failed with HRESULT: 0x{:08X}", (uint32_t)hr);
        }
        
        // If frame index query is not supported, manually advance to next frame
        if (!supports_frame_index_query_) {
            current_index_ = (current_index_ + 1) % textures_.size();
        }
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
        
        // Use Shader Reflection to get input signature from vertex shader bytecode
        ComPtr<ID3D11ShaderReflection> reflector;
        HRESULT hr = D3DReflect(vs->get_info().code.data(), vs->get_info().code.size(), 
                                IID_ID3D11ShaderReflection, (void**)reflector.GetAddressOf());
        
        if (SUCCEEDED(hr) && reflector) {
            D3D11_SHADER_DESC shader_desc;
            reflector->GetDesc(&shader_desc);
            
            // Build a map of semantic name -> input slot index for matching
            std::unordered_map<std::string, uint32_t> semantic_to_element_index;
            for (uint32_t i = 0; i < info_.vertex_input_state.vertex_elements.size(); ++i) {
                semantic_to_element_index[info_.vertex_input_state.vertex_elements[i].semantic_name] = i;
            }
            
            // Iterate through shader input parameters
            for (uint32_t i = 0; i < shader_desc.InputParameters; ++i) {
                D3D11_SIGNATURE_PARAMETER_DESC param_desc;
                reflector->GetInputParameterDesc(i, &param_desc);
                
                // Find matching vertex element by semantic name (case-insensitive)
                auto it = semantic_to_element_index.find(param_desc.SemanticName);
                if (it == semantic_to_element_index.end()) {
                    // Try case-insensitive match
                    for (const auto& pair : semantic_to_element_index) {
                        if (_stricmp(pair.first.c_str(), param_desc.SemanticName) == 0) {
                            it = semantic_to_element_index.find(pair.first);
                            break;
                        }
                    }
                }
                
                if (it != semantic_to_element_index.end()) {
                    const auto& el = info_.vertex_input_state.vertex_elements[it->second];
                    
                    D3D11_INPUT_ELEMENT_DESC desc = {};
                    desc.SemanticName = param_desc.SemanticName;  // Use semantic from shader
                    desc.SemanticIndex = param_desc.SemanticIndex; // Use semantic index from shader
                    desc.Format = DX11Util::rhi_format_to_dxgi(el.format);
                    desc.InputSlot = el.stream_index;
                    desc.AlignedByteOffset = el.offset;
                    desc.InputSlotClass = el.use_instance_index ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                    desc.InstanceDataStepRate = el.use_instance_index ? 1u : 0u;
                    
                    elements.push_back(desc);
                } else {
                    WARN(LogRHI, "Vertex shader expects semantic '{}' but no matching vertex element provided", param_desc.SemanticName);
                }
            }
        } else {
            // Fallback: use vertex elements directly (manual mode)
            for (const auto& el : info_.vertex_input_state.vertex_elements) {
                D3D11_INPUT_ELEMENT_DESC desc = {};
                desc.SemanticName = el.semantic_name.c_str();
                desc.SemanticIndex = el.semantic_index;
                desc.Format = DX11Util::rhi_format_to_dxgi(el.format);
                desc.InputSlot = el.stream_index;
                desc.AlignedByteOffset = el.offset;
                desc.InputSlotClass = el.use_instance_index ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                desc.InstanceDataStepRate = el.use_instance_index ? 1u : 0u;
                elements.push_back(desc);
            }
        }
        
        if (!elements.empty()) {
            backend_.get_device()->CreateInputLayout(elements.data(), (UINT)elements.size(), 
                                                     vs->get_info().code.data(), vs->get_info().code.size(), 
                                                     input_layout_.GetAddressOf());
        }
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
    
    // Set topology from pipeline info instead of hardcoding
    topology_ = DX11Util::primitive_type_to_dx11(info_.primitive_type);

    return true;
}

void DX11GraphicsPipeline::bind(ID3D11DeviceContext* context) {
    // Clear previous shader bindings to avoid resource conflicts
    ID3D11ShaderResourceView* null_srvs[16] = {};
    context->VSSetShaderResources(0, 16, null_srvs);
    context->PSSetShaderResources(0, 16, null_srvs);
    
    auto vs = resource_cast(info_.vertex_shader);
    auto ps = resource_cast(info_.fragment_shader);
    if (vs) context->VSSetShader((ID3D11VertexShader*)vs->get_shader().Get(), nullptr, 0);
    else context->VSSetShader(nullptr, nullptr, 0);
    if (ps) context->PSSetShader((ID3D11PixelShader*)ps->get_shader().Get(), nullptr, 0);
    else context->PSSetShader(nullptr, nullptr, 0);
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
    while (backend_.get_context()->GetData(query_.Get(), nullptr, 0, 0) == S_FALSE) {
        Sleep(0);
    }
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
    // Shutdown ImGui if initialized
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    
    // Release immediate context wrapper first (it holds references to backend)
    immediate_context_.reset();
    
    // Release D3D11 resources
    context_.Reset(); 
    device_.Reset(); 
    factory_.Reset(); 
}

void DX11Backend::set_name(RHIResourceRef resource, const std::string& name) {
    if (!resource) return;
    resource->set_name(name);
    
    ID3D11DeviceChild* child = static_cast<ID3D11DeviceChild*>(resource->raw_handle());
    if (child) {
        child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}
void DX11Backend::init_imgui(void* window_handle) {
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Setup style
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(window_handle);
    ImGui_ImplDX11_Init(device_.Get(), context_.Get());
    
    INFO(LogRHI, "ImGui initialized successfully");
}

void DX11Backend::imgui_new_frame() {
    // Order matters: Platform first, then Renderer, then NewFrame
    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
}

void DX11Backend::imgui_render() {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0) {
        // Set back buffer as render target for ImGui
        auto swapchain = EngineContext::render_system()->get_swapchain();
        if (swapchain) {
            auto dx_swapchain = std::static_pointer_cast<DX11Swapchain>(swapchain);
            // Before present(), the current index is the one we just rendered to
            ID3D11RenderTargetView* rtv = dx_swapchain->get_back_buffer_rtv(dx_swapchain->get_current_frame_index());
            if (rtv) {
                context_->OMSetRenderTargets(1, &rtv, nullptr);
                
                // Set viewport to full screen
                D3D11_VIEWPORT vp;
                vp.TopLeftX = 0;
                vp.TopLeftY = 0;
                vp.Width = (float)swapchain->get_extent().width;
                vp.Height = (float)swapchain->get_extent().height;
                vp.MinDepth = 0.0f;
                vp.MaxDepth = 1.0f;
                context_->RSSetViewports(1, &vp);
            }
        }
        
        ImGui_ImplDX11_RenderDrawData(draw_data);
    }
}

void DX11Backend::imgui_shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
RHIQueueRef DX11Backend::get_queue(const RHIQueueInfo& info) { return std::make_shared<DX11Queue>(info); }

RHISurfaceRef DX11Backend::create_surface(void* native_window_handle) {
    HWND hwnd = static_cast<HWND>(native_window_handle);
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
    context_->Flush();
}
void DX11CommandContext::texture_barrier(const RHITextureBarrier& b) {}
void DX11CommandContext::buffer_barrier(const RHIBufferBarrier& b) {}
void DX11CommandContext::copy_texture_to_buffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff) {
    // CopyResource cannot be used between different resource types (texture vs buffer)
    // We need to create a staging texture, copy to it, then copy data to buffer
    auto* dx11_texture = static_cast<DX11Texture*>(s.get());
    auto* dx11_buffer = static_cast<DX11Buffer*>(d.get());
    if (!dx11_texture || !dx11_buffer) return;
    
    ID3D11Texture2D* src_texture = dx11_texture->get_handle().Get();
    if (!src_texture) return;
    
    // Get texture desc for the subresource we want to copy
    const auto& tex_info = dx11_texture->get_info();
    uint32_t width = tex_info.extent.width >> ss.mip_level;
    uint32_t height = tex_info.extent.height >> ss.mip_level;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    uint32_t row_pitch = width * 4; // Assuming RGBA8 format
    uint32_t aligned_row_pitch = (row_pitch + 255) & ~255;
    uint32_t total_size = aligned_row_pitch * height;
    
    // Create staging texture for this specific subresource
    D3D11_TEXTURE2D_DESC staging_desc = {};
    staging_desc.Width = width;
    staging_desc.Height = height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DX11Util::rhi_format_to_dxgi(tex_info.format);
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    ComPtr<ID3D11Texture2D> staging_texture;
    HRESULT hr = backend_.get_device()->CreateTexture2D(&staging_desc, nullptr, staging_texture.GetAddressOf());
    if (FAILED(hr)) return;
    
    // Calculate source subresource index
    uint32_t src_subresource = D3D11CalcSubresource(ss.mip_level, ss.base_array_layer, tex_info.mip_levels);
    
    // Copy from source texture to staging texture
    context_->CopySubresourceRegion(staging_texture.Get(), 0, 0, 0, 0, src_texture, src_subresource, nullptr);
    
    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;
    
    // Get buffer info to check if we can map it directly
    // For GPU_DEFAULT buffers, we need to use UpdateSubresource
    // For CPU accessible buffers, we can map directly
    const auto& buf_info = dx11_buffer->get_info();
    if (buf_info.memory_usage == MEMORY_USAGE_CPU_ONLY || 
        buf_info.memory_usage == MEMORY_USAGE_CPU_TO_GPU) {
        // Buffer is CPU accessible, map it
        D3D11_MAPPED_SUBRESOURCE buf_mapped;
        hr = context_->Map(dx11_buffer->get_handle().Get(), 0, D3D11_MAP_WRITE, 0, &buf_mapped);
        if (SUCCEEDED(hr)) {
            uint8_t* dst = static_cast<uint8_t*>(buf_mapped.pData) + doff;
            uint8_t* src = static_cast<uint8_t*>(mapped.pData);
            
            // Copy row by row handling pitch differences
            for (uint32_t row = 0; row < height; row++) {
                memcpy(dst + row * row_pitch, src + row * mapped.RowPitch, row_pitch);
            }
            context_->Unmap(dx11_buffer->get_handle().Get(), 0);
        }
    } else {
        // GPU_DEFAULT buffer, use UpdateSubresource via staging approach
        // Create a temporary staging buffer
        RHIBufferInfo staging_buf_info = {};
        staging_buf_info.size = width * height * 4;
        staging_buf_info.type = RESOURCE_TYPE_BUFFER;
        staging_buf_info.memory_usage = MEMORY_USAGE_CPU_ONLY;
        
        D3D11_BUFFER_DESC buf_desc = {};
        buf_desc.ByteWidth = width * height * 4;
        buf_desc.Usage = D3D11_USAGE_STAGING;
        buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        
        ComPtr<ID3D11Buffer> staging_buffer;
        hr = backend_.get_device()->CreateBuffer(&buf_desc, nullptr, staging_buffer.GetAddressOf());
        if (SUCCEEDED(hr)) {
            // Copy data to staging buffer
            D3D11_MAPPED_SUBRESOURCE buf_mapped;
            hr = context_->Map(staging_buffer.Get(), 0, D3D11_MAP_WRITE, 0, &buf_mapped);
            if (SUCCEEDED(hr)) {
                uint8_t* dst = static_cast<uint8_t*>(buf_mapped.pData);
                uint8_t* src = static_cast<uint8_t*>(mapped.pData);
                
                for (uint32_t row = 0; row < height; row++) {
                    memcpy(dst + row * row_pitch, src + row * mapped.RowPitch, row_pitch);
                }
                context_->Unmap(staging_buffer.Get(), 0);
                
                // Copy from staging buffer to destination buffer
                context_->CopySubresourceRegion(
                    dx11_buffer->get_handle().Get(),
                    0,
                    (UINT)doff, 0, 0,
                    staging_buffer.Get(),
                    0,
                    nullptr
                );
            }
        }
    }
    
    context_->Unmap(staging_texture.Get(), 0);
}
void DX11CommandContext::copy_buffer_to_texture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds) {
    // For buffer to texture copy in DX11, we need to use UpdateSubresource
    // since CopyResource requires resources of the same type.
    auto* dx11_buffer = static_cast<DX11Buffer*>(s.get());
    auto* dx11_texture = static_cast<DX11Texture*>(d.get());
    
    if (!dx11_buffer || !dx11_texture) return;
    
    // Map the source buffer to access data
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(dx11_buffer->get_handle().Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;
    
    // Get texture info for proper row pitch calculation
    const auto& tex_info = dx11_texture->get_info();
    uint32_t width = tex_info.extent.width;
    uint32_t height = tex_info.extent.height;
    uint32_t bpp = 4; // Assuming RGBA8 format
    
    // Calculate row pitch with 256-byte alignment (as done in texture.cpp)
    uint32_t row_pitch = width * bpp;
    uint32_t aligned_row_pitch = (row_pitch + 255) & ~255;
    
    // Use UpdateSubresource to copy data to the specific subresource
    uint32_t dst_subresource = D3D11CalcSubresource(ds.mip_level, ds.base_array_layer, tex_info.mip_levels);
    
    // Use D3D11_BOX to specify the region
    D3D11_BOX box = {};
    box.left = 0;
    box.right = width;
    box.top = 0;
    box.bottom = height;
    box.front = 0;
    box.back = 1;
    
    context_->UpdateSubresource(
        dx11_texture->get_handle().Get(),
        dst_subresource,
        &box,
        mapped.pData,
        aligned_row_pitch,
        aligned_row_pitch * height
    );
    
    context_->Unmap(dx11_buffer->get_handle().Get(), 0);
}
void DX11CommandContext::copy_buffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz) { D3D11_BOX box = { (UINT)soff, 0, 0, (UINT)(soff + sz), 1, 1 }; context_->CopySubresourceRegion((ID3D11Resource*)d->raw_handle(), 0, (UINT)doff, 0, 0, (ID3D11Resource*)s->raw_handle(), 0, &box); }
void DX11CommandContext::copy_texture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds) {
    // Use CopySubresourceRegion to support subresource-level copy
    auto* src_tex = static_cast<DX11Texture*>(s.get());
    auto* dst_tex = static_cast<DX11Texture*>(d.get());
    if (!src_tex || !dst_tex) return;
    
    const auto& src_info = src_tex->get_info();
    const auto& dst_info = dst_tex->get_info();
    
    uint32_t src_subresource = D3D11CalcSubresource(ss.mip_level, ss.base_array_layer, src_info.mip_levels);
    uint32_t dst_subresource = D3D11CalcSubresource(ds.mip_level, ds.base_array_layer, dst_info.mip_levels);
    
    context_->CopySubresourceRegion(
        dst_tex->get_handle().Get(),
        dst_subresource,
        0, 0, 0, // dst x, y, z
        src_tex->get_handle().Get(),
        src_subresource,
        nullptr // copy entire subresource
    );
}
void DX11CommandContext::generate_mips(RHITextureRef s) {
    auto texture = std::static_pointer_cast<DX11Texture>(s);
    if (!texture) return;
    
    auto srv = texture->get_srv();
    if (!srv) {
        srv = texture->create_srv();
    }
    if (!srv) {
        ERR(LogRHI, "Failed to get SRV for GenerateMips");
        return;
    }
    
    context_->GenerateMips(srv.Get());
}
void DX11CommandContext::push_event(const std::string& name, Color3 color) {
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    if (SUCCEEDED(context_.As(&annotation))) {
        std::wstring wname(name.begin(), name.end());
        annotation->BeginEvent(wname.c_str());
    }
}
void DX11CommandContext::pop_event() {
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    if (SUCCEEDED(context_.As(&annotation))) {
        annotation->EndEvent();
    }
}
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
        // Use read-only DSV if depth is read-only (allows simultaneous SRV binding)
        dsv = info.depth_stencil_attachment.read_only ? dx_view->get_dsv_read_only().Get() : dx_view->get_dsv().Get();
        
        UINT clear_flags = 0;
        if (info.depth_stencil_attachment.load_op == ATTACHMENT_LOAD_OP_CLEAR && !info.depth_stencil_attachment.read_only) {
            clear_flags |= D3D11_CLEAR_DEPTH;
            // if (has_stencil) clear_flags |= D3D11_CLEAR_STENCIL;
            context_->ClearDepthStencilView(dsv, clear_flags, info.depth_stencil_attachment.clear_depth, (UINT8)info.depth_stencil_attachment.clear_stencil);
        }
    }

    if (!rtvs.empty() || dsv) {
        context_->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), dsv);
    }
}
void DX11CommandContext::end_render_pass() {
    // Unbind render targets to prevent resource conflicts
    ID3D11RenderTargetView* null_rtvs[MAX_RENDER_TARGETS] = {};
    ID3D11DepthStencilView* null_dsv = nullptr;
    context_->OMSetRenderTargets(MAX_RENDER_TARGETS, null_rtvs, null_dsv);
}
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
void DX11CommandContext::bind_texture(RHITextureRef t, uint32_t s, ShaderFrequency f) {
    auto* dx11_texture = static_cast<DX11Texture*>(t.get());
    if (!dx11_texture) return;
    
    // Create or get SRV
    auto srv = dx11_texture->get_srv();
    if (!srv) {
        srv = dx11_texture->create_srv();
    }
    if (!srv) return;
    
    ID3D11ShaderResourceView* srv_ptr = srv.Get();
    if (f & SHADER_FREQUENCY_VERTEX) context_->VSSetShaderResources(s, 1, &srv_ptr);
    if (f & SHADER_FREQUENCY_FRAGMENT) context_->PSSetShaderResources(s, 1, &srv_ptr);
    if (f & SHADER_FREQUENCY_COMPUTE) context_->CSSetShaderResources(s, 1, &srv_ptr);
}
void DX11CommandContext::bind_sampler(RHISamplerRef s, uint32_t slot, ShaderFrequency f) {
    auto* dx11_sampler = static_cast<DX11Sampler*>(s.get());
    if (!dx11_sampler) return;
    
    ID3D11SamplerState* sampler = (ID3D11SamplerState*)s->raw_handle();
    if (f & SHADER_FREQUENCY_VERTEX) context_->VSSetSamplers(slot, 1, &sampler);
    if (f & SHADER_FREQUENCY_FRAGMENT) context_->PSSetSamplers(slot, 1, &sampler);
    if (f & SHADER_FREQUENCY_COMPUTE) context_->CSSetSamplers(slot, 1, &sampler);
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
void DX11CommandContext::imgui_create_fonts_texture() {
    ImGui_ImplDX11_CreateDeviceObjects();
}

void DX11CommandContext::imgui_render_draw_data() {
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

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

// DX11CommandContextImmediate implementation

DX11CommandContextImmediate::DX11CommandContextImmediate(DX11Backend& backend) : backend_(backend) {
}

ID3D11DeviceContext* DX11CommandContextImmediate::get_context() {
    return backend_.get_context().Get();
}

void DX11CommandContextImmediate::flush() {
    // Flush immediate context to ensure all commands are submitted to GPU
    backend_.get_context()->Flush();
}

void DX11CommandContextImmediate::texture_barrier(const RHITextureBarrier& b) {
    // DX11 doesn't need explicit texture barriers like Vulkan/DX12
    // Resources are automatically transitioned when used
    (void)b; // Unused in DX11
}

void DX11CommandContextImmediate::buffer_barrier(const RHIBufferBarrier& b) {
    // DX11 doesn't need explicit buffer barriers
    (void)b; // Unused in DX11
}

void DX11CommandContextImmediate::copy_texture_to_buffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff) {
    // Note: This operation requires staging due to DX11 limitations
    // We create a staging texture, copy GPU data to it, read back to CPU, then upload to buffer
    
    auto* dx11_texture = static_cast<DX11Texture*>(s.get());
    auto* dx11_buffer = static_cast<DX11Buffer*>(d.get());
    if (!dx11_texture || !dx11_buffer) return;
    
    ID3D11Texture2D* src_texture = dx11_texture->get_handle().Get();
    if (!src_texture) return;
    
    // Get texture info for the subresource we want to copy
    const auto& tex_info = dx11_texture->get_info();
    uint32_t width = tex_info.extent.width >> ss.mip_level;
    uint32_t height = tex_info.extent.height >> ss.mip_level;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    uint32_t row_pitch = width * 4; // Assuming RGBA8 format
    uint32_t aligned_row_pitch = (row_pitch + 255) & ~255;
    
    // Create staging texture for readback
    D3D11_TEXTURE2D_DESC staging_desc = {};
    staging_desc.Width = width;
    staging_desc.Height = height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DX11Util::rhi_format_to_dxgi(tex_info.format);
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    ComPtr<ID3D11Texture2D> staging_texture;
    HRESULT hr = backend_.get_device()->CreateTexture2D(&staging_desc, nullptr, staging_texture.GetAddressOf());
    if (FAILED(hr)) return;
    
    ID3D11DeviceContext* ctx = get_context();
    
    uint32_t src_subresource = D3D11CalcSubresource(ss.mip_level, ss.base_array_layer, tex_info.mip_levels);
    ctx->CopySubresourceRegion(staging_texture.Get(), 0, 0, 0, 0, src_texture, src_subresource, nullptr);
    
    // Read back from staging texture using immediate context
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;
    
    uint8_t* temp_data = new uint8_t[width * height * 4];
    for (uint32_t row = 0; row < height; row++) {
        memcpy(temp_data + row * row_pitch, (uint8_t*)mapped.pData + row * mapped.RowPitch, row_pitch);
    }
    ctx->Unmap(staging_texture.Get(), 0);
    
    // Update destination buffer
    D3D11_BOX dst_box = {};
    dst_box.left = (UINT)doff;
    dst_box.right = (UINT)(doff + width * height * 4);
    dst_box.top = 0;
    dst_box.bottom = 1;
    dst_box.front = 0;
    dst_box.back = 1;
    
    ctx->UpdateSubresource(
        dx11_buffer->get_handle().Get(), 0, &dst_box, temp_data, row_pitch, row_pitch);
    
    delete[] temp_data;
}

void DX11CommandContextImmediate::copy_buffer_to_texture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds) {
    auto* dx11_buffer = static_cast<DX11Buffer*>(s.get());
    auto* dx11_texture = static_cast<DX11Texture*>(d.get());
    
    if (!dx11_buffer || !dx11_texture) return;
    
    ID3D11DeviceContext* ctx = get_context();
    
    // Map the source buffer to get CPU pointer
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(dx11_buffer->get_handle().Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;
    
    const auto& tex_info = dx11_texture->get_info();
    uint32_t width = tex_info.extent.width;
    uint32_t height = tex_info.extent.height;
    uint32_t bpp = 4; // Assuming RGBA8 format
    
    uint32_t row_pitch = width * bpp;
    uint32_t aligned_row_pitch = (row_pitch + 255) & ~255;
    uint32_t dst_subresource = D3D11CalcSubresource(ds.mip_level, ds.base_array_layer, tex_info.mip_levels);
    
    D3D11_BOX box = {};
    box.left = 0;
    box.right = width;
    box.top = 0;
    box.bottom = height;
    box.front = 0;
    box.back = 1;
    
    // Copy data from mapped buffer to texture
    uint8_t* src_data = static_cast<uint8_t*>(mapped.pData) + soff;
    ctx->UpdateSubresource(
        dx11_texture->get_handle().Get(),
        dst_subresource,
        &box,
        src_data,
        aligned_row_pitch,
        aligned_row_pitch * height
    );
    
    ctx->Unmap(dx11_buffer->get_handle().Get(), 0);
}

void DX11CommandContextImmediate::copy_buffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz) {
    ID3D11DeviceContext* ctx = get_context();
    
    D3D11_BOX box = { (UINT)soff, 0, 0, (UINT)(soff + sz), 1, 1 };
    ctx->CopySubresourceRegion(
        (ID3D11Resource*)d->raw_handle(), 0, (UINT)doff, 0, 0,
        (ID3D11Resource*)s->raw_handle(), 0, &box
    );
}

void DX11CommandContextImmediate::copy_texture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds) {
    auto* src_tex = static_cast<DX11Texture*>(s.get());
    auto* dst_tex = static_cast<DX11Texture*>(d.get());
    if (!src_tex || !dst_tex) return;
    
    ID3D11DeviceContext* ctx = get_context();
    
    const auto& src_info = src_tex->get_info();
    const auto& dst_info = dst_tex->get_info();
    
    uint32_t src_subresource = D3D11CalcSubresource(ss.mip_level, ss.base_array_layer, src_info.mip_levels);
    uint32_t dst_subresource = D3D11CalcSubresource(ds.mip_level, ds.base_array_layer, dst_info.mip_levels);
    
    ctx->CopySubresourceRegion(
        dst_tex->get_handle().Get(), dst_subresource, 0, 0, 0,
        src_tex->get_handle().Get(), src_subresource, nullptr
    );
}

void DX11CommandContextImmediate::generate_mips(RHITextureRef s) {
    auto texture = std::static_pointer_cast<DX11Texture>(s);
    if (!texture) return;
    
    ID3D11DeviceContext* ctx = get_context();
    
    auto srv = texture->get_srv();
    if (!srv) {
        srv = texture->create_srv();
    }
    if (!srv) {
        ERR(LogRHI, "Failed to get SRV for GenerateMips");
        return;
    }
    
    ctx->GenerateMips(srv.Get());
}