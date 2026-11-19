#include "engine/function/render/render_pass/depth_visualize_pass.h"
#include "engine/function/render/graph/rdg_builder.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_resource/shader_utils.h"

namespace render {

DEFINE_LOG_TAG(LogDepthVisualizePass, "DepthVisualizePass");

DepthVisualizePass::DepthVisualizePass() = default;

DepthVisualizePass::~DepthVisualizePass() {
    destroy();
}

void DepthVisualizePass::destroy() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    if (constant_buffer_) constant_buffer_->destroy();
    if (sampler_) sampler_->destroy();
    initialized_ = false;
}

void DepthVisualizePass::init() {
    create_shaders();
    create_constant_buffer();
    create_sampler();
    create_pipeline();
    initialized_ = true;
}

void DepthVisualizePass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load pre-compiled vertex shader
    auto vs_code = ShaderUtils::load_or_compile("depth_visualize_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (vs_code.empty()) {
        ERR(LogDepthVisualizePass, "Failed to load/compile vertex shader");
        return;
    }
    
    RHIShaderInfo vs_info = {};
    vs_info.entry = "VSMain";
    vs_info.frequency = SHADER_FREQUENCY_VERTEX;
    vs_info.code = vs_code;
    vertex_shader_ = std::make_shared<Shader>();
    vertex_shader_->shader_ = backend->create_shader(vs_info);
    
    auto ps_code = ShaderUtils::load_or_compile("depth_visualize_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (ps_code.empty()) {
        ERR(LogDepthVisualizePass, "Failed to load/compile pixel shader");
        return;
    }
    
    RHIShaderInfo ps_info = {};
    ps_info.entry = "PSMain";
    ps_info.frequency = SHADER_FREQUENCY_FRAGMENT;
    ps_info.code = ps_code;
    fragment_shader_ = std::make_shared<Shader>();
    fragment_shader_->shader_ = backend->create_shader(ps_info);
}

void DepthVisualizePass::create_constant_buffer() {
    auto backend = EngineContext::rhi();
    RHIBufferInfo info = {};
    info.size = sizeof(DepthVisualizeConstants);
    info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    constant_buffer_ = backend->create_buffer(info);
}

void DepthVisualizePass::create_sampler() {
    auto backend = EngineContext::rhi();
    RHISamplerInfo sampler_info = {};
    sampler_ = backend->create_sampler(sampler_info);
}

void DepthVisualizePass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_ || !fragment_shader_) return;

    RHIRootSignatureInfo root_info = {};
    root_info.add_entry_from_reflect(vertex_shader_->shader_);
    root_info.add_entry_from_reflect(fragment_shader_->shader_);
    // Keep the parameter slot stable when the current visualization shader
    // optimizes out near/far plane constants.
    root_info.add_entry({.binding = 0, .frequency = SHADER_FREQUENCY_FRAGMENT, .type = RESOURCE_TYPE_UNIFORM_BUFFER});
    root_signature_ = backend->create_root_signature(root_info);
    if (root_signature_) descriptor_set_ = root_signature_->create_descriptor_set(0);

    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    pipe_info.fragment_shader = fragment_shader_->shader_;
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
    pipe_info.vertex_input_state.vertex_elements.clear();

    pipe_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    
    pipe_info.depth_stencil_state.enable_depth_test = false;
    pipe_info.depth_stencil_state.enable_depth_write = false;

    // NOTE: This assumes backbuffer format is consistently R8G8B8A8_UNORM
    pipe_info.color_attachment_formats[0] = FORMAT_R8G8B8A8_UNORM; 
    pipe_info.depth_stencil_attachment_format = FORMAT_UKNOWN; // No depth buffer bound

    pipeline_ = backend->create_graphics_pipeline(pipe_info);
}

void DepthVisualizePass::build(RDGBuilder& graph, RDGTextureHandle depth, RDGTextureHandle output, float near_plane, float far_plane) {
    if (!initialized_ || !pipeline_ || !constant_buffer_) return;
    const auto constants = graph.create_buffer("DepthVisualizeConstants")
        .import(constant_buffer_, RESOURCE_STATE_VERTEX_BUFFER).finish();
    graph.create_render_pass("DepthVisualize")
        .root_signature(root_signature_)
        .read(0, 0, 0, depth)
        .read(0, 0, 0, constants)
        .color(0, output, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE)
        .execute([this, near_plane, far_plane](RDGPassContext context) {
            const DepthVisualizeConstants data{near_plane, far_plane, {}};
            if (void* mapped = constant_buffer_->map()) {
                memcpy(mapped, &data, sizeof(data));
                constant_buffer_->unmap();
            }
            context.descriptors[0]->write_sampler(0, sampler_);
            context.command->set_graphics_pipeline(pipeline_);
            context.command->bind_descriptor_set(context.descriptors[0]);
            context.command->draw(3);
        }).finish();
}

} // namespace render
