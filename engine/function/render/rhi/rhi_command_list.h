#pragma once

#include "engine/configs.h"
#include "engine/function/render/rhi/rhi.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

struct RHICommandImmediate;
struct RHICommand;

struct CommandListImmediateInfo {
    RHICommandContextImmediateRef context;
};

struct CommandListInfo {
    RHICommandPoolRef pool;
    RHICommandContextRef context;

    bool bypass = true;
};

// RHICommand Base Structs

struct RHICommandImmediate {
    RHICommandImmediate() = default;
    virtual ~RHICommandImmediate() = default;
    virtual void execute(RHICommandContextImmediateRef context) = 0;
};

struct RHICommand {
    RHICommand() = default;
    virtual ~RHICommand() = default;
    virtual void execute(RHICommandContextRef context) = 0;
};

// RHICommandList Class

class RHICommandList {
public:
    RHICommandList(const CommandListInfo& info) : info_(info) {}
    ~RHICommandList() {
        if (info_.pool && info_.context) {
            info_.pool->return_to_pool(info_.context);
        }
    }

    void* raw_handle() { return info_.context->raw_handle(); }

    void begin_command();
    void end_command();
    void execute(RHIFenceRef fence = nullptr, RHISemaphoreRef wait_semaphore = nullptr, RHISemaphoreRef signal_semaphore = nullptr);

    void texture_barrier(const RHITextureBarrier& barrier);
    void buffer_barrier(const RHIBufferBarrier& barrier);
    void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset);
    void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource);
    void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size);
    void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource);
    void generate_mips(RHITextureRef src);

    void push_event(const std::string& name, Color3 color = {0.0f, 0.0f, 0.0f});
    void pop_event();

    void begin_render_pass(RHIRenderPassRef render_pass);
    void end_render_pass();

    void set_viewport(Offset2D min, Offset2D max);
    void set_scissor(Offset2D min, Offset2D max);
    void set_depth_bias(float constant_bias, float slope_bias, float clamp_bias);
    void set_line_width(float width);

    void set_graphics_pipeline(RHIGraphicsPipelineRef graphics_pipeline);
    void set_compute_pipeline(RHIComputePipelineRef compute_pipeline);
    void set_ray_tracing_pipeline(RHIRayTracingPipelineRef ray_tracing_pipeline);

    void push_constants(void* data, uint16_t size, ShaderFrequency frequency);
    void bind_descriptor_set(RHIDescriptorSetRef descriptor, uint32_t set = 0);
    void bind_constant_buffer(RHIBufferRef buffer, uint32_t slot, ShaderFrequency frequency);
    void bind_texture(RHITextureRef texture, uint32_t slot, ShaderFrequency frequency);
    void bind_sampler(RHISamplerRef sampler, uint32_t slot, ShaderFrequency frequency);
    void bind_vertex_buffer(RHIBufferRef vertex_buffer, uint32_t stream_index = 0, uint32_t offset = 0);
    void bind_index_buffer(RHIBufferRef index_buffer, uint32_t offset = 0);

    void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
    void dispatch_indirect(RHIBufferRef argument_buffer, uint32_t argument_offset = 0);
    void trace_rays(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

    void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
    void draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, uint32_t vertex_offset = 0,
                      uint32_t first_instance = 0);
    void draw_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count);
    void draw_indexed_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count);

    void imgui_create_fonts_texture();
    void imgui_render_draw_data();

protected:
    CommandListInfo info_;
    std::vector<RHICommand*> commands_;

    void add_command(RHICommand* command) { commands_.push_back(command); }
};

class RHICommandListImmediate {
public:
    RHICommandListImmediate(const CommandListImmediateInfo& info) : info_(info) {}

    void flush();
    void texture_barrier(const RHITextureBarrier& barrier);
    void buffer_barrier(const RHIBufferBarrier& barrier);
    void copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset);
    void copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource);
    void copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size);
    void copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource);
    void generate_mips(RHITextureRef src);

