#include "engine/function/render/rhi/rhi.h"
#include "engine/core/log/Log.h"
#include "engine/platform/dx11/platform_rhi.h"
#include <algorithm>

RHIBackendRef RHIBackend::backend_ = nullptr;

class DummyRHIBackend : public RHIBackend {
public:
    DummyRHIBackend(const RHIBackendInfo& info) : RHIBackend(info) {}

    void init_imgui(GLFWwindow* window) override {}
    void imgui_new_frame() override {}
    void imgui_render() override {}
    void imgui_shutdown() override {}

    RHIQueueRef get_queue(const RHIQueueInfo& info) override { return nullptr; }
    RHISurfaceRef create_surface(GLFWwindow* window) override { return nullptr; }
    RHISwapchainRef create_swapchain(const RHISwapchainInfo& info) override { return nullptr; }
    RHICommandPoolRef create_command_pool(const RHICommandPoolInfo& info) override { return nullptr; }
    RHICommandContextRef create_command_context(RHICommandPoolRef pool) override { return nullptr; }

    RHIBufferRef create_buffer(const RHIBufferInfo& info) override { return nullptr; }
    RHITextureRef create_texture(const RHITextureInfo& info) override { return nullptr; }
    RHITextureViewRef create_texture_view(const RHITextureViewInfo& info) override { return nullptr; }
    RHISamplerRef create_sampler(const RHISamplerInfo& info) override { return nullptr; }
    RHIShaderRef create_shader(const RHIShaderInfo& info) override { return nullptr; }
    RHIShaderBindingTableRef create_shader_binding_table(const RHIShaderBindingTableInfo& info) override { return nullptr; }
    RHITopLevelAccelerationStructureRef create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo& info) override { return nullptr; }
    RHIBottomLevelAccelerationStructureRef create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo& info) override { return nullptr; }

    RHIRootSignatureRef create_root_signature(const RHIRootSignatureInfo& info) override { return nullptr; }

    RHIRenderPassRef create_render_pass(const RHIRenderPassInfo& info) override { return nullptr; }
    RHIGraphicsPipelineRef create_graphics_pipeline(const RHIGraphicsPipelineInfo& info) override { return nullptr; }
    RHIComputePipelineRef create_compute_pipeline(const RHIComputePipelineInfo& info) override { return nullptr; }
    RHIRayTracingPipelineRef create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo& info) override { return nullptr; }

    RHIFenceRef create_fence(bool signaled) override { return nullptr; }
    RHISemaphoreRef create_semaphore() override { return nullptr; }

    RHICommandContextImmediateRef get_immediate_command() override { return nullptr; }

    std::vector<uint8_t> compile_shader(const char* source, const char* entry, const char* profile) override { return {}; }
};

RHIBackendRef RHIBackend::init(const RHIBackendInfo& info) {
    if (backend_ == nullptr) {
        if (info.type == BACKEND_DX11) {
            backend_ = std::make_shared<DX11Backend>(info);
        } else {
            backend_ = std::make_shared<DummyRHIBackend>(info);
        }
    }
    return backend_;
}

void RHIBackend::tick() {
    for (auto& resources : resource_map_) {
        for (RHIResourceRef& resource : resources) {
            if (resource) {
                // Accessing private members of RHIResource (friend class RHIBackend)
                if(resource.use_count() == 1)   resource->last_use_tick_++;
                else                            resource->last_use_tick_ = 0;

                if(resource->last_use_tick_ > 6)
                {
                    resource->destroy();
                    resource = nullptr;
                }
            }
        }
        resources.erase(std::remove_if(resources.begin(), resources.end(), [](RHIResourceRef x) {
            return x == nullptr;
        }), resources.end());
    }
}

void RHIBackend::destroy() {
    for (int32_t i = resource_map_.size() - 1; i >= 0; i--) {
        auto& resources = resource_map_[i];
        for (RHIResourceRef& resource : resources) {
            if (resource) {
                resource->destroy();
            }
        }
    }
}
