#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/render/rhi/rhi_command_list.h"
#include <algorithm>
#include <cmath>

Extent3D RHITexture::mip_extent(uint32_t mip_level) {
    Extent3D size = info_.extent;
    for (uint32_t i = 0; i < mip_level; ++i) {
        size.width = std::max(1u, size.width / 2);
        size.height = std::max(1u, size.height / 2);
        size.depth = std::max(1u, size.depth / 2);
    }
    return size;
}

RHICommandListRef RHICommandPool::create_command_list(bool bypass) {
    RHICommandContextRef context = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!idle_contexts_.empty()) {
            context = idle_contexts_.front();
            idle_contexts_.pop();
        }
    }

    if (!context) {
        context = RHIBackend::get()->create_command_context(std::static_pointer_cast<RHICommandPool>(shared_from_this()));
        contexts_.push_back(context);
    }

    CommandListInfo info;
    info.pool = std::static_pointer_cast<RHICommandPool>(shared_from_this());
    info.context = context;
    info.bypass = bypass;

    return std::make_shared<RHICommandList>(info);
}