protected:
    CommandListImmediateInfo info_;
    std::vector<RHICommandImmediate*> commands_;

    void add_command(RHICommandImmediate* command) { commands_.push_back(command); }
};
using RHICommandListImmediateRef = std::shared_ptr<RHICommandListImmediate>;

// Command Implementations (Inline)

#define ADD_COMMAND(CommandName, ...)          \
    do {                                       \
        RHICommand* cmd = new CommandName(__VA_ARGS__); \
        add_command(cmd);                      \
    } while (0)

#define ADD_COMMAND_IMMEDIATE(CommandName, ...)         \
    do {                                                \
        RHICommandImmediate* cmd = new CommandName(__VA_ARGS__); \
        add_command(cmd);                               \
    } while (0)

// Struct Definitions for Commands

struct RHICommandBeginCommand : public RHICommand {
    void execute(RHICommandContextRef context) override { context->begin_command(); }
};

struct RHICommandEndCommand : public RHICommand {
    void execute(RHICommandContextRef context) override { context->end_command(); }
};

struct RHICommandTextureBarrier : public RHICommand {
    RHITextureBarrier barrier;
    RHICommandTextureBarrier(const RHITextureBarrier& b) : barrier(b) {}
    void execute(RHICommandContextRef context) override { context->texture_barrier(barrier); }
};

struct RHICommandBufferBarrier : public RHICommand {
    RHIBufferBarrier barrier;
    RHICommandBufferBarrier(const RHIBufferBarrier& b) : barrier(b) {}
    void execute(RHICommandContextRef context) override { context->buffer_barrier(barrier); }
};

struct RHICommandCopyTextureToBuffer : public RHICommand {
    RHITextureRef src;
    TextureSubresourceLayers src_subresource;
    RHIBufferRef dst;
    uint64_t dst_offset;
    RHICommandCopyTextureToBuffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff)
        : src(s), src_subresource(ss), dst(d), dst_offset(doff) {}
    void execute(RHICommandContextRef context) override { context->copy_texture_to_buffer(src, src_subresource, dst, dst_offset); }
};

struct RHICommandCopyBufferToTexture : public RHICommand {
    RHIBufferRef src;
    uint64_t src_offset;
    RHITextureRef dst;
    TextureSubresourceLayers dst_subresource;
    RHICommandCopyBufferToTexture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds)
        : src(s), src_offset(soff), dst(d), dst_subresource(ds) {}
    void execute(RHICommandContextRef context) override { context->copy_buffer_to_texture(src, src_offset, dst, dst_subresource); }
};

struct RHICommandCopyBuffer : public RHICommand {
    RHIBufferRef src;
    uint64_t src_offset;
    RHIBufferRef dst;
    uint64_t dst_offset;
    uint64_t size;
    RHICommandCopyBuffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz)
        : src(s), src_offset(soff), dst(d), dst_offset(doff), size(sz) {}
    void execute(RHICommandContextRef context) override { context->copy_buffer(src, src_offset, dst, dst_offset, size); }
};

struct RHICommandCopyTexture : public RHICommand {
    RHITextureRef src;
    TextureSubresourceLayers src_subresource;
    RHITextureRef dst;
    TextureSubresourceLayers dst_subresource;
    RHICommandCopyTexture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds)
        : src(s), src_subresource(ss), dst(d), dst_subresource(ds) {}
    void execute(RHICommandContextRef context) override { context->copy_texture(src, src_subresource, dst, dst_subresource); }
};

struct RHICommandGenerateMips : public RHICommand {
    RHITextureRef src;
    RHICommandGenerateMips(RHITextureRef s) : src(s) {}
    void execute(RHICommandContextRef context) override { context->generate_mips(src); }
};

struct RHICommandPushEvent : public RHICommand {
    std::string name;
    Color3 color;
    RHICommandPushEvent(const std::string& n, Color3 c) : name(n), color(c) {}
    void execute(RHICommandContextRef context) override { context->push_event(name, color); }
};

