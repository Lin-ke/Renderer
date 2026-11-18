#include "engine/function/render/render_pass/bloom_pass.h"
#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/main/engine_context.h"

#include <cmath>

DEFINE_LOG_TAG(LogBloom, "Bloom");

namespace render {

namespace {
struct BloomParameters {
    uint32_t width;
    uint32_t height;
    float value;
    uint32_t horizontal;
};
static_assert(sizeof(BloomParameters) == 16);
}

void BloomPass::init() {
    const auto backend = EngineContext::rhi();
    const auto source = ShaderUtils::load_shader_source("bloom.hlsl");
    if (!backend || !source) {
        ERR(LogBloom, "Cannot load bloom.hlsl");
        return;
    }

    auto create_shader = [&](const char* entry, ShaderFrequency frequency, const char* profile) {
        return backend->create_shader({.entry = entry, .frequency = frequency,
            .code = backend->compile_shader(source->c_str(), entry, profile)});
    };
    const auto bright = create_shader("BrightCS", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    const auto blur = create_shader("BlurCS", SHADER_FREQUENCY_COMPUTE, "cs_5_0");
    const auto vertex = create_shader("CompositeVS", SHADER_FREQUENCY_VERTEX, "vs_5_0");
    const auto fragment = create_shader("CompositePS", SHADER_FREQUENCY_FRAGMENT, "ps_5_0");
    if (!bright || !blur || !vertex || !fragment) {
        ERR(LogBloom, "Failed to compile bloom shaders");
        return;
    }

    RHIRootSignatureInfo compute_layout;
    compute_layout.add_entry({.frequency = SHADER_FREQUENCY_COMPUTE, .type = RESOURCE_TYPE_TEXTURE});
    compute_layout.add_entry({.frequency = SHADER_FREQUENCY_COMPUTE, .type = RESOURCE_TYPE_RW_TEXTURE});
    compute_layout.add_push_constant({.size = sizeof(BloomParameters), .frequency = SHADER_FREQUENCY_COMPUTE});
    compute_root_ = backend->create_root_signature(compute_layout);

    RHIRootSignatureInfo composite_layout;
    composite_layout.add_entry({.binding = 0, .frequency = SHADER_FREQUENCY_FRAGMENT, .type = RESOURCE_TYPE_TEXTURE});
    composite_layout.add_entry({.binding = 1, .frequency = SHADER_FREQUENCY_FRAGMENT, .type = RESOURCE_TYPE_TEXTURE});
    composite_layout.add_entry({.frequency = SHADER_FREQUENCY_FRAGMENT, .type = RESOURCE_TYPE_SAMPLER});
    composite_layout.add_push_constant({.size = sizeof(BloomParameters), .frequency = SHADER_FREQUENCY_FRAGMENT});
    composite_root_ = backend->create_root_signature(composite_layout);
    if (!compute_root_ || !composite_root_) {
        ERR(LogBloom, "Failed to create bloom root signatures");
        return;
    }

    bright_pipeline_ = backend->create_compute_pipeline({bright, compute_root_});
    blur_pipeline_ = backend->create_compute_pipeline({blur, compute_root_});
    RHIGraphicsPipelineInfo composite_info{};
    composite_info.vertex_shader = vertex;
    composite_info.fragment_shader = fragment;
    composite_info.root_signature = composite_root_;
    composite_info.rasterizer_state.cull_mode = CULL_MODE_NONE;
    composite_info.depth_stencil_state.enable_depth_test = false;
    composite_info.depth_stencil_state.enable_depth_write = false;
    composite_info.color_attachment_formats[0] = EngineContext::render_system()->get_color_format();
    composite_pipeline_ = backend->create_graphics_pipeline(composite_info);
    sampler_ = backend->create_sampler({.min_filter = FILTER_TYPE_LINEAR, .mag_filter = FILTER_TYPE_LINEAR,
        .address_mode_u = ADDRESS_MODE_CLAMP_TO_EDGE, .address_mode_v = ADDRESS_MODE_CLAMP_TO_EDGE});
    if (!is_ready()) ERR(LogBloom, "Failed to create bloom pipelines or sampler");
}

bool BloomPass::is_ready() const {
    return bright_pipeline_ && blur_pipeline_ && composite_pipeline_ && sampler_;
}

bool BloomPass::build(RDGBuilder& graph, RDGTextureHandle scene_color, RDGTextureHandle final_color,
                      Extent2D extent, const BloomSettings& settings) const {
    if (!is_ready() || !extent.width || !extent.height || scene_color.id() == final_color.id() ||
        !std::isfinite(settings.threshold) || settings.threshold < 0.0f ||
        !std::isfinite(settings.intensity) || settings.intensity < 0.0f) {
        ERR(LogBloom, "Invalid bloom resources, extent, or settings");
        return false;
    }

    const Extent3D half_extent{extent.width / 2 + extent.width % 2, extent.height / 2 + extent.height % 2, 1};
    const auto bright = graph.create_texture("BloomBright").extent(half_extent)
        .format(FORMAT_R16G16B16A16_SFLOAT).allow_read_write().finish();
    const auto blur_x = graph.create_texture("BloomBlurX").extent(half_extent)
        .format(FORMAT_R16G16B16A16_SFLOAT).allow_read_write().finish();
    const auto blur_y = graph.create_texture("BloomBlurY").extent(half_extent)
        .format(FORMAT_R16G16B16A16_SFLOAT).allow_read_write().finish();

    auto add_compute_pass = [&](const char* name, RDGTextureHandle input, RDGTextureHandle output,
                                RHIComputePipelineRef pipeline, uint32_t horizontal) {
        graph.create_compute_pass(name)
            .root_signature(compute_root_)
            .read(0, 0, 0, input)
            .read_write(0, 0, 0, output)
            .execute([pipeline = std::move(pipeline), half_extent, value = settings.threshold, horizontal](RDGPassContext context) {
                auto& command = context.command;
                BloomParameters parameters{half_extent.width, half_extent.height, value, horizontal};
                command->set_compute_pipeline(pipeline);
                command->push_constants(&parameters, sizeof(parameters), SHADER_FREQUENCY_COMPUTE);
                command->bind_descriptor_set(context.descriptors[0], 0);
                command->dispatch((half_extent.width + 7) / 8, (half_extent.height + 7) / 8, 1);
            })
            .finish();
    };
    add_compute_pass("BloomBright", scene_color, bright, bright_pipeline_, 0);
    add_compute_pass("BloomBlurX", bright, blur_x, blur_pipeline_, 1);
    add_compute_pass("BloomBlurY", blur_x, blur_y, blur_pipeline_, 0);

    graph.create_render_pass("BloomComposite")
        .root_signature(composite_root_)
        .read(0, 0, 0, scene_color)
        .read(0, 1, 0, blur_y)
        .color(0, final_color, ATTACHMENT_LOAD_OP_DONT_CARE, ATTACHMENT_STORE_OP_STORE)
        .execute([pipeline = composite_pipeline_, sampler = sampler_, extent, intensity = settings.intensity](RDGPassContext context) {
            auto& command = context.command;
            BloomParameters parameters{extent.width, extent.height, intensity, 0};
            command->set_viewport({0, 0}, {extent.width, extent.height});
            command->set_scissor({0, 0}, {extent.width, extent.height});
            command->set_graphics_pipeline(pipeline);
            command->push_constants(&parameters, sizeof(parameters), SHADER_FREQUENCY_FRAGMENT);
            context.descriptors[0]->write_sampler(0, sampler);
            command->bind_descriptor_set(context.descriptors[0], 0);
            command->draw(3, 1, 0, 0);
        })
        .finish();
    return true;
}

} // namespace render
