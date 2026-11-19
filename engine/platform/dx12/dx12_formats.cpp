#include "platform_rhi.h"

DXGI_FORMAT DX12Backend::to_dxgi_format(RHIFormat format) {
    switch (format) {
        case FORMAT_R8_SRGB: return DXGI_FORMAT_R8_UNORM;
        case FORMAT_R8G8_SRGB: return DXGI_FORMAT_R8G8_UNORM;
        case FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case FORMAT_R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
        case FORMAT_R16G16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case FORMAT_R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
        case FORMAT_R32G32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case FORMAT_R32G32B32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case FORMAT_R32G32B32A32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case FORMAT_R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case FORMAT_R8_SNORM: return DXGI_FORMAT_R8_SNORM;
        case FORMAT_R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        case FORMAT_R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case FORMAT_R16_SNORM: return DXGI_FORMAT_R16_SNORM;
        case FORMAT_R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case FORMAT_R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case FORMAT_R8_UINT: return DXGI_FORMAT_R8_UINT;
        case FORMAT_R8G8_UINT: return DXGI_FORMAT_R8G8_UINT;
        case FORMAT_R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case FORMAT_R16_UINT: return DXGI_FORMAT_R16_UINT;
        case FORMAT_R16G16_UINT: return DXGI_FORMAT_R16G16_UINT;
        case FORMAT_R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
        case FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
        case FORMAT_R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
        case FORMAT_R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
        case FORMAT_R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case FORMAT_R8_SINT: return DXGI_FORMAT_R8_SINT;
        case FORMAT_R8G8_SINT: return DXGI_FORMAT_R8G8_SINT;
        case FORMAT_R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case FORMAT_R16_SINT: return DXGI_FORMAT_R16_SINT;
        case FORMAT_R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
        case FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case FORMAT_R32_SINT: return DXGI_FORMAT_R32_SINT;
        case FORMAT_R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
        case FORMAT_R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
        case FORMAT_R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        case FORMAT_D32_SFLOAT: return DXGI_FORMAT_D32_FLOAT;
        case FORMAT_D32_SFLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
        case FORMAT_BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
        case FORMAT_BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case FORMAT_BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT DX12Backend::to_dxgi_resource_format(RHIFormat format, ResourceType type) {
    if (format == FORMAT_D32_SFLOAT) return DXGI_FORMAT_R32_TYPELESS;
    if (format == FORMAT_D24_UNORM_S8_UINT) return DXGI_FORMAT_R24G8_TYPELESS;
    if (format == FORMAT_D32_SFLOAT_S8_UINT) return DXGI_FORMAT_R32G8X24_TYPELESS;
    if (type & RESOURCE_TYPE_RW_TEXTURE) {
        if (format == FORMAT_R8G8B8A8_SRGB) return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if (format == FORMAT_B8G8R8A8_SRGB) return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    }
    return to_dxgi_format(format);
}
DXGI_FORMAT DX12Backend::to_dxgi_srv_format(RHIFormat format) {
    if (format == FORMAT_D32_SFLOAT) return DXGI_FORMAT_R32_FLOAT;
    if (format == FORMAT_D24_UNORM_S8_UINT) return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    if (format == FORMAT_D32_SFLOAT_S8_UINT) return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    return to_dxgi_format(format);
}
DXGI_FORMAT DX12Backend::to_dxgi_dsv_format(RHIFormat format) { return to_dxgi_format(format); }
DXGI_FORMAT DX12Backend::to_dxgi_uav_format(RHIFormat format) {
    if (format == FORMAT_R8G8B8A8_SRGB) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if (format == FORMAT_B8G8R8A8_SRGB) return DXGI_FORMAT_B8G8R8A8_UNORM;
    return to_dxgi_format(format);
}
uint32_t DX12Backend::get_format_byte_size(RHIFormat format) {
    switch (format) {
        case FORMAT_R8_SRGB: return 1;
        case FORMAT_R8G8_SRGB: return 2;
        case FORMAT_R8G8B8_SRGB: return 3;
        case FORMAT_R8G8B8A8_SRGB: return 4;
        case FORMAT_B8G8R8A8_SRGB: return 4;
        case FORMAT_B8G8R8A8_UNORM: return 4;
        case FORMAT_R16_SFLOAT: return 2;
        case FORMAT_R16G16_SFLOAT: return 4;
        case FORMAT_R16G16B16_SFLOAT: return 6;
        case FORMAT_R16G16B16A16_SFLOAT: return 8;
        case FORMAT_R32_SFLOAT: return 4;
        case FORMAT_R32G32_SFLOAT: return 8;
        case FORMAT_R32G32B32_SFLOAT: return 12;
        case FORMAT_R32G32B32A32_SFLOAT: return 16;
        case FORMAT_R8_UNORM: return 1;
        case FORMAT_R8G8_UNORM: return 2;
        case FORMAT_R8G8B8_UNORM: return 3;
        case FORMAT_R8G8B8A8_UNORM: return 4;
        case FORMAT_R16_UNORM: return 2;
        case FORMAT_R16G16_UNORM: return 4;
        case FORMAT_R16G16B16_UNORM: return 6;
        case FORMAT_R16G16B16A16_UNORM: return 8;
        case FORMAT_R8_SNORM: return 1;
        case FORMAT_R8G8_SNORM: return 2;
        case FORMAT_R8G8B8_SNORM: return 3;
        case FORMAT_R8G8B8A8_SNORM: return 4;
        case FORMAT_R16_SNORM: return 2;
        case FORMAT_R16G16_SNORM: return 4;
        case FORMAT_R16G16B16_SNORM: return 6;
        case FORMAT_R16G16B16A16_SNORM: return 8;
        case FORMAT_R8_UINT: return 1;
        case FORMAT_R8G8_UINT: return 2;
        case FORMAT_R8G8B8_UINT: return 3;
        case FORMAT_R8G8B8A8_UINT: return 4;
        case FORMAT_R16_UINT: return 2;
        case FORMAT_R16G16_UINT: return 4;
        case FORMAT_R16G16B16_UINT: return 6;
        case FORMAT_R16G16B16A16_UINT: return 8;
        case FORMAT_R32_UINT: return 4;
        case FORMAT_R32G32_UINT: return 8;
        case FORMAT_R32G32B32_UINT: return 12;
        case FORMAT_R32G32B32A32_UINT: return 16;
        case FORMAT_R8_SINT: return 1;
        case FORMAT_R8G8_SINT: return 2;
        case FORMAT_R8G8B8_SINT: return 3;
        case FORMAT_R8G8B8A8_SINT: return 4;
        case FORMAT_R16_SINT: return 2;
        case FORMAT_R16G16_SINT: return 4;
        case FORMAT_R16G16B16_SINT: return 6;
        case FORMAT_R16G16B16A16_SINT: return 8;
        case FORMAT_R32_SINT: return 4;
        case FORMAT_R32G32_SINT: return 8;
        case FORMAT_R32G32B32_SINT: return 12;
        case FORMAT_R32G32B32A32_SINT: return 16;
        case FORMAT_D32_SFLOAT: return 4;
        case FORMAT_D32_SFLOAT_S8_UINT: return 8;
        case FORMAT_D24_UNORM_S8_UINT: return 4;
        default: return 0;
    }
}

D3D12_RESOURCE_STATES DX12Backend::to_d3d12_resource_state(RHIResourceState state) {
    switch (state) {
        case RESOURCE_STATE_TRANSFER_SRC: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RESOURCE_STATE_TRANSFER_DST: return D3D12_RESOURCE_STATE_COPY_DEST;
        case RESOURCE_STATE_VERTEX_BUFFER: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RESOURCE_STATE_INDEX_BUFFER: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case RESOURCE_STATE_COLOR_ATTACHMENT: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RESOURCE_STATE_DEPTH_STENCIL_READ: return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RESOURCE_STATE_UNORDERED_ACCESS: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RESOURCE_STATE_SHADER_RESOURCE: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RESOURCE_STATE_INDIRECT_ARGUMENT: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case RESOURCE_STATE_PRESENT: return D3D12_RESOURCE_STATE_PRESENT;
        case RESOURCE_STATE_ACCELERATION_STRUCTURE: return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        default: return D3D12_RESOURCE_STATE_COMMON;
    }
}