struct RHICommandPopEvent : public RHICommand {
    void execute(RHICommandContextRef context) override { context->pop_event(); }
};

struct RHICommandBeginRenderPass : public RHICommand {
    RHIRenderPassRef render_pass;
    RHICommandBeginRenderPass(RHIRenderPassRef rp) : render_pass(rp) {}
    void execute(RHICommandContextRef context) override { context->begin_render_pass(render_pass); }
};

struct RHICommandEndRenderPass : public RHICommand {
    void execute(RHICommandContextRef context) override { context->end_render_pass(); }
};

struct RHICommandSetViewport : public RHICommand {
    Offset2D min_pos;
    Offset2D max_pos;
    RHICommandSetViewport(Offset2D mi, Offset2D ma) : min_pos(mi), max_pos(ma) {}
    void execute(RHICommandContextRef context) override { context->set_viewport(min_pos, max_pos); }
};

struct RHICommandSetScissor : public RHICommand {
    Offset2D min_pos;
    Offset2D max_pos;
    RHICommandSetScissor(Offset2D mi, Offset2D ma) : min_pos(mi), max_pos(ma) {}
    void execute(RHICommandContextRef context) override { context->set_scissor(min_pos, max_pos); }
};

struct RHICommandSetDepthBias : public RHICommand {
    float constant_bias;
    float slope_bias;
    float clamp_bias;
    RHICommandSetDepthBias(float c, float s, float cl) : constant_bias(c), slope_bias(s), clamp_bias(cl) {}
    void execute(RHICommandContextRef context) override { context->set_depth_bias(constant_bias, slope_bias, clamp_bias); }
};

struct RHICommandSetLineWidth : public RHICommand {
    float width;
    RHICommandSetLineWidth(float w) : width(w) {}
    void execute(RHICommandContextRef context) override { context->set_line_width(width); }
};

struct RHICommandSetGraphicsPipeline : public RHICommand {
    RHIGraphicsPipelineRef pipeline;
    RHICommandSetGraphicsPipeline(RHIGraphicsPipelineRef p) : pipeline(p) {}
    void execute(RHICommandContextRef context) override { context->set_graphics_pipeline(pipeline); }
};

struct RHICommandSetComputePipeline : public RHICommand {
    RHIComputePipelineRef pipeline;
    RHICommandSetComputePipeline(RHIComputePipelineRef p) : pipeline(p) {}
    void execute(RHICommandContextRef context) override { context->set_compute_pipeline(pipeline); }
};

struct RHICommandSetRayTracingPipeline : public RHICommand {
    RHIRayTracingPipelineRef pipeline;
    RHICommandSetRayTracingPipeline(RHIRayTracingPipelineRef p) : pipeline(p) {}
    void execute(RHICommandContextRef context) override { context->set_ray_tracing_pipeline(pipeline); }
};

struct RHICommandPushConstants : public RHICommand {
    uint8_t data[256] = {0};
    uint16_t size;
    ShaderFrequency frequency;
    RHICommandPushConstants(void* d, uint16_t s, ShaderFrequency f) : size(s), frequency(f) {
        assert(size <= 256);
        memcpy(&this->data[0], d, size);
    }
    void execute(RHICommandContextRef context) override { context->push_constants(&data[0], size, frequency); }
};

struct RHICommandBindDescriptorSet : public RHICommand {
    RHIDescriptorSetRef descriptor;
    uint32_t set;
    RHICommandBindDescriptorSet(RHIDescriptorSetRef d, uint32_t s) : descriptor(d), set(s) {}
    void execute(RHICommandContextRef context) override { context->bind_descriptor_set(descriptor, set); }
};

