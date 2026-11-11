#pragma once
#include "engine/function/render/rhi/rhi_structs.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d12shader.h>
#include <wrl/client.h>

inline ShaderReflectInfo reflect_dx_shader(const std::vector<uint8_t>& code, ShaderFrequency frequency) {
    ShaderReflectInfo info = {};
    info.frequency = frequency;
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection12;
    if (SUCCEEDED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection12)))) {
        D3D12_SHADER_DESC desc = {};
        reflection12->GetDesc(&desc);
        for (uint32_t i = 0; i < desc.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC resource = {};
            reflection12->GetResourceBindingDesc(i, &resource);
            ResourceType type = RESOURCE_TYPE_NONE;
            switch (resource.Type) {
                case D3D_SIT_CBUFFER: type = RESOURCE_TYPE_UNIFORM_BUFFER; break;
                case D3D_SIT_SAMPLER: type = RESOURCE_TYPE_SAMPLER; break;
                case D3D_SIT_TEXTURE: type = resource.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE ? RESOURCE_TYPE_TEXTURE_CUBE : RESOURCE_TYPE_TEXTURE; break;
                case D3D_SIT_STRUCTURED: case D3D_SIT_BYTEADDRESS: type = RESOURCE_TYPE_BUFFER; break;
                case D3D_SIT_UAV_RWTYPED: type = RESOURCE_TYPE_RW_TEXTURE; break;
                case D3D_SIT_UAV_RWSTRUCTURED: case D3D_SIT_UAV_RWBYTEADDRESS: type = RESOURCE_TYPE_RW_BUFFER; break;
                default: break;
            }
            if (type != RESOURCE_TYPE_NONE)
                info.resources.push_back({.set = resource.Space, .binding = resource.BindPoint, .size = resource.BindCount, .frequency = frequency, .type = type});
        }
        if (frequency == SHADER_FREQUENCY_COMPUTE)
            reflection12->GetThreadGroupSize(&info.local_size_x, &info.local_size_y, &info.local_size_z);
        return info;
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection)))) return info;
    D3D11_SHADER_DESC desc = {};
    reflection->GetDesc(&desc);
    for (uint32_t i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC resource = {};
        reflection->GetResourceBindingDesc(i, &resource);
        ResourceType type = RESOURCE_TYPE_NONE;
        switch (resource.Type) {
            case D3D_SIT_CBUFFER: type = RESOURCE_TYPE_UNIFORM_BUFFER; break;
            case D3D_SIT_SAMPLER: type = RESOURCE_TYPE_SAMPLER; break;
            case D3D_SIT_TEXTURE: type = resource.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE ? RESOURCE_TYPE_TEXTURE_CUBE : RESOURCE_TYPE_TEXTURE; break;
            case D3D_SIT_STRUCTURED: case D3D_SIT_BYTEADDRESS: type = RESOURCE_TYPE_BUFFER; break;
            case D3D_SIT_UAV_RWTYPED: type = RESOURCE_TYPE_RW_TEXTURE; break;
            case D3D_SIT_UAV_RWSTRUCTURED: case D3D_SIT_UAV_RWBYTEADDRESS: type = RESOURCE_TYPE_RW_BUFFER; break;
            default: break;
        }
        if (type != RESOURCE_TYPE_NONE)
            info.resources.push_back({.binding = resource.BindPoint, .size = resource.BindCount, .frequency = frequency, .type = type});
    }
    if (frequency == SHADER_FREQUENCY_COMPUTE)
        reflection->GetThreadGroupSize(&info.local_size_x, &info.local_size_y, &info.local_size_z);
    return info;
}
