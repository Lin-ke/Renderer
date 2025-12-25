#ifndef RHI_DRIVER_H
#define RHI_DRIVER_H

// act like UE FDynamicRHI

enum RHIDeviceType
{
    BACKEND_DX11 = 0,
    BACKEND_DX12,

    BACKEND_MAX_ENUM,    //
};

typedef struct RHIDeviceInfo
{
    RHIDeviceType type;

    bool enableDebug = false;
    bool enableRayTracing;

} RHIDeviceInfo;

class RHIDevice{
    virtual void init() = 0;
    virtual void draw() = 0;
    
};

#endif 