struct RHICommandBindConstantBuffer : public RHICommand {
    RHIBufferRef buffer;
    uint32_t slot;
    ShaderFrequency frequency;
    RHICommandBindConstantBuffer(RHIBufferRef b, uint32_t s, ShaderFrequency f) : buffer(b), slot(s), frequency(f) {}
    void execute(RHICommandContextRef context) override { context->bind_constant_buffer(buffer, slot, frequency); }
};

struct RHICommandBindTexture : public RHICommand {
    RHITextureRef texture;
    uint32_t slot;
    ShaderFrequency frequency;
    RHICommandBindTexture(RHITextureRef t, uint32_t s, ShaderFrequency f) : texture(t), slot(s), frequency(f) {}
    void execute(RHICommandContextRef context) override { context->bind_texture(texture, slot, frequency); }
};

struct RHICommandBindSampler : public RHICommand {
    RHISamplerRef sampler;
    uint32_t slot;
    ShaderFrequency frequency;
    RHICommandBindSampler(RHISamplerRef s, uint32_t sl, ShaderFrequency f) : sampler(s), slot(sl), frequency(f) {}
    void execute(RHICommandContextRef context) override { context->bind_sampler(sampler, slot, frequency); }
};

struct RHICommandBindVertexBuffer : public RHICommand {
    RHIBufferRef buffer;
    uint32_t stream_index;
    uint32_t offset;
    RHICommandBindVertexBuffer(RHIBufferRef b, uint32_t s, uint32_t o) : buffer(b), stream_index(s), offset(o) {}
    void execute(RHICommandContextRef context) override { context->bind_vertex_buffer(buffer, stream_index, offset); }
};

struct RHICommandBindIndexBuffer : public RHICommand {
    RHIBufferRef buffer;
    uint32_t offset;
    RHICommandBindIndexBuffer(RHIBufferRef b, uint32_t o) : buffer(b), offset(o) {}
    void execute(RHICommandContextRef context) override { context->bind_index_buffer(buffer, offset); }
};

struct RHICommandDispatch : public RHICommand {
    uint32_t x, y, z;
    RHICommandDispatch(uint32_t gx, uint32_t gy, uint32_t gz) : x(gx), y(gy), z(gz) {}
    void execute(RHICommandContextRef context) override { context->dispatch(x, y, z); }
};

struct RHICommandDispatchIndirect : public RHICommand {
    RHIBufferRef buffer;
    uint32_t offset;
    RHICommandDispatchIndirect(RHIBufferRef b, uint32_t o) : buffer(b), offset(o) {}
    void execute(RHICommandContextRef context) override { context->dispatch_indirect(buffer, offset); }
};

struct RHICommandTraceRays : public RHICommand {
    uint32_t x, y, z;
    RHICommandTraceRays(uint32_t gx, uint32_t gy, uint32_t gz) : x(gx), y(gy), z(gz) {}
    void execute(RHICommandContextRef context) override { context->trace_rays(x, y, z); }
};

struct RHICommandDraw : public RHICommand {
    uint32_t vc, ic, fv, fi;
    RHICommandDraw(uint32_t v, uint32_t i, uint32_t f, uint32_t inst) : vc(v), ic(i), fv(f), fi(inst) {}
    void execute(RHICommandContextRef context) override { context->draw(vc, ic, fv, fi); }
};

struct RHICommandDrawIndexed : public RHICommand {
    uint32_t ic, instc, fi, vo, finst;
    RHICommandDrawIndexed(uint32_t i, uint32_t inst, uint32_t f, uint32_t v, uint32_t first_inst)
        : ic(i), instc(inst), fi(f), vo(v), finst(first_inst) {}
    void execute(RHICommandContextRef context) override { context->draw_indexed(ic, instc, fi, vo, finst); }
};

struct RHICommandDrawIndirect : public RHICommand {
    RHIBufferRef buffer;
    uint32_t offset;
    uint32_t count;
    RHICommandDrawIndirect(RHIBufferRef b, uint32_t o, uint32_t c) : buffer(b), offset(o), count(c) {}
    void execute(RHICommandContextRef context) override { context->draw_indirect(buffer, offset, count); }
};

