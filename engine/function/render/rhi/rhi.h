#pragma once

enum RHIBackendType
{
    BACKEND_DX11 = 0,

    BACKEND_MAX_ENUM,    //
};

typedef struct RHIBackendInfo
{
    RHIBackendType type;

    bool enableDebug = false;
    bool enableRayTracing;

}RHIBackendInfo;


class RHI {
public:
    virtual ~RHI() = default;

    virtual void init(void* window_handle) = 0;
    virtual void draw_triangle_test() = 0;
    virtual void present() = 0;
};

