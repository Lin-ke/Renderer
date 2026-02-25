#include "engine/function/render/render_pass/depth_pre_pass.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/main/engine_context.h"
#include "engine/core/log/Log.h"
#include "engine/function/render/render_system/render_system.h"

#include <cstring>

DEFINE_LOG_TAG(LogDepthPrePass, "DepthPrePass");

namespace render {

DepthPrePass::~DepthPrePass() {
    if (pipeline_) pipeline_->destroy();
    if (root_signature_) root_signature_->destroy();
    for (auto& buf : per_frame_buffers_) {
        if (buf) buf->destroy();
    }
    if (per_object_buffer_) per_object_buffer_->destroy();
}

void DepthPrePass::init() {
    INFO(LogDepthPrePass, "Initializing DepthPrePass...");
    create_shaders();
    create_uniform_buffers();
    create_pipeline();
    initialized_ = true;
    INFO(LogDepthPrePass, "DepthPrePass initialized successfully");
}

void DepthPrePass::create_shaders() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Load VS
    auto vs_code = ShaderUtils::load_or_compile("depth_pass_vs.cso", nullptr, "VSMain", "vs_5_0");
    if (!vs_code.empty()) {
        RHIShaderInfo info = {};
        info.entry = "VSMain";
        info.frequency = SHADER_FREQUENCY_VERTEX;
        info.code = vs_code;
        auto shader = backend->create_shader(info);
        vertex_shader_ = std::make_shared<Shader>();
        vertex_shader_->shader_ = shader;
    } else {
        ERR(LogDepthPrePass, "Failed to load depth_pass_vs.cso");
    }

    // Load PS (Required for DX11 even if output is void)
    auto ps_code = ShaderUtils::load_or_compile("depth_pass_ps.cso", nullptr, "PSMain", "ps_5_0");
    if (!ps_code.empty()) {
        RHIShaderInfo info = {};
        info.entry = "PSMain";
        info.frequency = SHADER_FREQUENCY_FRAGMENT;
        info.code = ps_code;
        auto shader = backend->create_shader(info);
        fragment_shader_ = std::make_shared<Shader>();
        fragment_shader_->shader_ = shader;
    }
}

void DepthPrePass::create_uniform_buffers() {
    auto backend = EngineContext::rhi();
    if (!backend) return;
    
    // Create per-frame buffers (Triple Buffering)
    per_frame_buffers_.resize(kFramesInFlight);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        RHIBufferInfo frame_info = {};
        frame_info.size = sizeof(PerFrameData);
        frame_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
        frame_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
        frame_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
        per_frame_buffers_[i] = backend->create_buffer(frame_info);
    }

    // Per-object buffer
    // TODO: Ideally this should use dynamic offsets or a large ring buffer to prevent 
    // overwriting data within the same frame. For now, we use a single buffer which 
    // is technically incorrect for multiple objects but matches current project patterns.
    RHIBufferInfo obj_info = {};
    obj_info.size = sizeof(PerObjectData);
    obj_info.type = RESOURCE_TYPE_UNIFORM_BUFFER;
    obj_info.memory_usage = MEMORY_USAGE_CPU_TO_GPU;
    obj_info.creation_flag = BUFFER_CREATION_PERSISTENT_MAP;
    per_object_buffer_ = backend->create_buffer(obj_info);
}

