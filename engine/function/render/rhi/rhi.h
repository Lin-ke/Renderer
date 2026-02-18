#pragma once

#include "engine/core/log/Log.h"
#include "engine/function/render/rhi/rhi_resource.h"
#include "engine/function/render/rhi/rhi_structs.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

enum RHIBackendType {
    BACKEND_VULKAN = 0,
    BACKEND_DX11,

    BACKEND_MAX_ENUM,
};

struct RHIBackendInfo {
    RHIBackendType type;

    bool enable_debug;
    bool enable_ray_tracing;
};

// RHI Backend Interface (DynamicRHI)
class RHIBackend {
private:
    static RHIBackendRef backend_;

public:
    static RHIBackendRef init(const RHIBackendInfo& info);

    static RHIBackendRef get() { return backend_; }
    
    static void reset_backend() { backend_.reset(); }

    virtual void tick(); // Update resource counters, clean up

    virtual void destroy();

    // ImGui
    virtual void init_imgui(GLFWwindow* window) = 0;
    virtual void imgui_new_frame() = 0;
    virtual void imgui_render() = 0;
    virtual void imgui_shutdown() = 0;

    // Basic Resources
    virtual RHIQueueRef get_queue(const RHIQueueInfo& info) = 0;

    virtual RHISurfaceRef create_surface(GLFWwindow* window) = 0;

    virtual RHISwapchainRef create_swapchain(const RHISwapchainInfo& info) = 0;

    virtual RHICommandPoolRef create_command_pool(const RHICommandPoolInfo& info) = 0;

    virtual RHICommandContextRef create_command_context(RHICommandPoolRef pool) = 0;

    // Buffers, Textures, Shaders, Acceleration Structures
    virtual RHIBufferRef create_buffer(const RHIBufferInfo& info) = 0;

    virtual RHITextureRef create_texture(const RHITextureInfo& info) = 0;

    virtual RHITextureViewRef create_texture_view(const RHITextureViewInfo& info) = 0;

    virtual RHISamplerRef create_sampler(const RHISamplerInfo& info) = 0;

    virtual RHIShaderRef create_shader(const RHIShaderInfo& info) = 0;

    virtual RHIShaderBindingTableRef create_shader_binding_table(const RHIShaderBindingTableInfo& info) = 0;

    virtual RHITopLevelAccelerationStructureRef create_top_level_acceleration_structure(const RHITopLevelAccelerationStructureInfo& info) = 0;

    virtual RHIBottomLevelAccelerationStructureRef create_bottom_level_acceleration_structure(const RHIBottomLevelAccelerationStructureInfo& info) = 0;

    // Root Signature, Descriptors
    virtual RHIRootSignatureRef create_root_signature(const RHIRootSignatureInfo& info) = 0;

    // Pipeline State
    virtual RHIRenderPassRef create_render_pass(const RHIRenderPassInfo& info) = 0;

    virtual RHIGraphicsPipelineRef create_graphics_pipeline(const RHIGraphicsPipelineInfo& info) = 0;

    virtual RHIComputePipelineRef create_compute_pipeline(const RHIComputePipelineInfo& info) = 0;

    virtual RHIRayTracingPipelineRef create_ray_tracing_pipeline(const RHIRayTracingPipelineInfo& info) = 0;

    // Synchronization
    virtual RHIFenceRef create_fence(bool signaled) = 0;

    virtual RHISemaphoreRef create_semaphore() = 0;

    // Immediate Command Interface
    virtual RHICommandContextImmediateRef get_immediate_command() = 0;

    // Shader Compilation
    /**
     * @brief Compile shader source code to platform-specific bytecode
     * @param source Shader source code (HLSL for DX11, GLSL for OpenGL/Vulkan)
     * @param entry Entry point function name (e.g., "main")
     * @param profile Shader profile/target (e.g., "vs_5_0", "ps_5_0" for DX11)
     * @return Compiled bytecode, empty vector if compilation fails
     */
    virtual std::vector<uint8_t> compile_shader(const char* source, const char* entry, const char* profile) = 0;

