#pragma once

#include "engine/function/render/rhi/rhi.h"
#include <memory>

namespace Engine {
    class RenderSystem {
    public:
        RenderSystem() = default;
        ~RenderSystem() = default;

        void initialize(void* window_handle);
        void tick();
        
        // Test function to draw a triangle
        void draw_triangle_test();

    private:
        std::shared_ptr<RHI> rhi_;
    };
}