void DepthPrePass::create_pipeline() {
    auto backend = EngineContext::rhi();
    if (!backend || !vertex_shader_) return;

    RHIRootSignatureInfo root_info = {};
    root_signature_ = backend->create_root_signature(root_info);
    if (!root_signature_) return;

    RHIGraphicsPipelineInfo pipe_info = {};
    pipe_info.vertex_shader = vertex_shader_->shader_;
    if (fragment_shader_) {
        pipe_info.fragment_shader = fragment_shader_->shader_;
    }
    pipe_info.root_signature = root_signature_;
    pipe_info.primitive_type = PRIMITIVE_TYPE_TRIANGLE_LIST;

    // Input Layout: Position only
    pipe_info.vertex_input_state.vertex_elements.resize(1);
    pipe_info.vertex_input_state.vertex_elements[0].stream_index = 0;
    pipe_info.vertex_input_state.vertex_elements[0].semantic_name = "POSITION";
    pipe_info.vertex_input_state.vertex_elements[0].format = FORMAT_R32G32B32_SFLOAT;
    pipe_info.vertex_input_state.vertex_elements[0].offset = 0;

    // Rasterizer
    pipe_info.rasterizer_state.cull_mode = CULL_MODE_BACK;
    pipe_info.rasterizer_state.fill_mode = FILL_MODE_SOLID;
    pipe_info.rasterizer_state.depth_clip_mode = DEPTH_CLIP;

    // Depth Stencil
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = true;
    pipe_info.depth_stencil_state.depth_test = COMPARE_FUNCTION_LESS; 

    // Blend State - No color write
    pipe_info.blend_state.render_targets[0].enable = false;
    pipe_info.blend_state.render_targets[0].color_write_mask = 0; 

    // Formats
    // Depth only, no color attachment usually.
    auto render_system = EngineContext::render_system();
    if (render_system) {
        pipe_info.depth_stencil_attachment_format = render_system->get_depth_format();
    } else {
        pipe_info.depth_stencil_attachment_format = FORMAT_D32_SFLOAT;
    }
    
    pipeline_ = backend->create_graphics_pipeline(pipe_info);
    if (!pipeline_) {
        ERR(LogDepthPrePass, "Failed to create pipeline");
    }
}

void DepthPrePass::set_per_frame_data(const Mat4& view, const Mat4& proj) {
    per_frame_data_ = {}; // Zero init
    per_frame_data_.view = view;
    per_frame_data_.proj = proj;
}

void DepthPrePass::build(RDGBuilder& builder) {
    // Empty default
}

void DepthPrePass::build(RDGBuilder& builder, RDGTextureHandle depth_target, const std::vector<DrawBatch>& batches) {
    if (!initialized_ || !pipeline_) return;

    // Create Render Pass
    auto rp_builder = builder.create_render_pass("DepthPrePass")
        .depth_stencil(depth_target, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 1.0f, 0);

    // Get extent for viewport
    Extent2D extent = {1280, 720};
    auto* render_system = EngineContext::render_system();
    if (render_system && render_system->get_swapchain()) {
        extent = render_system->get_swapchain()->get_extent();
    }

    rp_builder.execute([this, batches, extent](RDGPassContext context) {
        RHICommandListRef cmd = context.command;
        if (!cmd) return;

        // Set viewport and scissor to ensure valid rendering state
        cmd->set_viewport({0, 0}, {extent.width, extent.height});
        cmd->set_scissor({0, 0}, {extent.width, extent.height});
        
        cmd->set_graphics_pipeline(pipeline_);
        
        // Update PerFrame Buffer (Double Buffering)
        uint32_t frame_index = EngineContext::current_frame_index() % kFramesInFlight;
        RHIBufferRef current_frame_buffer = per_frame_buffers_[frame_index];

        if (current_frame_buffer) {
            void* mapped = current_frame_buffer->map();
            if (mapped) {
                memcpy(mapped, &per_frame_data_, sizeof(PerFrameData));
                current_frame_buffer->unmap();
            }
            cmd->bind_constant_buffer(current_frame_buffer, 0, SHADER_FREQUENCY_VERTEX);
        }

        // Draw Batches
        for (const auto& batch : batches) {
            // Update PerObject Buffer
            if (per_object_buffer_) {
                PerObjectData obj_data;
                obj_data.model = batch.model_matrix;
                obj_data.inv_model = batch.inv_model_matrix;
                
                void* mapped = per_object_buffer_->map();
                if (mapped) {
                    memcpy(mapped, &obj_data, sizeof(PerObjectData));
                    per_object_buffer_->unmap();
                }
                cmd->bind_constant_buffer(per_object_buffer_, 1, SHADER_FREQUENCY_VERTEX);
            }

            // Vertex Buffer
            if (batch.vertex_buffer) {
                cmd->bind_vertex_buffer(batch.vertex_buffer, 0, 0);
            }
            // No Normal/Tangent/UV needed for depth pass (unless alpha testing)

            // Draw
            if (batch.index_buffer) {
                cmd->bind_index_buffer(batch.index_buffer, 0);
                cmd->draw_indexed(batch.index_count, 1, batch.index_offset, 0, 0);
            }
        }
    });
    
    rp_builder.finish();
}

} // namespace render