struct RHICommandDrawIndexedIndirect : public RHICommand {
    RHIBufferRef buffer;
    uint32_t offset;
    uint32_t count;
    RHICommandDrawIndexedIndirect(RHIBufferRef b, uint32_t o, uint32_t c) : buffer(b), offset(o), count(c) {}
    void execute(RHICommandContextRef context) override { context->draw_indexed_indirect(buffer, offset, count); }
};

struct RHICommandImGuiCreateFontsTexture : public RHICommand {
    void execute(RHICommandContextRef context) override { context->imgui_create_fonts_texture(); }
};

struct RHICommandImGuiRenderDrawData : public RHICommand {
    void execute(RHICommandContextRef context) override { context->imgui_render_draw_data(); }
};

// Immediate Commands

struct RHICommandImmediateTextureBarrier : public RHICommandImmediate {
    RHITextureBarrier barrier;
    RHICommandImmediateTextureBarrier(const RHITextureBarrier& b) : barrier(b) {}
    void execute(RHICommandContextImmediateRef context) override { context->texture_barrier(barrier); }
};

struct RHICommandImmediateBufferBarrier : public RHICommandImmediate {
    RHIBufferBarrier barrier;
    RHICommandImmediateBufferBarrier(const RHIBufferBarrier& b) : barrier(b) {}
    void execute(RHICommandContextImmediateRef context) override { context->buffer_barrier(barrier); }
};

struct RHICommandImmediateCopyTextureToBuffer : public RHICommandImmediate {
    RHITextureRef src;
    TextureSubresourceLayers src_subresource;
    RHIBufferRef dst;
    uint64_t dst_offset;
    RHICommandImmediateCopyTextureToBuffer(RHITextureRef s, TextureSubresourceLayers ss, RHIBufferRef d, uint64_t doff)
        : src(s), src_subresource(ss), dst(d), dst_offset(doff) {}
    void execute(RHICommandContextImmediateRef context) override { context->copy_texture_to_buffer(src, src_subresource, dst, dst_offset); }
};

struct RHICommandImmediateCopyBufferToTexture : public RHICommandImmediate {
    RHIBufferRef src;
    uint64_t src_offset;
    RHITextureRef dst;
    TextureSubresourceLayers dst_subresource;
    RHICommandImmediateCopyBufferToTexture(RHIBufferRef s, uint64_t soff, RHITextureRef d, TextureSubresourceLayers ds)
        : src(s), src_offset(soff), dst(d), dst_subresource(ds) {}
    void execute(RHICommandContextImmediateRef context) override { context->copy_buffer_to_texture(src, src_offset, dst, dst_subresource); }
};

struct RHICommandImmediateCopyBuffer : public RHICommandImmediate {
    RHIBufferRef src;
    uint64_t src_offset;
    RHIBufferRef dst;
    uint64_t dst_offset;
    uint64_t size;
    RHICommandImmediateCopyBuffer(RHIBufferRef s, uint64_t soff, RHIBufferRef d, uint64_t doff, uint64_t sz)
        : src(s), src_offset(soff), dst(d), dst_offset(doff), size(sz) {}
    void execute(RHICommandContextImmediateRef context) override { context->copy_buffer(src, src_offset, dst, dst_offset, size); }
};

struct RHICommandImmediateCopyTexture : public RHICommandImmediate {
    RHITextureRef src;
    TextureSubresourceLayers src_subresource;
    RHITextureRef dst;
    TextureSubresourceLayers dst_subresource;
    RHICommandImmediateCopyTexture(RHITextureRef s, TextureSubresourceLayers ss, RHITextureRef d, TextureSubresourceLayers ds)
        : src(s), src_subresource(ss), dst(d), dst_subresource(ds) {}
    void execute(RHICommandContextImmediateRef context) override { context->copy_texture(src, src_subresource, dst, dst_subresource); }
};