    const RHIBackendInfo& get_backend_info() const { return backend_info_; }

protected:
    RHIBackend() = delete;
    RHIBackend(const RHIBackendInfo& info) : backend_info_(info) {}

    void register_resource(RHIResourceRef resource) {
        resource_map_[resource->get_type()].push_back(resource);
    }

    std::array<std::vector<RHIResourceRef>, RHI_RESOURCE_TYPE_MAX_CNT> resource_map_;

    RHIBackendInfo backend_info_;
};

// Command Context Interface // vkcmd... 
class RHICommandContext : public RHIResource {
public:
    RHICommandContext(RHICommandPoolRef pool) : RHIResource(RHI_COMMAND_CONTEXT), pool_(pool) {}

    virtual void begin_command() = 0;

    virtual void end_command() = 0;

    virtual void execute(RHIFenceRef wait_fence, RHISemaphoreRef wait_semaphore, RHISemaphoreRef signal_semaphore) = 0;

    virtual void texture_barrier(const RHITextureBarrier& barrier) = 0;

    virtual void buffer_barrier(const RHIBufferBarrier& barrier) = 0;

    virtual void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) = 0;

    virtual void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) = 0;

    virtual void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) = 0;

    virtual void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) = 0;

    virtual void generate_mips(RHITextureRef src) = 0;

    virtual void push_event(const std::string& name, Color3 color) = 0;

    virtual void pop_event() = 0;

    virtual void begin_render_pass(RHIRenderPassRef render_pass) = 0;

    virtual void end_render_pass() = 0;

    virtual void set_viewport(Offset2D min, Offset2D max) = 0;

    virtual void set_scissor(Offset2D min, Offset2D max) = 0;

    virtual void set_depth_bias(float constant_bias, float slope_bias, float clamp_bias) = 0;

    virtual void set_line_width(float width) = 0;

    virtual void set_graphics_pipeline(RHIGraphicsPipelineRef graphics_pipeline) = 0;

    virtual void set_compute_pipeline(RHIComputePipelineRef compute_pipeline) = 0;

    virtual void set_ray_tracing_pipeline(RHIRayTracingPipelineRef ray_tracing_pipeline) = 0;

    virtual void push_constants(void* data, uint16_t size, ShaderFrequency frequency) = 0;

    virtual void bind_descriptor_set(RHIDescriptorSetRef descriptor, uint32_t set) = 0;

    virtual void bind_constant_buffer(RHIBufferRef buffer, uint32_t slot, ShaderFrequency frequency) = 0;

    virtual void bind_vertex_buffer(RHIBufferRef vertex_buffer, uint32_t stream_index, uint32_t offset) = 0;

    virtual void bind_index_buffer(RHIBufferRef index_buffer, uint32_t offset) = 0;

    virtual void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) = 0;

    virtual void dispatch_indirect(RHIBufferRef argument_buffer, uint32_t argument_offset) = 0;

    virtual void trace_rays(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) = 0;

    virtual void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) = 0;

    virtual void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) = 0;

    virtual void draw_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) = 0;

    virtual void draw_indexed_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) = 0;

    /**
     * @brief Read texture data back to CPU memory
     * @param texture The texture to read from
     * @param data Output buffer to receive pixel data (must be pre-allocated with sufficient size)
     * @param size Size of the output buffer in bytes
     * @return true if successful
     */
    virtual bool read_texture(RHITextureRef texture, void* data, uint32_t size) = 0;

    // ImGui
    virtual void imgui_create_fonts_texture() = 0;

    virtual void imgui_render_draw_data() = 0;

private:
    RHICommandPoolRef pool_;
};

// Immediate Command Context Interface
class RHICommandContextImmediate : public RHIResource {
public:
    RHICommandContextImmediate() : RHIResource(RHI_COMMAND_CONTEXT_IMMEDIATE) {}

    virtual void flush() = 0;

    virtual void texture_barrier(const RHITextureBarrier& barrier) = 0;

    virtual void buffer_barrier(const RHIBufferBarrier& barrier) = 0;

    virtual void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) = 0;

    virtual void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) = 0;

    virtual void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) = 0;

    virtual void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) = 0;

    virtual void generate_mips(RHITextureRef src) = 0;
};
