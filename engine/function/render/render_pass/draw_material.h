#pragma once

#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_pass/npr_forward_pass.h"
#include "engine/function/render/render_resource/material.h"

namespace render {

struct DrawMaterial {
    RenderPassMasks pass_mask = PASS_MASK_NONE;
    RasterizerCullMode cull_mode = CULL_MODE_BACK;
    bool use_for_depth_pass = true;
    std::optional<GBufferMaterialData> pbr;
    std::optional<NPRMaterialData> npr;
    std::array<RHITextureRef, 7> textures;
};

DrawMaterial snapshot_material(const Material& material);

} // namespace render