struct RHICommandImmediateGenerateMips : public RHICommandImmediate {
    RHITextureRef src;
    RHICommandImmediateGenerateMips(RHITextureRef s) : src(s) {}
    void execute(RHICommandContextImmediateRef context) override { context->generate_mips(src); }
};

// RHICommandList Implementation

inline void RHICommandList::begin_command() {
    if (info_.bypass) info_.context->begin_command();
    else ADD_COMMAND(RHICommandBeginCommand);
}

inline void RHICommandList::end_command() {
    if (info_.bypass) info_.context->end_command();
    else ADD_COMMAND(RHICommandEndCommand);
}

inline void RHICommandList::execute(RHIFenceRef fence, RHISemaphoreRef wait_semaphore, RHISemaphoreRef signal_semaphore) {
    if (!info_.bypass) {
        for (auto* cmd : commands_) {
            cmd->execute(info_.context);
            delete cmd;
        }
        commands_.clear();
    }
    info_.context->execute(fence, wait_semaphore, signal_semaphore);
}

inline void RHICommandList::texture_barrier(const RHITextureBarrier& barrier) {
    if (info_.bypass) info_.context->texture_barrier(barrier);
    else ADD_COMMAND(RHICommandTextureBarrier, barrier);
}

inline void RHICommandList::buffer_barrier(const RHIBufferBarrier& barrier) {
    if (info_.bypass) info_.context->buffer_barrier(barrier);
    else ADD_COMMAND(RHICommandBufferBarrier, barrier);
}

inline void RHICommandList::copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) {
    if (info_.bypass) info_.context->copy_texture_to_buffer(src, src_subresource, dst, dst_offset);
    else ADD_COMMAND(RHICommandCopyTextureToBuffer, src, src_subresource, dst, dst_offset);
}

inline void RHICommandList::copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) {
    if (info_.bypass) info_.context->copy_buffer_to_texture(src, src_offset, dst, dst_subresource);
    else ADD_COMMAND(RHICommandCopyBufferToTexture, src, src_offset, dst, dst_subresource);
}

inline void RHICommandList::copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) {
    if (info_.bypass) info_.context->copy_buffer(src, src_offset, dst, dst_offset, size);
    else ADD_COMMAND(RHICommandCopyBuffer, src, src_offset, dst, dst_offset, size);
}

inline void RHICommandList::copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) {
    if (info_.bypass) info_.context->copy_texture(src, src_subresource, dst, dst_subresource);
    else ADD_COMMAND(RHICommandCopyTexture, src, src_subresource, dst, dst_subresource);
}

inline void RHICommandList::generate_mips(RHITextureRef src) {
    if (info_.bypass) info_.context->generate_mips(src);
    else ADD_COMMAND(RHICommandGenerateMips, src);
}

inline void RHICommandList::push_event(const std::string& name, Color3 color) {
    if (info_.bypass) info_.context->push_event(name, color);
    else ADD_COMMAND(RHICommandPushEvent, name, color);
}

inline void RHICommandList::pop_event() {
    if (info_.bypass) info_.context->pop_event();
    else ADD_COMMAND(RHICommandPopEvent);
}

inline void RHICommandList::begin_render_pass(RHIRenderPassRef render_pass) {
    if (info_.bypass) info_.context->begin_render_pass(render_pass);
    else ADD_COMMAND(RHICommandBeginRenderPass, render_pass);
}

inline void RHICommandList::end_render_pass() {
    if (info_.bypass) info_.context->end_render_pass();
    else ADD_COMMAND(RHICommandEndRenderPass);
}

inline void RHICommandList::set_viewport(Offset2D min, Offset2D max) {
    if (info_.bypass) info_.context->set_viewport(min, max);
    else ADD_COMMAND(RHICommandSetViewport, min, max);
}

inline void RHICommandList::set_scissor(Offset2D min, Offset2D max) {
    if (info_.bypass) info_.context->set_scissor(min, max);
    else ADD_COMMAND(RHICommandSetScissor, min, max);
}

