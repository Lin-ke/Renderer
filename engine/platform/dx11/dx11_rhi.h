#pragma once
#include "engine/function/render/rhi/rhi.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <engine/function/render/rhi/rhi_device.h>
class RHIDeviceDX11 : public RHIDevice {
    bool enableDebug_;
    bool enableRayTracing_;
public:
    RHIDeviceDX11(const RHIDeviceInfo& info) {
        // Initialize DX11 specific settings based on info
        enableDebug_ = info.enableDebug;
        enableRayTracing_ = info.enableRayTracing;
    }
};

class DX11RHI : public RHI {
public:
    DX11RHI();
    ~DX11RHI() override;

    void init(void* window_handle) override;
    void draw_triangle_test() override;
    void present() override;

private:
    void create_device_and_swap_chain(HWND hwnd);
    void create_render_target_view();
    void compile_shaders();
    void create_buffers();
    void set_viewport(int width, int height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;
    
    // Resources for triangle test
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;

    int width_ = 800;
    int height_ = 600;
};

