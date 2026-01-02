#include "render_system.h"
#include "engine/platform/dx11/dx11_rhi.h"


void RenderSystem::init(void* window_handle) {
    // Create DX11 RHI instance
    // 创建 DX11 RHI 实例
    rhi_ = std::make_shared<DX11RHI>();
    rhi_->init(window_handle);
}

void RenderSystem::tick(const RenderPacket& packet) {
    if (rhi_) {
        rhi_->draw_triangle_test();
        rhi_->present();
    }
}

void RenderSystem::draw_triangle_test() {
    if (rhi_) {
        rhi_->draw_triangle_test();
    }
}
