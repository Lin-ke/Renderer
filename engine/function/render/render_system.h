#pragma once

#include "engine/function/render/rhi/rhi.h"
#include <memory>

class RenderSystem {
public:
    RenderSystem() = default;
    ~RenderSystem() = default;
    void init(void* window_handle);
    void tick();
    
    // Test function to draw a triangle
    void draw_triangle_test();
    RHI* get_rhi() const { return rhi_.get(); }

private:
    std::shared_ptr<RHI> rhi_;

    
};
