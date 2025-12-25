#pragma once

namespace Engine {
    class RHI {
    public:
        virtual ~RHI() = default;

        virtual void init(void* window_handle) = 0;
        virtual void draw_triangle_test() = 0;
        virtual void present() = 0;
    };
}
