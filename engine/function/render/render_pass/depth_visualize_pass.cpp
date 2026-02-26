#include "engine/function/render/render_pass/depth_visualize_pass.h"
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
    root_signature_ = backend->create_root_signature(root_info);

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

void DepthVisualizePass::draw(RHICommandContextRef command, RHITextureRef depth_texture, RHITextureViewRef output_rtv, Extent2D extent, float near_plane, float far_plane) {
    if (!initialized_ || !pipeline_ || !command) return;

    auto backend = EngineContext::rhi();

    RHIRenderPassInfo rp_info = {};
    rp_info.extent = extent;
    rp_info.color_attachments[0].texture_view = output_rtv;
    rp_info.color_attachments[0].load_op = ATTACHMENT_LOAD_OP_CLEAR;
    rp_info.color_attachments[0].store_op = ATTACHMENT_STORE_OP_STORE;
    rp_info.color_attachments[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    
    RHIRenderPassRef render_pass = backend->create_render_pass(rp_info);
    
    command->begin_render_pass(render_pass);

    DepthVisualizeConstants constants;
    constants.near_plane = near_plane;
    constants.far_plane = far_plane;
    
    if (constant_buffer_) {
        void* mapped = constant_buffer_->map();
        if (mapped) {
            memcpy(mapped, &constants, sizeof(constants));
            constant_buffer_->unmap();
        }
    }
    
    command->set_viewport({0, 0}, {extent.width, extent.height});
    command->set_scissor({0, 0}, {extent.width, extent.height});

    command->set_graphics_pipeline(pipeline_);
    
    command->bind_constant_buffer(constant_buffer_, 0, SHADER_FREQUENCY_FRAGMENT);
    
    command->bind_texture(depth_texture, 0, SHADER_FREQUENCY_FRAGMENT);
    command->bind_sampler(sampler_, 0, SHADER_FREQUENCY_FRAGMENT);
    
    command->draw(3, 1, 0, 0);
    
    command->end_render_pass();
    
    render_pass->destroy();
}

} // namespace render
