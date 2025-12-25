#include "dx11_rhi.h"
#include "engine/core/log/Log.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace Engine {

    // Simple vertex structure for the triangle
    // 简单的三角形顶点结构
    struct Vertex {
        float x, y, z;      // Position / 位置
        float r, g, b, a;   // Color / 颜色
    };

    DX11RHI::DX11RHI() {}

    DX11RHI::~DX11RHI() {
        // ComPtr handles cleanup automatically, but explicit cleanup can be done here if needed.
        // ComPtr 会自动处理清理，但如果需要，可以在这里显式清理。
    }

    void DX11RHI::init(void* window_handle) {
        HWND hwnd = static_cast<HWND>(window_handle);
        
        // Retrieve window client area dimensions
        // 获取窗口客户区尺寸
        RECT rc;
        GetClientRect(hwnd, &rc);
        width_ = rc.right - rc.left;
        height_ = rc.bottom - rc.top;

        LOG("Initializing DX11 RHI with width: {}, height: {}", width_, height_);

        create_device_and_swap_chain(hwnd);
        create_render_target_view();
        set_viewport(width_, height_);
        compile_shaders();
        create_buffers();
    }

    void DX11RHI::create_device_and_swap_chain(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1; // Double buffering / 双重缓冲
        sd.BufferDesc.Width = width_;
        sd.BufferDesc.Height = height_;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;

        D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL feature_level;

        UINT createDeviceFlags = 0;
#ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        // Create Device, Device Context and Swap Chain
        // 创建设备、设备上下文和交换链
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags, // D3D11_CREATE_DEVICE_DEBUG for debug layer
            feature_levels,
            1,
            D3D11_SDK_VERSION,
            &sd,
            swap_chain_.GetAddressOf(),
            device_.GetAddressOf(),
            &feature_level,
            context_.GetAddressOf()
        );

        if (FAILED(hr)) {
            FATAL("Failed to create D3D11 device and swap chain!");
        } else {
            LOG("D3D11 Device and Swap Chain created successfully.");
        }
    }

    void DX11RHI::create_render_target_view() {
        // Get the back buffer from the swap chain
        // 从交换链获取后备缓冲
        Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
        HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back_buffer.GetAddressOf());
        if (FAILED(hr)) {
            FATAL("Failed to get back buffer!");
            return;
        }

        // Create Render Target View
        // 创建渲染目标视图
        hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, render_target_view_.GetAddressOf());
        if (FAILED(hr)) {
            FATAL("Failed to create Render Target View!");
        }
    }

    void DX11RHI::set_viewport(int width, int height) {
        D3D11_VIEWPORT vp;
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        context_->RSSetViewports(1, &vp);
    }

    void DX11RHI::compile_shaders() {
        // Basic Vertex Shader Source
        // 基础顶点着色器源码
        const char* vs_src = R"(
            struct VS_INPUT {
                float3 pos : POSITION;
                float4 color : COLOR;
            };
            struct VS_OUTPUT {
                float4 pos : SV_POSITION;
                float4 color : COLOR;
            };
            VS_OUTPUT main(VS_INPUT input) {
                VS_OUTPUT output;
                output.pos = float4(input.pos, 1.0f);
                output.color = input.color;
                return output;
            }
        )";

        // Basic Pixel Shader Source
        // 基础像素着色器源码
        const char* ps_src = R"(
            struct PS_INPUT {
                float4 pos : SV_POSITION;
                float4 color : COLOR;
            };
            float4 main(PS_INPUT input) : SV_TARGET {
                return input.color;
            }
        )";

        Microsoft::WRL::ComPtr<ID3DBlob> vs_blob, ps_blob, error_blob;

        // Compile Vertex Shader
        // 编译顶点着色器
        HRESULT hr = D3DCompile(vs_src, strlen(vs_src), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vs_blob.GetAddressOf(), error_blob.GetAddressOf());
        if (FAILED(hr)) {
            if (error_blob) FATAL("VS Compile Error: {}", (char*)error_blob->GetBufferPointer());
            return;
        }

        // Create Vertex Shader
        // 创建顶点着色器
        hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vertex_shader_.GetAddressOf());
        if (FAILED(hr)) FATAL("Failed to create Vertex Shader!");

        // Compile Pixel Shader
        // 编译像素着色器
        hr = D3DCompile(ps_src, strlen(ps_src), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, ps_blob.GetAddressOf(), error_blob.GetAddressOf());
        if (FAILED(hr)) {
            if (error_blob) FATAL("PS Compile Error: {}", (char*)error_blob->GetBufferPointer());
            return;
        }

        // Create Pixel Shader
        // 创建像素着色器
        hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, pixel_shader_.GetAddressOf());
        if (FAILED(hr)) FATAL("Failed to create Pixel Shader!");

        // Create Input Layout
        // 创建输入布局
        D3D11_INPUT_ELEMENT_DESC ied[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = device_->CreateInputLayout(ied, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), input_layout_.GetAddressOf());
        if (FAILED(hr)) FATAL("Failed to create Input Layout!");
    }

    void DX11RHI::create_buffers() {
        // Triangle vertices (Position + Color)
        // 三角形顶点 (位置 + 颜色)
        Vertex vertices[] = {
            { 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f }, // Top Red
            { 0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f }, // Bottom Right Green
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }, // Bottom Left Blue
        };

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(Vertex) * 3;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA init_data = {};
        init_data.pSysMem = vertices;

        HRESULT hr = device_->CreateBuffer(&bd, &init_data, vertex_buffer_.GetAddressOf());
        if (FAILED(hr)) FATAL("Failed to create Vertex Buffer!");
    }

    void DX11RHI::draw_triangle_test() {
        // Bind Render Target View
        // 绑定渲染目标视图
        context_->OMSetRenderTargets(1, render_target_view_.GetAddressOf(), nullptr);

        // Clear the back buffer to a deep blue
        // 清除后备缓冲为深蓝色
        float clear_color[] = { 0.1f, 0.1f, 0.2f, 1.0f };
        context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);

        // Bind Shaders and Input Layout
        // 绑定着色器和输入布局
        context_->IASetInputLayout(input_layout_.Get());
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);

        // Bind Vertex Buffer
        // 绑定顶点缓冲
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, vertex_buffer_.GetAddressOf(), &stride, &offset);

        // Set Topology
        // 设置图元拓扑
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Draw
        // 绘制
        context_->Draw(3, 0);
    }

    void DX11RHI::present() {
        // Present the back buffer
        // 呈现后备缓冲
        swap_chain_->Present(1, 0); // VSync enabled
    }
}