inline void RHICommandList::set_depth_bias(float constant_bias, float slope_bias, float clamp_bias) {
    if (info_.bypass) info_.context->set_depth_bias(constant_bias, slope_bias, clamp_bias);
    else ADD_COMMAND(RHICommandSetDepthBias, constant_bias, slope_bias, clamp_bias);
}

inline void RHICommandList::set_line_width(float width) {
    if (info_.bypass) info_.context->set_line_width(width);
    else ADD_COMMAND(RHICommandSetLineWidth, width);
}

inline void RHICommandList::set_graphics_pipeline(RHIGraphicsPipelineRef graphics_pipeline) {
    if (info_.bypass) info_.context->set_graphics_pipeline(graphics_pipeline);
    else ADD_COMMAND(RHICommandSetGraphicsPipeline, graphics_pipeline);
}

inline void RHICommandList::set_compute_pipeline(RHIComputePipelineRef compute_pipeline) {
    if (info_.bypass) info_.context->set_compute_pipeline(compute_pipeline);
    else ADD_COMMAND(RHICommandSetComputePipeline, compute_pipeline);
}

inline void RHICommandList::set_ray_tracing_pipeline(RHIRayTracingPipelineRef ray_tracing_pipeline) {
    if (info_.bypass) info_.context->set_ray_tracing_pipeline(ray_tracing_pipeline);
    else ADD_COMMAND(RHICommandSetRayTracingPipeline, ray_tracing_pipeline);
}

inline void RHICommandList::push_constants(void* data, uint16_t size, ShaderFrequency frequency) {
    if (info_.bypass) info_.context->push_constants(data, size, frequency);
    else ADD_COMMAND(RHICommandPushConstants, data, size, frequency);
}

inline void RHICommandList::bind_descriptor_set(RHIDescriptorSetRef descriptor, uint32_t set) {
    if (info_.bypass) info_.context->bind_descriptor_set(descriptor, set);
    else ADD_COMMAND(RHICommandBindDescriptorSet, descriptor, set);
}

inline void RHICommandList::bind_constant_buffer(RHIBufferRef buffer, uint32_t slot, ShaderFrequency frequency) {
    if (info_.bypass) info_.context->bind_constant_buffer(buffer, slot, frequency);
    else ADD_COMMAND(RHICommandBindConstantBuffer, buffer, slot, frequency);
}

inline void RHICommandList::bind_texture(RHITextureRef texture, uint32_t slot, ShaderFrequency frequency) {
    if (info_.bypass) info_.context->bind_texture(texture, slot, frequency);
    else ADD_COMMAND(RHICommandBindTexture, texture, slot, frequency);
}

inline void RHICommandList::bind_sampler(RHISamplerRef sampler, uint32_t slot, ShaderFrequency frequency) {
    if (info_.bypass) info_.context->bind_sampler(sampler, slot, frequency);
    else ADD_COMMAND(RHICommandBindSampler, sampler, slot, frequency);
}

inline void RHICommandList::bind_vertex_buffer(RHIBufferRef vertex_buffer, uint32_t stream_index, uint32_t offset) {
    if (info_.bypass) info_.context->bind_vertex_buffer(vertex_buffer, stream_index, offset);
    else ADD_COMMAND(RHICommandBindVertexBuffer, vertex_buffer, stream_index, offset);
}

inline void RHICommandList::bind_index_buffer(RHIBufferRef index_buffer, uint32_t offset) {
    if (info_.bypass) info_.context->bind_index_buffer(index_buffer, offset);
    else ADD_COMMAND(RHICommandBindIndexBuffer, index_buffer, offset);
}

inline void RHICommandList::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    if (info_.bypass) info_.context->dispatch(group_count_x, group_count_y, group_count_z);
    else ADD_COMMAND(RHICommandDispatch, group_count_x, group_count_y, group_count_z);
}

