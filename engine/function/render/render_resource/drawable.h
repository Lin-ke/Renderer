#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include <vector>

// Forward declarations
struct DrawBatch;
struct RHIAccelerationStructureInstanceInfo;
struct SurfaceCacheTask;

class Drawable {
public:
    virtual void collect_draw_batch(std::vector<DrawBatch>& batches) = 0;
    virtual void collect_acceleration_structure_instance(std::vector<RHIAccelerationStructureInstanceInfo>& instances) {}
    virtual void collect_surface_cache_task(std::vector<SurfaceCacheTask>& tasks) {}
};