inline void RHICommandList::dispatch_indirect(RHIBufferRef argument_buffer, uint32_t argument_offset) {
    if (info_.bypass) info_.context->dispatch_indirect(argument_buffer, argument_offset);
    else ADD_COMMAND(RHICommandDispatchIndirect, argument_buffer, argument_offset);
}

inline void RHICommandList::trace_rays(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    if (info_.bypass) info_.context->trace_rays(group_count_x, group_count_y, group_count_z);
    else ADD_COMMAND(RHICommandTraceRays, group_count_x, group_count_y, group_count_z);
}

inline void RHICommandList::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
    if (info_.bypass) info_.context->draw(vertex_count, instance_count, first_vertex, first_instance);
    else ADD_COMMAND(RHICommandDraw, vertex_count, instance_count, first_vertex, first_instance);
}

inline void RHICommandList::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) {
    if (info_.bypass) info_.context->draw_indexed(index_count, instance_count, first_index, vertex_offset, first_instance);
    else ADD_COMMAND(RHICommandDrawIndexed, index_count, instance_count, first_index, vertex_offset, first_instance);
}

inline void RHICommandList::draw_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) {
    if (info_.bypass) info_.context->draw_indirect(argument_buffer, offset, draw_count);
    else ADD_COMMAND(RHICommandDrawIndirect, argument_buffer, offset, draw_count);
}

inline void RHICommandList::draw_indexed_indirect(RHIBufferRef argument_buffer, uint32_t offset, uint32_t draw_count) {
    if (info_.bypass) info_.context->draw_indexed_indirect(argument_buffer, offset, draw_count);
    else ADD_COMMAND(RHICommandDrawIndexedIndirect, argument_buffer, offset, draw_count);
}

inline void RHICommandList::imgui_create_fonts_texture() {
    if (info_.bypass) info_.context->imgui_create_fonts_texture();
    else ADD_COMMAND(RHICommandImGuiCreateFontsTexture);
}

inline void RHICommandList::imgui_render_draw_data() {
    if (info_.bypass) info_.context->imgui_render_draw_data();
    else ADD_COMMAND(RHICommandImGuiRenderDrawData);
}

// RHICommandListImmediate Implementation

inline void RHICommandListImmediate::flush() {
    for (auto* cmd : commands_) {
        cmd->execute(info_.context);
        delete cmd;
    }
    commands_.clear();
    info_.context->flush();
}

inline void RHICommandListImmediate::texture_barrier(const RHITextureBarrier& barrier) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateTextureBarrier, barrier);
}

inline void RHICommandListImmediate::buffer_barrier(const RHIBufferBarrier& barrier) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateBufferBarrier, barrier);
}

inline void RHICommandListImmediate::copy_texture_to_buffer(RHITextureRef src, TextureSubresourceLayers src_subresource, RHIBufferRef dst, uint64_t dst_offset) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateCopyTextureToBuffer, src, src_subresource, dst, dst_offset);
}

inline void RHICommandListImmediate::copy_buffer_to_texture(RHIBufferRef src, uint64_t src_offset, RHITextureRef dst, TextureSubresourceLayers dst_subresource) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateCopyBufferToTexture, src, src_offset, dst, dst_subresource);
}

inline void RHICommandListImmediate::copy_buffer(RHIBufferRef src, uint64_t src_offset, RHIBufferRef dst, uint64_t dst_offset, uint64_t size) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateCopyBuffer, src, src_offset, dst, dst_offset, size);
}

inline void RHICommandListImmediate::copy_texture(RHITextureRef src, TextureSubresourceLayers src_subresource, RHITextureRef dst, TextureSubresourceLayers dst_subresource) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateCopyTexture, src, src_subresource, dst, dst_subresource);
}

inline void RHICommandListImmediate::generate_mips(RHITextureRef src) {
    ADD_COMMAND_IMMEDIATE(RHICommandImmediateGenerateMips, src);
}
